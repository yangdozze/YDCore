// YD Core — distortion: tanh waveshaper → one-pole tone LP → dry/wet.
#pragma once
#include "../Engine/DspUtils.h"

namespace ydc
{
class Distortion
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { lpL = lpR = 0.0f; }

    void process (float* l, float* r, int n, float drive01, float toneHz, float mix)
    {
        const float g    = 1.0f + drive01 * drive01 * 30.0f;
        const float comp = 1.0f / std::sqrt (std::max (1.0f, g * 0.6f));
        const float c    = 1.0f - std::exp (-kTwoPi * clampf (toneHz, 200.0f, 20000.0f) / (float) sr);
        const float wet  = clampf (mix, 0.0f, 1.0f);
        const float dry  = 1.0f - wet;

        for (int i = 0; i < n; ++i)
        {
            const float wl = std::tanh (l[i] * g) * comp;
            const float wr = std::tanh (r[i] * g) * comp;
            lpL += c * (wl - lpL);
            lpR += c * (wr - lpR);
            l[i] = l[i] * dry + lpL * wet;
            r[i] = r[i] * dry + lpR * wet;
        }
    }

private:
    double sr = 44100.0;
    float lpL = 0.0f, lpR = 0.0f;
};

} // namespace ydc
