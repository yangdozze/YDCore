// YD Core — reverb (JUCE algorithmic reverb wrapped with our parameter set).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Engine/DspUtils.h"

namespace ydc
{
class ReverbFx
{
public:
    void prepare (double sampleRate)
    {
        reverb.setSampleRate (sampleRate);
        reset();
    }

    void reset() { reverb.reset(); }

    void process (float* l, float* r, int n, float size, float damp, float width, float mix)
    {
        const float wet = clampf (mix, 0.0f, 1.0f);
        juce::Reverb::Parameters p;
        p.roomSize   = clampf (size, 0.0f, 1.0f);
        p.damping    = clampf (damp, 0.0f, 1.0f);
        p.width      = clampf (width, 0.0f, 1.0f);
        p.wetLevel   = wet * 0.9f;
        p.dryLevel   = 1.0f - wet * 0.35f;   // gentle equal-loudness feel
        p.freezeMode = 0.0f;
        reverb.setParameters (p);
        reverb.processStereo (l, r, n);
    }

private:
    juce::Reverb reverb;
};

} // namespace ydc
