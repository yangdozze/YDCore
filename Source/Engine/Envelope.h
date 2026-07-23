// GLOBUS — analog-style exponential ADSR. Click-free: attack resumes from the
// current level, voice stealing uses a ~3 ms fast release.
#pragma once
#include "DspUtils.h"

namespace ydc
{
class AdsrEnv
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release, FastRelease };

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        fastCoef = coefFor (0.0012f);   // ~3 ms to silence (voice stealing)
        reset();
    }

    void reset() noexcept
    {
        stage = Stage::Idle;
        level = 0.0f;
    }

    /** Update times once per control block (cheap: exp only when a value changed). */
    void setParams (float a, float d, float s, float r) noexcept
    {
        // decay/release knobs describe perceived time-to-target, not the raw
        // one-pole tau (which would take ~7x longer to reach silence)
        if (! exactEq (a, atkTime)) { atkTime = a; atkCoef = coefFor (std::max (0.0005f, a)); }
        if (! exactEq (d, decTime)) { decTime = d; decCoef = coefFor (std::max (0.001f, d) * (1.0f / 3.0f)); }
        sustain = clampf (s, 0.0f, 1.0f);
        if (! exactEq (r, relTime)) { relTime = r; relCoef = coefFor (std::max (0.002f, r) * 0.25f); }
    }

    void noteOn() noexcept  { stage = Stage::Attack; }                    // continues from current level → no click
    void noteOff() noexcept { if (stage != Stage::Idle) stage = Stage::Release; }
    void fastRelease() noexcept { if (stage != Stage::Idle) stage = Stage::FastRelease; }

    bool isActive() const noexcept   { return stage != Stage::Idle; }
    bool isReleasing() const noexcept{ return stage == Stage::Release || stage == Stage::FastRelease; }
    float getLevel() const noexcept  { return level; }

    inline float process() noexcept
    {
        switch (stage)
        {
            case Stage::Attack:
                level += atkCoef * (1.28f - level);       // overshoot target → analog curve
                if (level >= 1.0f) { level = 1.0f; stage = Stage::Decay; }
                break;
            case Stage::Decay:
                level += decCoef * (sustain - level);
                if (std::abs (level - sustain) < 1.0e-4f) stage = Stage::Sustain;
                break;
            case Stage::Sustain:
                level += decCoef * (sustain - level);      // follows sustain knob smoothly
                break;
            case Stage::Release:
                level += relCoef * (0.0f - level);
                if (level < 2.0e-5f) { level = 0.0f; stage = Stage::Idle; }
                break;
            case Stage::FastRelease:
                level += fastCoef * (0.0f - level);
                if (level < 2.0e-5f) { level = 0.0f; stage = Stage::Idle; }
                break;
            case Stage::Idle:
            default:
                return 0.0f;
        }
        return level;
    }

private:
    float coefFor (float timeSec) const noexcept
    {
        return 1.0f - std::exp (-1.0f / (timeSec * (float) sr));
    }

    double sr = 44100.0;
    Stage stage = Stage::Idle;
    float level = 0.0f, sustain = 0.8f;
    float atkTime = -1.0f, decTime = -1.0f, relTime = -1.0f;
    float atkCoef = 0.01f, decCoef = 0.001f, relCoef = 0.001f, fastCoef = 0.01f;
};

} // namespace ydc
