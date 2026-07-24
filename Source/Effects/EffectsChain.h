// GLOBUS — master FX chain: Distortion → Chorus → Delay → Reverb → EQ,
// then stereo width, master gain and a safety soft-clip.
// Every effect bypasses through a ~10 ms crossfade (no clicks).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Distortion.h"
#include "ChorusFx.h"
#include "DelayFx.h"
#include "ReverbFx.h"
#include "ReverbHQ.h"
#include "EqFx.h"
#include "../Parameters.h"

namespace ydc
{
class EffectsChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** In-place on the engine output. fxMixScale/widthMod come from the mod
        matrix. quality routes the v1.2 paths: LEGACY = exact 1.1 processing,
        ECO = better interpolation, HIGH = 2× distortion + FDN reverb,
        ULTRA = 4× distortion + FDN reverb. */
    void process (juce::AudioBuffer<float>& buffer, const ParamRefs& params,
                  double bpm, float fxMixScale, float widthMod,
                  QualityMode quality = QualityMode::Legacy);

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
    ReverbHQ   reverbHq;          // v1.2 HIGH/ULTRA reverb engine
    EqFx       eq;

    BypassMixer bpDist, bpChorus, bpDelay, bpReverb, bpEq;
    bool  usingHqReverb = false;  // engine swap tracking (wet fades back in)
    float reverbEngineFade = 1.0f;

    juce::AudioBuffer<float> dryScratch;   // preallocated in prepare()
    juce::SmoothedValue<float> masterGain;
    juce::SmoothedValue<float> widthSmoothed;
    double sr = 44100.0;
};

} // namespace ydc
