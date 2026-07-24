// GLOBUS — distortion: tanh waveshaper → one-pole tone LP → dry/wet.
//
// v1.2 quality paths: LEGACY/ECO run the exact 1.1 math. HIGH oversamples the
// shaper 2×, ULTRA 4× (polyphase-allpass halfbands, prepared up front) with a
// DC blocker after the nonlinearity. The oversampled chains measure 2 / 4
// samples of impulse latency — reported to the host by the processor.
#pragma once
#include "../Engine/DspUtils.h"
#include "../Engine/FilterHQ.h"     // Halfband2x
#include <juce_core/juce_core.h>

namespace ydc
{
class Distortion
{
public:
    static constexpr int kLatencyHigh  = 2;   // measured: impulse peak offset at 2×
    static constexpr int kLatencyUltra = 4;   // measured: impulse peak offset at 4×

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& h : hb2) h.prepare();
        for (auto& h : hb4a) h.prepare();
        for (auto& h : hb4b) h.prepare();
        for (auto& h : hb4c) h.prepare();
        dcR_ = 1.0f - (float) (2.0 * juce::MathConstants<double>::pi * 12.0 / sampleRate);
        reset();
    }

    void reset()
    {
        lpL = lpR = 0.0f;
        for (auto& h : hb2) h.reset();
        for (auto& h : hb4a) h.reset();
        for (auto& h : hb4b) h.reset();
        for (auto& h : hb4c) h.reset();
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    }

    /** osFactor: 1 = legacy math (bit-exact 1.1), 2 = HIGH, 4 = ULTRA. */
    void process (float* l, float* r, int n, float drive01, float toneHz, float mix, int osFactor = 1)
    {
        const float g    = 1.0f + drive01 * drive01 * 30.0f;
        const float comp = 1.0f / std::sqrt (std::max (1.0f, g * 0.6f));
        const float c    = 1.0f - std::exp (-kTwoPi * clampf (toneHz, 200.0f, 20000.0f) / (float) sr);
        const float wet  = clampf (mix, 0.0f, 1.0f);
        const float dry  = 1.0f - wet;

        if (osFactor <= 1)
        {
            // ---- LEGACY path (unchanged 1.1 math)
            for (int i = 0; i < n; ++i)
            {
                const float wl = std::tanh (l[i] * g) * comp;
                const float wr = std::tanh (r[i] * g) * comp;
                lpL += c * (wl - lpL);
                lpR += c * (wr - lpR);
                l[i] = l[i] * dry + lpL * wet;
                r[i] = r[i] * dry + lpR * wet;
            }
            return;
        }

        // ---- oversampled shaper → tone LP → DC block → mix
        float* chans[2] = { l, r };
        float* lpState[2] = { &lpL, &lpR };
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = chans[ch];
            float& lp = *lpState[ch];
            for (int i = 0; i < n; ++i)
            {
                float shaped;
                if (osFactor == 2)
                {
                    float e, o;
                    hb2[ch].up (x[i], e, o);
                    e = std::tanh (e * g) * comp;
                    o = std::tanh (o * g) * comp;
                    shaped = hb2[ch].down (e, o);
                }
                else
                {
                    float e, o;
                    hb4a[ch].up (x[i], e, o);
                    float ee, eo, oe, oo;
                    hb4b[ch].up (e, ee, eo);
                    hb4b[ch + 2].up (o, oe, oo);
                    ee = std::tanh (ee * g) * comp;  eo = std::tanh (eo * g) * comp;
                    oe = std::tanh (oe * g) * comp;  oo = std::tanh (oo * g) * comp;
                    const float a = hb4c[ch].down (ee, eo);
                    const float b = hb4c[ch + 2].down (oe, oo);
                    shaped = hb4a[ch].down (a, b);
                }
                lp += c * (shaped - lp);
                // DC blocker (12 Hz) after the nonlinear stage
                const float dcOut = lp - dcX[ch] + dcR_ * dcY[ch];
                dcX[ch] = lp;
                dcY[ch] = sanitize (dcOut);
                x[i] = x[i] * dry + dcY[ch] * wet;
            }
        }
    }

private:
    double sr = 44100.0;
    float lpL = 0.0f, lpR = 0.0f;
    Halfband2x hb2[2];                       // 2× per channel
    Halfband2x hb4a[2], hb4b[4], hb4c[4];    // 4×: outer pair + inner pairs per channel
    float dcX[2] {}, dcY[2] {};
    float dcR_ = 0.998f;
};

} // namespace ydc
