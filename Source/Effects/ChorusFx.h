// GLOBUS — stereo chorus: two modulated delay taps, right channel LFO offset
// by the width control.
#pragma once
#include <vector>
#include <juce_core/juce_core.h>
#include "../Engine/DspUtils.h"

namespace ydc
{
class ChorusFx
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        const int size = juce::nextPowerOfTwo ((int) (0.06 * sampleRate) + 8);
        bufL.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);
        mask = size - 1;
        reset();
    }

    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        writePos = 0;
        phase = 0.0;
    }

    void process (float* l, float* r, int n, float rateHz, float depth01, float width01, float mix)
    {
        const float wet = clampf (mix, 0.0f, 1.0f);
        const float dry = 1.0f - wet * 0.5f;                       // keep body when fully wet
        const double inc = clampf (rateHz, 0.01f, 10.0f) / sr;
        const float base  = 0.012f * (float) sr;                   // 12 ms centre
        const float depth = depth01 * 0.008f * (float) sr;         // ±8 ms
        const float wOfs  = width01 * 0.25f;                       // up to 90° phase offset

        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) writePos] = l[i];
            bufR[(size_t) writePos] = r[i];

            const float phL = (float) phase;
            const float phR = (float) phase + wOfs;
            const float dL = base + depth * std::sin (phL * kTwoPi);
            const float dR = base + depth * std::sin (phR * kTwoPi);

            l[i] = l[i] * dry + readInterp (bufL, dL) * wet;
            r[i] = r[i] * dry + readInterp (bufR, dR) * wet;

            writePos = (writePos + 1) & mask;
            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
        }
    }

private:
    float readInterp (const std::vector<float>& buf, float delaySamples) const
    {
        const float pos = (float) writePos - clampf (delaySamples, 1.0f, (float) mask - 2.0f);
        const int i0 = (int) std::floor (pos);
        const float frac = pos - (float) i0;
        const float a = buf[(size_t) (i0 & mask)];
        const float b = buf[(size_t) ((i0 + 1) & mask)];
        return a + frac * (b - a);
    }

    double sr = 44100.0, phase = 0.0;
    std::vector<float> bufL, bufR;
    int writePos = 0, mask = 0;
};

} // namespace ydc
