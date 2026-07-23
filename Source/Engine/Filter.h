// GLOBUS — multimode TPT state-variable filter (Zavalishin), 12/24 dB, stereo.
#pragma once
#include "DspUtils.h"
#include "../Parameters.h"

namespace ydc
{
class MultimodeFilter
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        maxCutoff = (float) (sampleRate * 0.45);
        reset();
    }

    void reset() noexcept
    {
        for (auto& st : stages)
            st = {};
    }

    /** Update coefficients once per control block. cutoffHz is pre-modulated. */
    void setParams (FilterType type_, float cutoffHz, float res01, float drive01) noexcept
    {
        type = type_;
        const float fc = clampf (cutoffHz, 20.0f, std::min (20000.0f, maxCutoff));
        g = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);

        // resonance: k = 2 (none) .. 0.07 (screaming, still bounded)
        k = 2.0f - 1.93f * clampf (res01, 0.0f, 1.0f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;

        // second (cascade) stage: gentle damping so 24 dB modes stay stable
        k2  = 1.0f;
        b1  = 1.0f / (1.0f + g * (g + k2));
        b2  = g * b1;
        b3  = g * b2;

        driveGain = 1.0f + drive01 * 7.0f;
        driveComp = 1.0f / std::sqrt (driveGain);
        driven    = drive01 > 0.001f;
    }

    /** Process one control block in place (stereo). */
    void process (float* l, float* r, int n) noexcept
    {
        float* chans[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            float* x = chans[c];
            auto& s1 = stages[c];
            auto& s2 = stages[c + 2];

            for (int i = 0; i < n; ++i)
            {
                float v0 = x[i];
                if (driven)
                    v0 = std::tanh (v0 * driveGain) * driveComp;

                float out = tick (s1, v0, a1, a2, a3, k);

                if (type == FilterType::LP24 || type == FilterType::HP24)
                    out = tick (s2, out, b1, b2, b3, k2);

                x[i] = sanitize (out);
            }
        }
    }

private:
    struct State { float ic1 = 0.0f, ic2 = 0.0f; };

    inline float tick (State& st, float v0, float c1, float c2, float c3, float damp) noexcept
    {
        const float v3 = v0 - st.ic2;
        const float v1 = c1 * st.ic1 + c2 * v3;
        const float v2 = st.ic2 + c2 * st.ic1 + c3 * v3;
        st.ic1 = sanitize (2.0f * v1 - st.ic1);
        st.ic2 = sanitize (2.0f * v2 - st.ic2);

        switch (type)
        {
            case FilterType::LP12:
            case FilterType::LP24:  return v2;
            case FilterType::HP12:
            case FilterType::HP24:  return v0 - damp * v1 - v2;
            case FilterType::BP12:  return v1;
            case FilterType::Notch: return v0 - damp * v1;
            default:                return v2;
        }
    }

    double sr = 44100.0;
    float maxCutoff = 20000.0f;
    FilterType type = FilterType::LP24;
    float g = 0.5f, k = 2.0f, a1 = 0, a2 = 0, a3 = 0;
    float k2 = 1.0f, b1 = 0, b2 = 0, b3 = 0;
    float driveGain = 1.0f, driveComp = 1.0f;
    bool driven = false;
    State stages[4]; // [L1, R1, L2, R2]
};

} // namespace ydc
