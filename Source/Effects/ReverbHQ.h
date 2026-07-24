// GLOBUS v1.2 — HIGH/ULTRA reverb: an original 8-line feedback delay network.
//
//   input → 2×2 allpass diffusers per channel → 8 mutually-prime delay lines
//   → per-line damping one-poles → Householder feedback (energy-preserving)
//   → ± tap mix-down to stereo. Two lines get a slow ±3-sample sine modulation
//   (fractional Catmull-Rom reads) which keeps the late tail dense without
//   chorusing. Feedback gain tops out at 0.92 — long, never runaway.
//
// LEGACY/ECO quality keeps the 1.1 juce::Reverb path (EffectsChain routes).
// Real-time safe: all buffers preallocated in prepare(), no allocation in
// process(), denormal-proofed via sanitize().
#pragma once
#include <vector>
#include <juce_core/juce_core.h>
#include "../Engine/DspUtils.h"

namespace ydc
{
class ReverbHQ
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double scale = sampleRate / 48000.0;
        static constexpr int base[kLines] = { 1931, 2213, 2503, 2801, 3163, 3457, 3767, 4093 }; // primes
        for (int i = 0; i < kLines; ++i)
        {
            lineLen[i] = std::max (256, (int) (base[i] * scale));
            const int size = juce::nextPowerOfTwo (lineLen[i] + 16);
            lines[(size_t) i].assign ((size_t) size, 0.0f);
            lineMask[i] = size - 1;
        }
        static constexpr int apBase[4] = { 142, 379, 107, 277 };
        for (int i = 0; i < 4; ++i)
        {
            apLen[i] = std::max (32, (int) (apBase[i] * scale));
            const int size = juce::nextPowerOfTwo (apLen[i] + 4);
            ap[(size_t) i].assign ((size_t) size, 0.0f);
            apMask[i] = size - 1;
        }
        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& a : ap)    std::fill (a.begin(), a.end(), 0.0f);
        std::fill (std::begin (damp), std::end (damp), 0.0f);
        std::fill (std::begin (writePos), std::end (writePos), 0);
        std::fill (std::begin (apPos), std::end (apPos), 0);
        modPhase = 0.0;
    }

    void process (float* l, float* r, int n, float size, float dampAmt, float width, float mix)
    {
        const float wet = clampf (mix, 0.0f, 1.0f);
        if (wet <= 0.0001f)
            return;
        const float dryGain = 1.0f - wet * 0.35f;          // matches the legacy wet/dry feel
        const float wetGain = wet * 0.55f;
        const float fb = 0.70f + clampf (size, 0.0f, 1.0f) * 0.22f;   // 0.70 … 0.92
        const float dampC = 0.05f + clampf (dampAmt, 0.0f, 1.0f) * 0.5f;
        const float w = clampf (width, 0.0f, 1.0f);
        const double modInc = 0.35 / sr;                   // ~0.35 Hz tail motion

        for (int i = 0; i < n; ++i)
        {
            // ---- input diffusion (2 allpasses per channel, g = 0.62)
            float inL = 0.5f * (l[i] + r[i] * 0.15f);
            float inR = 0.5f * (r[i] + l[i] * 0.15f);
            inL = allpass (0, inL);
            inL = allpass (1, inL);
            inR = allpass (2, inR);
            inR = allpass (3, inR);

            // ---- read the 8 lines (two with slow fractional modulation)
            modPhase += modInc;
            if (modPhase >= 1.0) modPhase -= 1.0;
            const float m1 = 3.0f * std::sin ((float) modPhase * kTwoPi);
            const float m2 = 3.0f * std::sin ((float) modPhase * kTwoPi + 2.1f);

            float y[kLines];
            for (int k = 0; k < kLines; ++k)
            {
                float delay = (float) lineLen[k];
                if (k == 2) delay += m1;
                if (k == 5) delay += m2;
                y[k] = readLine (k, delay);
                // per-line damping
                damp[k] += dampC * (y[k] - damp[k]);
                y[k] = y[k] + (damp[k] - y[k]) * dampC;    // gentle HF loss
            }

            // ---- Householder feedback: y' = y − (2/N)·Σy  (energy preserving)
            float sum = 0.0f;
            for (float v : y) sum += v;
            const float h = sum * (2.0f / (float) kLines);

            for (int k = 0; k < kLines; ++k)
            {
                const float inject = (k & 1) == 0 ? inL : inR;
                writeLine (k, sanitize (inject + (y[k] - h) * fb));
            }

            // ---- stereo tap mix (± signs decorrelate)
            const float outL = y[0] - y[1] + y[2] - y[3] + y[4] * 0.7f - y[5] * 0.7f;
            const float outR = y[0] + y[1] - y[2] - y[3] + y[6] * 0.7f - y[7] * 0.7f;
            const float mid  = (outL + outR) * 0.5f;
            const float side = (outL - outR) * 0.5f * w;

            l[i] = l[i] * dryGain + (mid + side) * wetGain;
            r[i] = r[i] * dryGain + (mid - side) * wetGain;
        }
    }

private:
    static constexpr int kLines = 8;

    inline float readLine (int k, float delaySamples) noexcept
    {
        auto& buf = lines[(size_t) k];
        const int mask = lineMask[k];
        const float pos = (float) writePos[k] - clampf (delaySamples, 8.0f, (float) mask - 4.0f);
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

    inline void writeLine (int k, float v) noexcept
    {
        lines[(size_t) k][(size_t) writePos[k]] = v;
        writePos[k] = (writePos[k] + 1) & lineMask[k];
    }

    inline float allpass (int k, float x) noexcept
    {
        constexpr float g = 0.62f;
        auto& buf = ap[(size_t) k];
        const int mask = apMask[k];
        const int readPos = (apPos[k] - apLen[k]) & mask;
        const float d = buf[(size_t) readPos];
        const float y = d - g * x;
        buf[(size_t) apPos[k]] = sanitize (x + g * d);
        apPos[k] = (apPos[k] + 1) & mask;
        return y;
    }

    double sr = 48000.0;
    std::vector<float> lines[kLines];
    int lineLen[kLines] {}, lineMask[kLines] {}, writePos[kLines] {};
    float damp[kLines] {};
    std::vector<float> ap[4];
    int apLen[4] {}, apMask[4] {}, apPos[4] {};
    double modPhase = 0.0;
};

} // namespace ydc
