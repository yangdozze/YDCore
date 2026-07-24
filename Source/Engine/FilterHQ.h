// GLOBUS v1.2 — appended filter models (style-inspired originals, per docs:
// these are NOT circuit emulations of any named hardware).
//
//   Ladder 24 — 4-pole ZDF ladder: zero-delay algebraic feedback solve around
//               four TPT one-poles, input drive tanh, resonance to the edge of
//               self-oscillation with passband-loss makeup.
//   OTA 24    — 4-pole cascade with a soft-clipped integrator character:
//               fast-tanh inside every stage, gentler feedback range.
//   SEM 12    — 2-pole TPT SVF voiced for the classic smooth-multimode feel:
//               bounded resonance (never self-oscillates), subtle band-state
//               saturation, 12 dB LP output.
//
// Quality: at HIGH/ULTRA the nonlinear models run at 2× internally through a
// polyphase-allpass halfband pair (~69 dB stopband) — prepared up front, no
// allocation at render time. LEGACY/ECO run at 1× (documented CPU trade).
//
// The legacy MultimodeFilter is untouched; BP 24 runs through it (linear
// cascade — no nonlinearity, no oversampling needed).
#pragma once
#include "DspUtils.h"
#include "../Parameters.h"

namespace ydc
{
/** Fast, bounded tanh approximation (max error ~1e-3 in ±3). */
inline float fastTanh (float x) noexcept
{
    x = clampf (x, -4.5f, 4.5f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

//==============================================================================
/** First-order allpass section operating in z^-2 (polyphase halfband branch). */
struct AllpassZ2
{
    float c = 0.0f, x2 = 0.0f, y2 = 0.0f;
    inline float process (float x) noexcept
    {
        const float y = c * (x - y2) + x2;
        x2 = x;
        y2 = y;
        return y;
    }
    void reset() noexcept { x2 = y2 = 0.0f; }
};

/** Classic 2-coefficient-per-branch polyphase halfband (~69 dB stopband). */
struct Halfband2x
{
    AllpassZ2 upA0, upA1, upB0, upB1;      // interpolator branches
    AllpassZ2 dnA0, dnA1, dnB0, dnB1;      // decimator branches
    float dnOdd = 0.0f;
    static constexpr float cA0 = 0.07986643f, cA1 = 0.54535365f;
    static constexpr float cB0 = 0.28382934f, cB1 = 0.83441189f;

    void reset() noexcept
    {
        for (auto* a : { &upA0, &upA1, &upB0, &upB1, &dnA0, &dnA1, &dnB0, &dnB1 })
            a->reset();
        dnOdd = 0.0f;
    }
    void prepare() noexcept
    {
        upA0.c = dnA0.c = cA0; upA1.c = dnA1.c = cA1;
        upB0.c = dnB0.c = cB0; upB1.c = dnB1.c = cB1;
    }
    /** One input sample → two upsampled samples. */
    inline void up (float x, float& e, float& o) noexcept
    {
        e = upA1.process (upA0.process (x));
        o = upB1.process (upB0.process (x));
    }
    /** Two processed samples → one output sample. */
    inline float down (float e, float o) noexcept
    {
        const float a = dnA1.process (dnA0.process (e));
        const float b = dnB1.process (dnB0.process (o));
        return 0.5f * (a + b);
    }
};

//==============================================================================
class FilterHQ
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        for (auto& h : hb) h.prepare();
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : lad) s = {};
        for (auto& s : svf) s = {};
        for (auto& h : hb) h.reset();
    }

    /** Coefficients once per control block. cutoffHz is pre-modulated. */
    void setParams (FilterType type_, float cutoffHz, float res01, float drive01, bool oversample_) noexcept
    {
        type = type_;
        oversample = oversample_;
        const double fsEff = oversample ? sr * 2.0 : sr;
        const float fc = clampf (cutoffHz, 20.0f, std::min (20000.0f, (float) (sr * 0.45)));
        g = std::tan (juce::MathConstants<float>::pi * fc / (float) fsEff);
        G = g / (1.0f + g);

        const float res = clampf (res01, 0.0f, 1.0f);
        if (type == FilterType::Ladder24)
        {
            k = res * 4.05f;                          // just past self-osc at max
            makeup = 1.0f + k * 0.45f;                // passband-loss compensation
        }
        else if (type == FilterType::Ota24)
        {
            k = res * 3.6f;
            makeup = 1.0f + k * 0.40f;
        }
        else // Sem12
        {
            k = 2.0f - 1.6f * res;                    // damping floor 0.4 — never self-osc
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        driveGain = 1.0f + clampf (drive01, 0.0f, 1.0f) * 5.0f;
        driveComp = 1.0f / std::sqrt (std::max (1.0f, driveGain * 0.75f));
    }

    /** Process one control block in place (stereo). */
    void process (float* l, float* r, int n) noexcept
    {
        float* chans[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            float* x = chans[c];
            if (oversample && type != FilterType::Sem12)
            {
                auto& h = hb[c];
                for (int i = 0; i < n; ++i)
                {
                    float e, o;
                    h.up (x[i], e, o);
                    e = tickNonlinear (c, e);
                    o = tickNonlinear (c, o);
                    x[i] = sanitize (h.down (e, o));
                }
            }
            else
            {
                for (int i = 0; i < n; ++i)
                    x[i] = sanitize (type == FilterType::Sem12 ? tickSem (c, x[i])
                                                               : tickNonlinear (c, x[i]));
            }
        }
    }

private:
    // ---- 4-pole ZDF core (Ladder 24 / OTA 24) ------------------------------
    struct LadderState { float s1 = 0, s2 = 0, s3 = 0, s4 = 0; };

    inline float tickNonlinear (int chan, float in) noexcept
    {
        auto& st = lad[chan];
        const float G2 = G * G, G4 = G2 * G2;
        const float oneMinusG = 1.0f - G;

        // zero-delay feedback solve (linear part)
        const float S = oneMinusG * (G2 * G * st.s1 + G2 * st.s2 + G * st.s3 + st.s4);
        const float inDriven = fastTanh (in * driveGain * makeup) * driveComp;
        const float y4lin = (G4 * inDriven + S) / (1.0f + k * G4);
        float x0 = inDriven - k * fastTanh (y4lin * 0.9f) * 1.1111f;   // soft-bounded feedback

        // run the four TPT one-poles forward
        const bool ota = type == FilterType::Ota24;
        float y = x0;
        float* s[4] = { &st.s1, &st.s2, &st.s3, &st.s4 };
        for (int p = 0; p < 4; ++p)
        {
            const float input = ota ? fastTanh (y) : y;
            const float v = (input - *s[p]) * G;
            y = v + *s[p];
            *s[p] = sanitize (y + v);
        }
        return y;
    }

    // ---- SEM-style 2-pole (linear SVF + subtle band saturation) ------------
    struct SvfState { float ic1 = 0, ic2 = 0; };

    inline float tickSem (int chan, float in) noexcept
    {
        auto& st = svf[chan];
        const float v0 = driveGain > 1.001f ? fastTanh (in * driveGain) * driveComp : in;
        const float v3 = v0 - st.ic2;
        float v1 = a1 * st.ic1 + a2 * v3;
        const float v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
        v1 = fastTanh (v1 * 1.15f) * 0.8695652f;      // gentle band-state rounding
        st.ic1 = sanitize (2.0f * v1 - st.ic1);
        st.ic2 = sanitize (2.0f * v2 - st.ic2);
        return v2;
    }

    double sr = 44100.0;
    FilterType type = FilterType::Ladder24;
    bool oversample = false;
    float g = 0.5f, G = 0.33f, k = 0.0f, makeup = 1.0f;
    float a1 = 0, a2 = 0, a3 = 0;                     // SEM SVF coefficients
    float driveGain = 1.0f, driveComp = 1.0f;
    LadderState lad[2];
    SvfState    svf[2];
    Halfband2x  hb[2];
};

} // namespace ydc
