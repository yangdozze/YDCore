// YD Core — LFO with tempo sync, fade-in, retrigger and S&H. Used per-voice
// (retrigger mode) and as engine-global master (free-running mode).
#pragma once
#include "DspUtils.h"
#include "../Parameters.h"

namespace ydc
{
class Lfo
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        fadeElapsed = 1.0e9f;
    }

    /** Note start (or engine reset). Resets fade; resets phase when retriggering. */
    void trigger (float phase01, bool resetPhase, float fadeSeconds, uint32_t seed) noexcept
    {
        if (resetPhase)
        {
            phase = (double) clampf (phase01, 0.0f, 1.0f);
            rng.seed (seed);
            shValue = rng.bi();
        }
        fadeTime = fadeSeconds;
        fadeElapsed = 0.0f;
    }

    /** Free-running voices adopt the master's phase so all voices stay aligned. */
    void syncPhaseFrom (const Lfo& master, float phaseOffset01) noexcept
    {
        phase = master.phase + (double) phaseOffset01;
        phase -= std::floor (phase);
        shValue = master.shValue;
    }

    /** Advance by n samples at rateHz; returns the *bipolar* raw value (fade applied). */
    float tick (int n, float rateHz, LfoWave wave) noexcept
    {
        const double inc = clampf (rateHz, 0.0f, 100.0f) / sr;
        phase += inc * n;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            shValue = rng.bi();
        }

        fadeElapsed += (float) n / (float) sr;
        const float fade = fadeTime > 0.001f ? clampf (fadeElapsed / fadeTime, 0.0f, 1.0f) : 1.0f;
        return shape (wave, phase, shValue) * fade;
    }

    /** Current value without advancing (already includes fade of last tick). */
    static float shape (LfoWave wave, double ph, float sh) noexcept
    {
        switch (wave)
        {
            case LfoWave::Sine:       return std::sin ((float) ph * kTwoPi);
            case LfoWave::Triangle:   return 1.0f - 2.0f * std::abs (2.0f * (float) ph - 1.0f);
            case LfoWave::Saw:        return 2.0f * (float) ph - 1.0f;
            case LfoWave::Square:     return ph < 0.5 ? 1.0f : -1.0f;
            case LfoWave::SampleHold: return sh;
            default:                  return 0.0f;
        }
    }

    /** Map a bipolar raw value according to the LFO's bipolar/unipolar switch. */
    static float mapPolarity (float raw, bool bipolar) noexcept
    {
        return bipolar ? raw : raw * 0.5f + 0.5f;
    }

    /** Effective rate in Hz given sync settings and host tempo. */
    static float effectiveRate (bool sync, float rateHz, int divIndex, double bpm) noexcept
    {
        if (! sync)
            return rateHz;
        const int idx = std::min (std::max (divIndex, 0), (int) (sizeof (choices::lfoDivBeats) / sizeof (float)) - 1);
        const float beats = choices::lfoDivBeats[idx];
        return (float) (bpm / 60.0) / beats;
    }

    double getPhase() const noexcept { return phase; }

private:
    double sr = 44100.0;
    double phase = 0.0;
    float shValue = 0.0f;
    float fadeTime = 0.0f, fadeElapsed = 1.0e9f;
    Rng rng;
};

} // namespace ydc
