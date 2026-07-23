#include "Parameters.h"

namespace ydc
{
using APF  = juce::AudioParameterFloat;
using APC  = juce::AudioParameterChoice;
using APB  = juce::AudioParameterBool;
using API  = juce::AudioParameterInt;
using Attr = juce::AudioParameterFloatAttributes;

//==============================================================================
// Display helpers
//==============================================================================
static Attr hzAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                            : juce::String (v, 1) + " Hz";
    });
}
static Attr secAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        return v < 1.0f ? juce::String (v * 1000.0f, 0) + " ms"
                        : juce::String (v, 2) + " s";
    });
}
static Attr pctAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        return juce::String (juce::roundToInt (v * 100.0f)) + " %";
    });
}
static Attr dbAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        return juce::String (v, 1) + " dB";
    });
}
static Attr centsAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        return juce::String (juce::roundToInt (v)) + " ct";
    });
}
static Attr panAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        const int p = juce::roundToInt (v * 50.0f);
        return p == 0 ? juce::String ("C") : (p < 0 ? juce::String (-p) + "L" : juce::String (p) + "R");
    });
}
static Attr biPctAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        const int p = juce::roundToInt (v * 100.0f);
        return (p > 0 ? "+" : "") + juce::String (p) + " %";
    });
}

static juce::NormalisableRange<float> freqRange (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}
static juce::NormalisableRange<float> timeRange (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.reserve (160);

    auto pid = [] (const juce::String& id) { return juce::ParameterID { id, 1 }; };

    // ---------------- Oscillators ----------------
    for (int i = 1; i <= 2; ++i)
    {
        const juce::String n = "Osc " + juce::String (i) + " ";
        p.push_back (std::make_unique<APB> (pid (ids::osc (i, "On")),        n + "On", i == 1));
        p.push_back (std::make_unique<APC> (pid (ids::osc (i, "Wave")),      n + "Wave", choices::oscWaves, 2)); // Saw
        p.push_back (std::make_unique<API> (pid (ids::osc (i, "Oct")),       n + "Octave", -3, 3, 0));
        p.push_back (std::make_unique<API> (pid (ids::osc (i, "Semi")),      n + "Semi", -12, 12, 0));
        p.push_back (std::make_unique<APF> (pid (ids::osc (i, "Fine")),      n + "Fine", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, centsAttr()));
        p.push_back (std::make_unique<APF> (pid (ids::osc (i, "Level")),     n + "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.75f, pctAttr()));
        p.push_back (std::make_unique<APF> (pid (ids::osc (i, "Pan")),       n + "Pan", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, panAttr()));
        p.push_back (std::make_unique<APF> (pid (ids::osc (i, "PW")),        n + "Pulse Width", juce::NormalisableRange<float> (0.05f, 0.95f), 0.5f, pctAttr()));
        p.push_back (std::make_unique<APF> (pid (ids::osc (i, "Phase")),     n + "Phase", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pctAttr()));
        p.push_back (std::make_unique<APB> (pid (ids::osc (i, "RandPhase")), n + "Random Phase", true));
        p.push_back (std::make_unique<API> (pid (ids::osc (i, "UniCount")),  n + "Unison", 1, 7, 1));
        p.push_back (std::make_unique<APF> (pid (ids::osc (i, "UniDetune")), n + "Unison Detune", juce::NormalisableRange<float> (0.0f, 100.0f), 18.0f, centsAttr()));
        p.push_back (std::make_unique<APF> (pid (ids::osc (i, "UniSpread")), n + "Unison Spread", juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f, pctAttr()));
        p.push_back (std::make_unique<APF> (pid (ids::osc (i, "Drift")),     n + "Drift", juce::NormalisableRange<float> (0.0f, 1.0f), 0.15f, pctAttr()));
    }

    // ---------------- Sub / Noise ----------------
    p.push_back (std::make_unique<APB> (pid (ids::subOn),      "Sub On", false));
    p.push_back (std::make_unique<APC> (pid (ids::subWave),    "Sub Wave", choices::subWaves, 0));
    p.push_back (std::make_unique<APF> (pid (ids::subLevel),   "Sub Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pctAttr()));
    p.push_back (std::make_unique<APC> (pid (ids::noiseType),  "Noise Type", choices::noiseTypes, 0));
    p.push_back (std::make_unique<APF> (pid (ids::noiseLevel), "Noise Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::noiseTone),  "Noise Tone", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, biPctAttr()));

    // ---------------- Filter ----------------
    p.push_back (std::make_unique<APB> (pid (ids::filterOn),     "Filter On", true));
    p.push_back (std::make_unique<APC> (pid (ids::filterType),   "Filter Type", choices::filterTypes, 1)); // LP24
    p.push_back (std::make_unique<APF> (pid (ids::cutoff),       "Filter Cutoff", freqRange (20.0f, 20000.0f, 1200.0f), 20000.0f, hzAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::resonance),    "Filter Resonance", juce::NormalisableRange<float> (0.0f, 1.0f), 0.15f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::filterDrive),  "Filter Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::keyTrack),     "Filter Key Track", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::filterEnvAmt), "Filter Env Amount", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, biPctAttr()));

    // ---------------- Envelopes ----------------
    auto addEnv = [&] (const char* aId, const char* dId, const char* sId, const char* rId, const juce::String& name,
                       float da, float dd, float ds, float dr)
    {
        p.push_back (std::make_unique<APF> (pid (aId), name + " Attack",  timeRange (0.001f, 10.0f, 0.25f), da, secAttr()));
        p.push_back (std::make_unique<APF> (pid (dId), name + " Decay",   timeRange (0.001f, 10.0f, 0.30f), dd, secAttr()));
        p.push_back (std::make_unique<APF> (pid (sId), name + " Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), ds, pctAttr()));
        p.push_back (std::make_unique<APF> (pid (rId), name + " Release", timeRange (0.002f, 10.0f, 0.35f), dr, secAttr()));
    };
    addEnv (ids::ampA, ids::ampD, ids::ampS, ids::ampR, "Amp",    0.003f, 0.20f, 0.80f, 0.15f);
    addEnv (ids::filA, ids::filD, ids::filS, ids::filR, "Filter", 0.003f, 0.25f, 0.30f, 0.20f);
    addEnv (ids::modA, ids::modD, ids::modS, ids::modR, "Mod",    0.010f, 0.30f, 0.00f, 0.20f);
    p.push_back (std::make_unique<APF> (pid (ids::modEnvAmt),  "Mod Env Amount", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, biPctAttr()));
    p.push_back (std::make_unique<APC> (pid (ids::modEnvDest), "Mod Env Destination", choices::modEnvDests, 0));

    // ---------------- LFOs ----------------
    for (int i = 1; i <= 2; ++i)
    {
        const juce::String n = "LFO " + juce::String (i) + " ";
        p.push_back (std::make_unique<APC> (pid (ids::lfo (i, "Wave")),    n + "Wave", choices::lfoWaves, 0));
        p.push_back (std::make_unique<APF> (pid (ids::lfo (i, "Rate")),    n + "Rate", freqRange (0.01f, 30.0f, 2.0f), i == 1 ? 5.0f : 0.5f, hzAttr()));
        p.push_back (std::make_unique<APB> (pid (ids::lfo (i, "Sync")),    n + "Sync", false));
        p.push_back (std::make_unique<APC> (pid (ids::lfo (i, "Div")),     n + "Division", choices::lfoDivisions, 3)); // 1/4
        p.push_back (std::make_unique<APF> (pid (ids::lfo (i, "Fade")),    n + "Fade In", timeRange (0.0f, 5.0f, 1.0f), 0.0f, secAttr()));
        p.push_back (std::make_unique<APF> (pid (ids::lfo (i, "Phase")),   n + "Phase", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pctAttr()));
        p.push_back (std::make_unique<APB> (pid (ids::lfo (i, "Bipolar")), n + "Bipolar", true));
        p.push_back (std::make_unique<APB> (pid (ids::lfo (i, "Retrig")),  n + "Retrigger", i == 2));
        p.push_back (std::make_unique<APC> (pid (ids::lfo (i, "Dest")),    n + "Destination", choices::lfoDests, 0));
        p.push_back (std::make_unique<APF> (pid (ids::lfo (i, "Amount")),  n + "Amount", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, biPctAttr()));
    }

    // ---------------- Mod matrix ----------------
    for (int s = 1; s <= 8; ++s)
    {
        const juce::String n = "Matrix " + juce::String (s) + " ";
        p.push_back (std::make_unique<APC> (pid (ids::slot (s, "Src")),     n + "Source", choices::modSources, 0));
        p.push_back (std::make_unique<APC> (pid (ids::slot (s, "Dst")),     n + "Destination", choices::modDests, 0));
        p.push_back (std::make_unique<APF> (pid (ids::slot (s, "Amt")),     n + "Amount", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, biPctAttr()));
        p.push_back (std::make_unique<APB> (pid (ids::slot (s, "Bipolar")), n + "Bipolar", true));
    }

    // ---------------- Play / global ----------------
    p.push_back (std::make_unique<APC> (pid (ids::playMode),       "Play Mode", choices::playModes, 0));
    p.push_back (std::make_unique<APF> (pid (ids::glideTime),      "Glide Time", timeRange (0.0f, 2.0f, 0.25f), 0.05f, secAttr()));
    p.push_back (std::make_unique<API> (pid (ids::pitchBendRange), "Pitch Bend Range", 0, 24, 2));
    p.push_back (std::make_unique<APF> (pid (ids::velAmount),      "Velocity Amount", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pctAttr()));
    p.push_back (std::make_unique<API> (pid (ids::polyLimit),      "Polyphony", 1, 32, 16));
    p.push_back (std::make_unique<APC> (pid (ids::notePriority),   "Note Priority", choices::notePriorities, 0));
    p.push_back (std::make_unique<APF> (pid (ids::stereoWidth),    "Stereo Width", juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::masterLevel),    "Master Level", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, dbAttr()));

    // ---------------- Arpeggiator ----------------
    p.push_back (std::make_unique<APB> (pid (ids::arpOn),   "Arp On", false));
    p.push_back (std::make_unique<APC> (pid (ids::arpMode), "Arp Mode", choices::arpModes, 0));
    p.push_back (std::make_unique<APC> (pid (ids::arpDiv),  "Arp Rate", choices::arpDivisions, 4)); // 1/16
    p.push_back (std::make_unique<APF> (pid (ids::arpGate), "Arp Gate", juce::NormalisableRange<float> (0.05f, 1.0f), 0.6f, pctAttr()));
    p.push_back (std::make_unique<API> (pid (ids::arpOct),  "Arp Octaves", 1, 3, 1));
    p.push_back (std::make_unique<APB> (pid (ids::arpHold), "Arp Hold", false));

    // ---------------- Effects ----------------
    p.push_back (std::make_unique<APB> (pid (ids::distOn),    "Dist On", false));
    p.push_back (std::make_unique<APF> (pid (ids::distDrive), "Dist Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::distTone),  "Dist Tone", freqRange (500.0f, 20000.0f, 4000.0f), 8000.0f, hzAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::distMix),   "Dist Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pctAttr()));

    p.push_back (std::make_unique<APB> (pid (ids::chOn),    "Chorus On", false));
    p.push_back (std::make_unique<APF> (pid (ids::chRate),  "Chorus Rate", freqRange (0.05f, 5.0f, 0.8f), 0.9f, hzAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::chDepth), "Chorus Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::chWidth), "Chorus Width", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::chMix),   "Chorus Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pctAttr()));

    p.push_back (std::make_unique<APB> (pid (ids::dlyOn),   "Delay On", false));
    p.push_back (std::make_unique<APF> (pid (ids::dlyTime), "Delay Time", timeRange (0.02f, 2.0f, 0.35f), 0.35f, secAttr()));
    p.push_back (std::make_unique<APB> (pid (ids::dlySync), "Delay Sync", true));
    p.push_back (std::make_unique<APC> (pid (ids::dlyDiv),  "Delay Division", choices::delayDivisions, 4)); // 1/8D
    p.push_back (std::make_unique<APF> (pid (ids::dlyFb),   "Delay Feedback", juce::NormalisableRange<float> (0.0f, 0.9f), 0.35f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::dlyTone), "Delay Tone", freqRange (200.0f, 20000.0f, 3000.0f), 4500.0f, hzAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::dlyMix),  "Delay Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f, pctAttr()));

    p.push_back (std::make_unique<APB> (pid (ids::rvOn),    "Reverb On", false));
    p.push_back (std::make_unique<APF> (pid (ids::rvSize),  "Reverb Size", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::rvDamp),  "Reverb Damp", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::rvWidth), "Reverb Width", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pctAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::rvMix),   "Reverb Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f, pctAttr()));

    p.push_back (std::make_unique<APB> (pid (ids::eqOn),   "EQ On", false));
    p.push_back (std::make_unique<APF> (pid (ids::eqLow),  "EQ Low", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::eqMid),  "EQ Mid", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr()));
    p.push_back (std::make_unique<APF> (pid (ids::eqHigh), "EQ High", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr()));

    return { p.begin(), p.end() };
}

//==============================================================================
ParamRefs::ParamRefs (juce::AudioProcessorValueTreeState& s)
{
    auto rp = [&s] (const juce::String& id)
    {
        auto* ptr = s.getRawParameterValue (id);
        jassert (ptr != nullptr);
        return ptr;
    };

    for (int i = 0; i < 2; ++i)
    {
        const int n = i + 1;
        osc[(size_t) i] = { rp (ids::osc (n, "On")),        rp (ids::osc (n, "Wave")),      rp (ids::osc (n, "Oct")),
                            rp (ids::osc (n, "Semi")),      rp (ids::osc (n, "Fine")),      rp (ids::osc (n, "Level")),
                            rp (ids::osc (n, "Pan")),       rp (ids::osc (n, "PW")),        rp (ids::osc (n, "Phase")),
                            rp (ids::osc (n, "RandPhase")), rp (ids::osc (n, "UniCount")),  rp (ids::osc (n, "UniDetune")),
                            rp (ids::osc (n, "UniSpread")), rp (ids::osc (n, "Drift")) };
    }

    subOn = rp (ids::subOn); subWave = rp (ids::subWave); subLevel = rp (ids::subLevel);
    noiseType = rp (ids::noiseType); noiseLevel = rp (ids::noiseLevel); noiseTone = rp (ids::noiseTone);

    filterOn = rp (ids::filterOn); filterType = rp (ids::filterType); cutoff = rp (ids::cutoff);
    resonance = rp (ids::resonance); filterDrive = rp (ids::filterDrive); keyTrack = rp (ids::keyTrack);
    filterEnvAmt = rp (ids::filterEnvAmt);

    amp = { rp (ids::ampA), rp (ids::ampD), rp (ids::ampS), rp (ids::ampR) };
    fil = { rp (ids::filA), rp (ids::filD), rp (ids::filS), rp (ids::filR) };
    mod = { rp (ids::modA), rp (ids::modD), rp (ids::modS), rp (ids::modR) };
    modEnvAmt = rp (ids::modEnvAmt); modEnvDest = rp (ids::modEnvDest);

    for (int i = 0; i < 2; ++i)
    {
        const int n = i + 1;
        lfo[(size_t) i] = { rp (ids::lfo (n, "Wave")),  rp (ids::lfo (n, "Rate")),    rp (ids::lfo (n, "Sync")),
                            rp (ids::lfo (n, "Div")),   rp (ids::lfo (n, "Fade")),    rp (ids::lfo (n, "Phase")),
                            rp (ids::lfo (n, "Bipolar")), rp (ids::lfo (n, "Retrig")), rp (ids::lfo (n, "Dest")),
                            rp (ids::lfo (n, "Amount")) };
    }

    for (int s2 = 0; s2 < 8; ++s2)
    {
        const int n = s2 + 1;
        slot[(size_t) s2] = { rp (ids::slot (n, "Src")), rp (ids::slot (n, "Dst")),
                              rp (ids::slot (n, "Amt")), rp (ids::slot (n, "Bipolar")) };
    }

    playMode = rp (ids::playMode); glideTime = rp (ids::glideTime); pitchBendRange = rp (ids::pitchBendRange);
    velAmount = rp (ids::velAmount); polyLimit = rp (ids::polyLimit); notePriority = rp (ids::notePriority);
    stereoWidth = rp (ids::stereoWidth); masterLevel = rp (ids::masterLevel);

    arpOn = rp (ids::arpOn); arpMode = rp (ids::arpMode); arpDiv = rp (ids::arpDiv);
    arpGate = rp (ids::arpGate); arpOct = rp (ids::arpOct); arpHold = rp (ids::arpHold);

    distOn = rp (ids::distOn); distDrive = rp (ids::distDrive); distTone = rp (ids::distTone); distMix = rp (ids::distMix);
    chOn = rp (ids::chOn); chRate = rp (ids::chRate); chDepth = rp (ids::chDepth); chWidth = rp (ids::chWidth); chMix = rp (ids::chMix);
    dlyOn = rp (ids::dlyOn); dlyTime = rp (ids::dlyTime); dlySync = rp (ids::dlySync); dlyDiv = rp (ids::dlyDiv);
    dlyFb = rp (ids::dlyFb); dlyTone = rp (ids::dlyTone); dlyMix = rp (ids::dlyMix);
    rvOn = rp (ids::rvOn); rvSize = rp (ids::rvSize); rvDamp = rp (ids::rvDamp); rvWidth = rp (ids::rvWidth); rvMix = rp (ids::rvMix);
    eqOn = rp (ids::eqOn); eqLow = rp (ids::eqLow); eqMid = rp (ids::eqMid); eqHigh = rp (ids::eqHigh);
}

//==============================================================================
juce::String getTooltip (const juce::String& id)
{
    static const std::map<juce::String, juce::String> tips = []
    {
        std::map<juce::String, juce::String> m;
        for (int i = 1; i <= 2; ++i)
        {
            const juce::String o = "Oscillator " + juce::String (i);
            m[ids::osc (i, "On")]        = o + ": enable/disable.";
            m[ids::osc (i, "Wave")]      = o + ": waveform. SuperSaw stacks 7 detuned saws.";
            m[ids::osc (i, "Oct")]       = o + ": octave transpose (-3..+3).";
            m[ids::osc (i, "Semi")]      = o + ": semitone transpose (-12..+12).";
            m[ids::osc (i, "Fine")]      = o + ": fine tune in cents (-100..+100).";
            m[ids::osc (i, "Level")]     = o + ": output level into the mixer.";
            m[ids::osc (i, "Pan")]       = o + ": stereo position.";
            m[ids::osc (i, "PW")]        = o + ": pulse width (Pulse wave only).";
            m[ids::osc (i, "Phase")]     = o + ": start phase when Random Phase is off.";
            m[ids::osc (i, "RandPhase")] = o + ": randomize phase at note start for a natural free-running feel.";
            m[ids::osc (i, "UniCount")]  = o + ": number of unison voices (1-7).";
            m[ids::osc (i, "UniDetune")] = o + ": unison detune spread in cents.";
            m[ids::osc (i, "UniSpread")] = o + ": unison stereo spread.";
            m[ids::osc (i, "Drift")]     = o + ": slow analog-style pitch drift.";
        }
        m[ids::subOn]      = "Sub oscillator: one octave below oscillator 1.";
        m[ids::subWave]    = "Sub oscillator waveform: sine or square.";
        m[ids::subLevel]   = "Sub oscillator level.";
        m[ids::noiseType]  = "Noise color: white (bright) or pink (warm).";
        m[ids::noiseLevel] = "Noise generator level.";
        m[ids::noiseTone]  = "Noise tone: negative = darker, positive = brighter.";

        m[ids::filterOn]     = "Enable the voice filter.";
        m[ids::filterType]   = "Filter mode: low-pass, high-pass, band-pass or notch (12/24 dB).";
        m[ids::cutoff]       = "Filter cutoff frequency.";
        m[ids::resonance]    = "Filter resonance. High values approach self-oscillation, kept stable.";
        m[ids::filterDrive]  = "Saturation drive into the filter.";
        m[ids::keyTrack]     = "How much cutoff follows the played key.";
        m[ids::filterEnvAmt] = "Filter envelope depth (bipolar, up to ±5 octaves).";

        auto env = [&m] (const char* a, const char* d, const char* s, const char* r, const juce::String& n)
        {
            m[a] = n + " envelope attack time.";
            m[d] = n + " envelope decay time.";
            m[s] = n + " envelope sustain level.";
            m[r] = n + " envelope release time.";
        };
        env (ids::ampA, ids::ampD, ids::ampS, ids::ampR, "Amp");
        env (ids::filA, ids::filD, ids::filS, ids::filR, "Filter");
        env (ids::modA, ids::modD, ids::modS, ids::modR, "Mod");
        m[ids::modEnvAmt]  = "Mod envelope depth (bipolar).";
        m[ids::modEnvDest] = "Where the mod envelope is applied.";

        for (int i = 1; i <= 2; ++i)
        {
            const juce::String l = "LFO " + juce::String (i);
            m[ids::lfo (i, "Wave")]    = l + ": waveform (S&H = stepped random).";
            m[ids::lfo (i, "Rate")]    = l + ": speed in Hz (when not synced).";
            m[ids::lfo (i, "Sync")]    = l + ": lock the rate to host tempo.";
            m[ids::lfo (i, "Div")]     = l + ": musical division when synced.";
            m[ids::lfo (i, "Fade")]    = l + ": fade-in time after note start.";
            m[ids::lfo (i, "Phase")]   = l + ": start phase offset.";
            m[ids::lfo (i, "Bipolar")] = l + ": bipolar (±) or unipolar (0..1) output.";
            m[ids::lfo (i, "Retrig")]  = l + ": restart phase on every note.";
            m[ids::lfo (i, "Dest")]    = l + ": quick-assign destination.";
            m[ids::lfo (i, "Amount")]  = l + ": quick-assign depth.";
        }

        for (int s = 1; s <= 8; ++s)
        {
            const juce::String n = "Matrix slot " + juce::String (s);
            m[ids::slot (s, "Src")]     = n + ": modulation source.";
            m[ids::slot (s, "Dst")]     = n + ": modulation target.";
            m[ids::slot (s, "Amt")]     = n + ": modulation depth (bipolar).";
            m[ids::slot (s, "Bipolar")] = n + ": treat the source as bipolar (±) or unipolar (0..1).";
        }

        m[ids::playMode]       = "Poly, Mono (retrigger) or Legato (no retrigger on overlapping notes).";
        m[ids::glideTime]      = "Portamento time between notes in Mono/Legato.";
        m[ids::pitchBendRange] = "Pitch bend range in semitones.";
        m[ids::velAmount]      = "How strongly velocity drives loudness.";
        m[ids::polyLimit]      = "Maximum simultaneous voices (voice stealing above).";
        m[ids::notePriority]   = "Which note wins in Mono/Legato: last, highest or lowest.";
        m[ids::stereoWidth]    = "Master stereo width (0 = mono, 200% = extra wide).";
        m[ids::masterLevel]    = "Master output level.";

        m[ids::arpOn]   = "Enable the arpeggiator.";
        m[ids::arpMode] = "Arpeggio direction pattern.";
        m[ids::arpDiv]  = "Arpeggiator step rate (tempo-synced).";
        m[ids::arpGate] = "Note length as a fraction of the step.";
        m[ids::arpOct]  = "Octave range the pattern climbs through.";
        m[ids::arpHold] = "Latch: keep arpeggiating after keys are released.";

        m[ids::distOn]    = "Enable distortion.";
        m[ids::distDrive] = "Distortion drive amount.";
        m[ids::distTone]  = "Distortion tone: low-pass after the shaper.";
        m[ids::distMix]   = "Distortion dry/wet.";
        m[ids::chOn]      = "Enable chorus.";
        m[ids::chRate]    = "Chorus modulation rate.";
        m[ids::chDepth]   = "Chorus modulation depth.";
        m[ids::chWidth]   = "Chorus stereo width.";
        m[ids::chMix]     = "Chorus dry/wet.";
        m[ids::dlyOn]     = "Enable delay.";
        m[ids::dlyTime]   = "Delay time (free-run, when Sync is off).";
        m[ids::dlySync]   = "Sync delay time to host tempo.";
        m[ids::dlyDiv]    = "Delay division when synced (D = dotted, T = triplet).";
        m[ids::dlyFb]     = "Delay feedback amount.";
        m[ids::dlyTone]   = "Low-pass filter inside the feedback loop.";
        m[ids::dlyMix]    = "Delay dry/wet.";
        m[ids::rvOn]      = "Enable reverb.";
        m[ids::rvSize]    = "Reverb room size.";
        m[ids::rvDamp]    = "Reverb high-frequency damping.";
        m[ids::rvWidth]   = "Reverb stereo width.";
        m[ids::rvMix]     = "Reverb dry/wet.";
        m[ids::eqOn]      = "Enable the 3-band output EQ.";
        m[ids::eqLow]     = "Low shelf gain (120 Hz).";
        m[ids::eqMid]     = "Mid peak gain (1 kHz).";
        m[ids::eqHigh]    = "High shelf gain (8 kHz).";
        return m;
    }();

    auto it = tips.find (id);
    return it != tips.end() ? it->second : juce::String();
}

} // namespace ydc
