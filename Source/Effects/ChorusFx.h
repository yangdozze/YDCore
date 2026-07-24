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
        depthSm = -1.0f;
    }

    /** hq (v1.2, quality ≠ LEGACY): Catmull-Rom fractional reads + smoothed
        modulation depth — fewer interpolation artefacts, no zipper on depth
        automation. hq=false is the exact 1.1 path. */
    void process (float* l, float* r, int n, float rateHz, float depth01, float width01, float mix,
                  bool hq = false)
    {
        const float wet = clampf (mix, 0.0f, 1.0f);
        const float dry = 1.0f - wet * 0.5f;                       // keep body when fully wet
        const double inc = clampf (rateHz, 0.01f, 10.0f) / sr;
        const float base  = 0.012f * (float) sr;                   // 12 ms centre
        const float wOfs  = width01 * 0.25f;                       // up to 90° phase offset

        float depth = depth01 * 0.008f * (float) sr;               // ±8 ms
        float depthStep = 0.0f;
        if (hq)
        {
            if (depthSm < 0.0f) depthSm = depth;
            depthStep = (depth - depthSm) / (float) std::max (1, n);
            const float target = depth;
            depth = depthSm;
            depthSm = target;
        }

        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) writePos] = l[i];
            bufR[(size_t) writePos] = r[i];

            const float phL = (float) phase;
            const float phR = (float) phase + wOfs;
            const float dL = base + depth * std::sin (phL * kTwoPi);
            const float dR = base + depth * std::sin (phR * kTwoPi);

            if (hq)
            {
                l[i] = l[i] * dry + readInterp4 (bufL, dL) * wet;
                r[i] = r[i] * dry + readInterp4 (bufR, dR) * wet;
                depth += depthStep;
            }
            else
            {
                l[i] = l[i] * dry + readInterp (bufL, dL) * wet;
                r[i] = r[i] * dry + readInterp (bufR, dR) * wet;
            }

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

    float readInterp4 (const std::vector<float>& buf, float delaySamples) const
    {
        const float pos = (float) writePos - clampf (delaySamples, 2.0f, (float) mask - 3.0f);
        const int i0 = (int) std::floor (pos);
        const float t = pos - (float) i0;
        const float y0 = buf[(size_t) ((i0 - 1) & mask)];
        const float y1 = buf[(size_t) (i0 & mask)];
        const float y2 = buf[(size_t) ((i0 + 1) & mask)];
        const float y3 = buf[(size_t) ((i0 + 2) & mask)];
        const float a = 0.5f * (3.0f * (y1 - y2) + y3 - y0);
        const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c = 0.5f * (y2 - y0);
        return ((a * t + b) * t + c) * t + y1;
    }

    double sr = 44100.0, phase = 0.0;
    std::vector<float> bufL, bufR;
    int writePos = 0, mask = 0;
    float depthSm = -1.0f;      // v1.2 hq depth smoothing (-1 = snap on first block)
};

} // namespace ydc
