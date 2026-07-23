// GLOBUS — 8-slot modulation matrix, evaluated per voice at control rate.
#pragma once
#include "DspUtils.h"
#include "../Parameters.h"

namespace ydc
{
/** Snapshot of all modulation source values for one voice at one control tick. */
struct ModSourcesState
{
    float velocity   = 0.0f;  // 0..1
    float modWheel   = 0.0f;  // 0..1
    float aftertouch = 0.0f;  // 0..1
    float keyTrack   = 0.0f;  // -1..1 (centred on middle C)
    float ampEnv     = 0.0f;  // 0..1
    float filEnv     = 0.0f;  // 0..1
    float modEnv     = 0.0f;  // 0..1
    float lfo[2]     = { 0.0f, 0.0f }; // post bipolar/unipolar mapping, post fade
    bool  lfoBipolar[2] = { true, true };
    float random     = 0.0f;  // -1..1, fixed per note
    float pitchBend  = 0.0f;  // -1..1
};

/** Accumulated modulation offsets applied by the voice. */
struct ModValues
{
    float pitchSemis[2] { 0, 0 };   // per-osc pitch offset in semitones
    float fineSemis[2]  { 0, 0 };   // per-osc fine offset in semitones
    float levelAdd[2]   { 0, 0 };
    float panAdd[2]     { 0, 0 };
    float pwAdd[2]      { 0, 0 };
    float cutoffOct     = 0.0f;     // octaves
    float resoAdd       = 0.0f;
    float ampAdd        = 0.0f;     // final amp gain = clamp(1 + ampAdd, 0, 2)
    float lfoRateMul[2] { 1.0f, 1.0f };
    float fxMixAdd      = 0.0f;     // final fx wet scale = clamp(1 + fxMixAdd, 0, 2)
    float widthAdd      = 0.0f;
    float noiseLevelAdd = 0.0f;     // used by the mod envelope's Noise Level destination

    void clear() noexcept { *this = ModValues(); }
};

/** Depth scalings per destination. */
namespace modScale
{
    constexpr float pitchSemis   = 24.0f;
    constexpr float fineSemis    = 1.0f;
    constexpr float cutoffOct    = 5.0f;
    constexpr float pwRange      = 0.45f;
    constexpr float lfoRateOct   = 3.0f;
    constexpr float lfoQuickPitch= 12.0f;
    constexpr float lfoQuickCut  = 4.0f;
}

/** One decoded matrix slot (indices read once per control tick from the atomics). */
struct SlotState
{
    ModSource src = ModSource::Off;
    ModDest   dst = ModDest::Off;
    float amount  = 0.0f;
    bool bipolar  = true;
};

inline bool sourceIsNaturallyBipolar (ModSource s, const ModSourcesState& st)
{
    switch (s)
    {
        case ModSource::KeyTrack:
        case ModSource::Random:
        case ModSource::PitchBend:  return true;
        case ModSource::Lfo1:       return st.lfoBipolar[0];
        case ModSource::Lfo2:       return st.lfoBipolar[1];
        case ModSource::Off:
        case ModSource::Velocity:
        case ModSource::ModWheel:
        case ModSource::Aftertouch:
        case ModSource::AmpEnv:
        case ModSource::FilterEnv:
        case ModSource::ModEnv:
        case ModSource::Count:
        default:                    return false;
    }
}

inline float sourceValue (ModSource s, const ModSourcesState& st)
{
    switch (s)
    {
        case ModSource::Velocity:   return st.velocity;
        case ModSource::ModWheel:   return st.modWheel;
        case ModSource::Aftertouch: return st.aftertouch;
        case ModSource::KeyTrack:   return st.keyTrack;
        case ModSource::AmpEnv:     return st.ampEnv;
        case ModSource::FilterEnv:  return st.filEnv;
        case ModSource::ModEnv:     return st.modEnv;
        case ModSource::Lfo1:       return st.lfo[0];
        case ModSource::Lfo2:       return st.lfo[1];
        case ModSource::Random:     return st.random;
        case ModSource::PitchBend:  return st.pitchBend;
        case ModSource::Off:
        case ModSource::Count:
        default:                    return 0.0f;
    }
}

/** Evaluate all slots into `out`. */
inline void evaluateMatrix (const SlotState* slots, int numSlots, const ModSourcesState& st, ModValues& out)
{
    for (int i = 0; i < numSlots; ++i)
    {
        const auto& sl = slots[i];
        if (sl.src == ModSource::Off || sl.dst == ModDest::Off || exactEq (sl.amount, 0.0f))
            continue;

        float v = sourceValue (sl.src, st);
        const bool natBi = sourceIsNaturallyBipolar (sl.src, st);
        if (sl.bipolar && ! natBi)  v = v * 2.0f - 1.0f;   // stretch 0..1 → ±1
        if (! sl.bipolar && natBi)  v = v * 0.5f + 0.5f;   // fold ±1 → 0..1

        const float a = sl.amount * v;
        switch (sl.dst)
        {
            case ModDest::Osc1Pitch:   out.pitchSemis[0] += a * modScale::pitchSemis; break;
            case ModDest::Osc2Pitch:   out.pitchSemis[1] += a * modScale::pitchSemis; break;
            case ModDest::OscAllPitch: out.pitchSemis[0] += a * modScale::pitchSemis;
                                       out.pitchSemis[1] += a * modScale::pitchSemis; break;
            case ModDest::Osc1Fine:    out.fineSemis[0]  += a * modScale::fineSemis; break;
            case ModDest::Osc2Fine:    out.fineSemis[1]  += a * modScale::fineSemis; break;
            case ModDest::Osc1Level:   out.levelAdd[0]   += a; break;
            case ModDest::Osc2Level:   out.levelAdd[1]   += a; break;
            case ModDest::Osc1Pan:     out.panAdd[0]     += a; break;
            case ModDest::Osc2Pan:     out.panAdd[1]     += a; break;
            case ModDest::Osc1PW:      out.pwAdd[0]      += a * modScale::pwRange; break;
            case ModDest::Osc2PW:      out.pwAdd[1]      += a * modScale::pwRange; break;
            case ModDest::Cutoff:      out.cutoffOct     += a * modScale::cutoffOct; break;
            case ModDest::Reso:        out.resoAdd       += a; break;
            case ModDest::AmpLevel:    out.ampAdd        += a; break;
            case ModDest::Lfo1Rate:    out.lfoRateMul[0] *= std::exp2 (a * modScale::lfoRateOct); break;
            case ModDest::Lfo2Rate:    out.lfoRateMul[1] *= std::exp2 (a * modScale::lfoRateOct); break;
            case ModDest::FxMix:       out.fxMixAdd      += a; break;
            case ModDest::StereoWidth: out.widthAdd      += a; break;
            case ModDest::Off:
            case ModDest::Count:
            default: break;
        }
    }
}

/** LFO quick-assign destinations (in addition to the matrix). */
inline void applyLfoQuickAssign (LfoDest dest, float amount, float lfoValue, ModValues& out)
{
    if (dest == LfoDest::Off || exactEq (amount, 0.0f))
        return;
    const float a = amount * lfoValue;
    switch (dest)
    {
        case LfoDest::PitchAll:  out.pitchSemis[0] += a * modScale::lfoQuickPitch;
                                 out.pitchSemis[1] += a * modScale::lfoQuickPitch; break;
        case LfoDest::Osc1Pitch: out.pitchSemis[0] += a * modScale::lfoQuickPitch; break;
        case LfoDest::Osc2Pitch: out.pitchSemis[1] += a * modScale::lfoQuickPitch; break;
        case LfoDest::Cutoff:    out.cutoffOct += a * modScale::lfoQuickCut; break;
        case LfoDest::AmpLevel:  out.ampAdd += a; break;
        case LfoDest::Pan:       out.panAdd[0] += a; out.panAdd[1] += a; break;
        case LfoDest::PWAll:     out.pwAdd[0] += a * modScale::pwRange;
                                 out.pwAdd[1] += a * modScale::pwRange; break;
        case LfoDest::FxMix:     out.fxMixAdd += a; break;
        case LfoDest::Off:
        default: break;
    }
}

/** Mod envelope destination routing. */
inline void applyModEnv (ModEnvDest dest, float amount, float envValue, ModValues& out)
{
    if (dest == ModEnvDest::Off || exactEq (amount, 0.0f))
        return;
    const float a = amount * envValue;
    switch (dest)
    {
        case ModEnvDest::PitchAll:  out.pitchSemis[0] += a * modScale::pitchSemis;
                                    out.pitchSemis[1] += a * modScale::pitchSemis; break;
        case ModEnvDest::Osc1Pitch: out.pitchSemis[0] += a * modScale::pitchSemis; break;
        case ModEnvDest::Osc2Pitch: out.pitchSemis[1] += a * modScale::pitchSemis; break;
        case ModEnvDest::Osc1PW:    out.pwAdd[0] += a * modScale::pwRange; break;
        case ModEnvDest::Osc2PW:    out.pwAdd[1] += a * modScale::pwRange; break;
        case ModEnvDest::Cutoff:    out.cutoffOct += a * modScale::cutoffOct; break;
        case ModEnvDest::Lfo1Rate:  out.lfoRateMul[0] *= std::exp2 (a * modScale::lfoRateOct); break;
        case ModEnvDest::Lfo2Rate:  out.lfoRateMul[1] *= std::exp2 (a * modScale::lfoRateOct); break;
        case ModEnvDest::NoiseLevel: out.noiseLevelAdd += a; break;
        case ModEnvDest::Off:
        default: break;
    }
}

} // namespace ydc
