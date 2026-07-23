// GLOBUS — stereo delay with tempo sync, slewed time changes (no pitch chaos),
// and a low-pass filter inside the feedback loop.
#pragma once
#include <vector>
#include "../Engine/DspUtils.h"
#include "../Parameters.h"

namespace ydc
{
class DelayFx
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        const int size = juce::nextPowerOfTwo ((int) (2.5 * sampleRate) + 8);
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
        lpL = lpR = 0.0f;
        currentDelay = -1.0f;
    }

    static float syncedTimeSec (int divIndex, double bpm)
    {
        const int idx = juce::jlimit (0, (int) (sizeof (choices::delayDivBeats) / sizeof (float)) - 1, divIndex);
        return (float) (60.0 / std::max (20.0, bpm)) * choices::delayDivBeats[idx];
    }

    void process (float* l, float* r, int n, float timeSec, float feedback, float toneHz, float mix)
    {
        const float wet = clampf (mix, 0.0f, 1.0f);
        const float fb  = clampf (feedback, 0.0f, 0.95f);
        const float c   = 1.0f - std::exp (-kTwoPi * clampf (toneHz, 200.0f, 20000.0f) / (float) sr);
        const float target = clampf (timeSec * (float) sr, 32.0f, (float) mask - 4.0f);
        if (currentDelay < 0.0f)
            currentDelay = target;

        // slew ~30 ms worth of change per second of audio → smooth, subtle pitch glide
        const float slew = 0.0008f;

        for (int i = 0; i < n; ++i)
        {
            currentDelay += (target - currentDelay) * slew;

            const float dl = readInterp (bufL, currentDelay);
            const float dr = readInterp (bufR, currentDelay);

            lpL += c * (dl - lpL);
            lpR += c * (dr - lpR);

            bufL[(size_t) writePos] = sanitize (l[i] + lpL * fb);
            bufR[(size_t) writePos] = sanitize (r[i] + lpR * fb);

            l[i] += lpL * wet;
            r[i] += lpR * wet;

            writePos = (writePos + 1) & mask;
        }
    }

private:
    float readInterp (const std::vector<float>& buf, float delaySamples) const
    {
        const float pos = (float) writePos - delaySamples;
        const int i0 = (int) std::floor (pos);
        const float frac = pos - (float) i0;
        const float a = buf[(size_t) (i0 & mask)];
        const float b = buf[(size_t) ((i0 + 1) & mask)];
        return a + frac * (b - a);
    }

    double sr = 44100.0;
    std::vector<float> bufL, bufR;
    int writePos = 0, mask = 0;
    float lpL = 0.0f, lpR = 0.0f;
    float currentDelay = -1.0f;
};

} // namespace ydc
