#include "EffectsChain.h"

namespace ydc
{
void EffectsChain::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    dist.prepare (sampleRate);
    chorus.prepare (sampleRate, maxBlockSize);
    delay.prepare (sampleRate, maxBlockSize);
    reverb.prepare (sampleRate);
    reverbHq.prepare (sampleRate);    // both reverb engines always prepared up front
    eq.prepare (sampleRate);

    dryScratch.setSize (2, std::max (16, maxBlockSize));
    masterGain.reset (sampleRate, 0.02);
    widthSmoothed.reset (sampleRate, 0.02);
    reset();
}

void EffectsChain::reset()
{
    dist.reset();
    chorus.reset();
    delay.reset();
    reverb.reset();
    reverbHq.reset();
    eq.reset();
    bpDist.prepare();
    bpChorus.prepare();
    bpDelay.prepare();
    bpReverb.prepare();
    bpEq.prepare();
    usingHqReverb = false;
    reverbEngineFade = 1.0f;
}

template <typename ProcessFn, typename ResetFn>
void EffectsChain::runEffect (BypassMixer& bp, bool enabled, juce::AudioBuffer<float>& buffer,
                              int n, float fadePerSample, ProcessFn&& processFx, ResetFn&& resetFx)
{
    const float target = enabled ? 1.0f : 0.0f;

    if (! enabled && bp.mix <= 0.0001f)
    {
        bp.mix = 0.0f;
        bp.wasFullyOff = true;
        return;                                   // fully bypassed: zero cost
    }

    if (enabled && bp.wasFullyOff)
    {
        resetFx();                                // fresh state when re-enabled (no stale tails)
        bp.wasFullyOff = false;
    }

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);

    if (std::abs (bp.mix - target) < 0.0001f && target >= 1.0f)
    {
        processFx (l, r, n);                      // steady active state: no crossfade cost
        return;
    }

    // crossfade: keep dry copy, process, blend with a ramping mix
    dryScratch.copyFrom (0, 0, l, n);
    dryScratch.copyFrom (1, 0, r, n);
    processFx (l, r, n);

    const float* dl = dryScratch.getReadPointer (0);
    const float* dr = dryScratch.getReadPointer (1);
    float m = bp.mix;
    for (int i = 0; i < n; ++i)
    {
        m = clampf (m + (target > m ? fadePerSample : -fadePerSample), 0.0f, 1.0f);
        l[i] = dl[i] + (l[i] - dl[i]) * m;
        r[i] = dr[i] + (r[i] - dr[i]) * m;
    }
    bp.mix = m;
}

void EffectsChain::process (juce::AudioBuffer<float>& buffer, const ParamRefs& params,
                            double bpm, float fxMixScale, float widthMod,
                            QualityMode quality)
{
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2)
        return;

    const float fadePerSample = 1.0f / (0.010f * (float) sr);   // ~10 ms bypass fade
    auto wetScale = [fxMixScale] (float mix) { return clampf (mix * fxMixScale, 0.0f, 1.0f); };

    // v1.2 quality routing — LEGACY keeps every 1.1 code path bit-exact
    const bool hqPaths = quality != QualityMode::Legacy;
    const int  distOs  = quality == QualityMode::High ? 2
                       : quality == QualityMode::Ultra ? 4 : 1;
    const bool wantHqReverb = quality == QualityMode::High || quality == QualityMode::Ultra;

    // ---- Distortion
    runEffect (bpDist, params.distOn->load() > 0.5f, buffer, n, fadePerSample,
        [&] (float* l, float* r, int len)
        { dist.process (l, r, len, params.distDrive->load(), params.distTone->load(),
                        params.distMix->load(), distOs); },
        [&] { dist.reset(); });

    // ---- Chorus
    runEffect (bpChorus, params.chOn->load() > 0.5f, buffer, n, fadePerSample,
        [&] (float* l, float* r, int len)
        { chorus.process (l, r, len, params.chRate->load(), params.chDepth->load(),
                          params.chWidth->load(), wetScale (params.chMix->load()), hqPaths); },
        [&] { chorus.reset(); });

    // ---- Delay
    runEffect (bpDelay, params.dlyOn->load() > 0.5f, buffer, n, fadePerSample,
        [&] (float* l, float* r, int len)
        {
            const float t = params.dlySync->load() > 0.5f
                          ? DelayFx::syncedTimeSec ((int) params.dlyDiv->load(), bpm)
                          : params.dlyTime->load();
            delay.process (l, r, len, t, params.dlyFb->load(), params.dlyTone->load(),
                           wetScale (params.dlyMix->load()), hqPaths);
        },
        [&] { delay.reset(); });

    // ---- Reverb (engine swaps fade the wet back in — click-free quality changes)
    if (wantHqReverb != usingHqReverb)
    {
        usingHqReverb = wantHqReverb;
        reverbEngineFade = 0.0f;
        if (usingHqReverb) reverbHq.reset();
        else               reverb.reset();
    }
    reverbEngineFade = clampf (reverbEngineFade + (float) n / (0.05f * (float) sr), 0.0f, 1.0f);
    runEffect (bpReverb, params.rvOn->load() > 0.5f, buffer, n, fadePerSample,
        [&] (float* l, float* r, int len)
        {
            const float wet = usingHqReverb ? wetScale (params.rvMix->load()) * reverbEngineFade
                                            : wetScale (params.rvMix->load())
                                              * (quality == QualityMode::Legacy ? 1.0f : reverbEngineFade);
            if (usingHqReverb)
                reverbHq.process (l, r, len, params.rvSize->load(), params.rvDamp->load(),
                                  params.rvWidth->load(), wet);
            else
                reverb.process (l, r, len, params.rvSize->load(), params.rvDamp->load(),
                                params.rvWidth->load(), wet);
        },
        [&] { reverb.reset(); reverbHq.reset(); });

    // ---- EQ
    runEffect (bpEq, params.eqOn->load() > 0.5f, buffer, n, fadePerSample,
        [&] (float* l, float* r, int len)
        { eq.process (l, r, len, params.eqLow->load(), params.eqMid->load(), params.eqHigh->load(),
                      hqPaths); },
        [&] { eq.reset(); });

    // ---- stereo width (mid/side), master gain, safety clip
    widthSmoothed.setTargetValue (clampf (params.stereoWidth->load() + widthMod, 0.0f, 2.0f));
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (params.masterLevel->load(), -60.0f));

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float w = widthSmoothed.getNextValue();
        const float g = masterGain.getNextValue();
        const float mid  = (l[i] + r[i]) * 0.5f;
        const float side = (l[i] - r[i]) * 0.5f * w;
        l[i] = softClip (sanitize ((mid + side) * g));
        r[i] = softClip (sanitize ((mid - side) * g));
    }
}

} // namespace ydc
