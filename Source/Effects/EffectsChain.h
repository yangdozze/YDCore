// YD Core — master FX chain: Distortion → Chorus → Delay → Reverb → EQ,
// then stereo width, master gain and a safety soft-clip.
// Every effect bypasses through a ~10 ms crossfade (no clicks).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Distortion.h"
#include "ChorusFx.h"
#include "DelayFx.h"
#include "ReverbFx.h"
#include "EqFx.h"
#include "../Parameters.h"

namespace ydc
{
class EffectsChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** In-place on the engine output. fxMixScale/widthMod come from the mod matrix. */
    void process (juce::AudioBuffer<float>& buffer, const ParamRefs& params,
                  double bpm, float fxMixScale, float widthMod);

private:
    /** Crossfaded wet/dry wrapper around one effect. */
    struct BypassMixer
    {
        float mix = 0.0f;          // 0 = bypassed, 1 = active
        bool  wasFullyOff = true;
        void prepare() { mix = 0.0f; wasFullyOff = true; }
    };

    template <typename ProcessFn, typename ResetFn>
    void runEffect (BypassMixer& bp, bool enabled, juce::AudioBuffer<float>& buffer,
                    int n, float fadePerSample, ProcessFn&& processFx, ResetFn&& resetFx);

    Distortion dist;
    ChorusFx   chorus;
    DelayFx    delay;
    ReverbFx   reverb;
    EqFx       eq;

    BypassMixer bpDist, bpChorus, bpDelay, bpReverb, bpEq;

    juce::AudioBuffer<float> dryScratch;   // preallocated in prepare()
    juce::SmoothedValue<float> masterGain;
    juce::SmoothedValue<float> widthSmoothed;
    double sr = 44100.0;
};

} // namespace ydc
