// GLOBUS — parameter contract. Every ID here is stable and host-automatable.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace ydc
{
//==============================================================================
// Choice lists (order is part of the save format — never reorder, only append)
//==============================================================================
namespace choices
{
    inline const juce::StringArray oscWaves      { "Sine", "Triangle", "Saw", "Square", "Pulse", "SuperSaw", "Noise" };
    inline const juce::StringArray subWaves      { "Sine", "Square" };
    inline const juce::StringArray noiseTypes    { "White", "Pink" };
    inline const juce::StringArray filterTypes   { "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "Notch" };
    inline const juce::StringArray lfoWaves      { "Sine", "Triangle", "Saw", "Square", "S&H" };
    inline const juce::StringArray lfoDivisions  { "1/1", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    inline const juce::StringArray delayDivisions{ "1/2", "1/4D", "1/4", "1/4T", "1/8D", "1/8", "1/8T", "1/16" };
    inline const juce::StringArray arpDivisions  { "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    inline const juce::StringArray arpModes      { "Up", "Down", "Up/Down", "Random" };
    inline const juce::StringArray playModes     { "Poly", "Mono", "Legato" };
    inline const juce::StringArray notePriorities{ "Last", "High", "Low" };
    inline const juce::StringArray lfoDests      { "Off", "Pitch 1+2", "Osc 1 Pitch", "Osc 2 Pitch", "Filter Cutoff", "Amp Level", "Pan", "PW 1+2", "FX Mix" };
    inline const juce::StringArray modEnvDests   { "Off", "Pitch 1+2", "Osc 1 Pitch", "Osc 2 Pitch", "Osc 1 PW", "Osc 2 PW", "Filter Cutoff", "LFO 1 Rate", "LFO 2 Rate", "Noise Level" };
    inline const juce::StringArray modSources    { "Off", "Velocity", "Mod Wheel", "Aftertouch", "Key Track", "Amp Env", "Filter Env", "Mod Env", "LFO 1", "LFO 2", "Random", "Pitch Bend" };
    inline const juce::StringArray modDests      { "Off", "Osc 1 Pitch", "Osc 2 Pitch", "Osc 1+2 Pitch", "Osc 1 Fine", "Osc 2 Fine",
                                                   "Osc 1 Level", "Osc 2 Level", "Osc 1 Pan", "Osc 2 Pan", "Osc 1 PW", "Osc 2 PW",
                                                   "Filter Cutoff", "Filter Reso", "Amp Level", "LFO 1 Rate", "LFO 2 Rate", "FX Mix", "Stereo Width" };

    // Beats per step for the tempo-sync division lists above (1 beat = quarter note)
    inline constexpr float lfoDivBeats[]   = { 4.0f, 2.0f, 4.0f/3.0f, 1.0f, 2.0f/3.0f, 0.5f, 1.0f/3.0f, 0.25f, 1.0f/6.0f, 0.125f };
    inline constexpr float delayDivBeats[] = { 2.0f, 1.5f, 1.0f, 2.0f/3.0f, 0.75f, 0.5f, 1.0f/3.0f, 0.25f };
    inline constexpr float arpDivBeats[]   = { 1.0f, 2.0f/3.0f, 0.5f, 1.0f/3.0f, 0.25f, 1.0f/6.0f, 0.125f };
}

// Enum mirrors of the choice lists (indices must match)
enum class OscWave    { Sine, Triangle, Saw, Square, Pulse, SuperSaw, Noise };
enum class FilterType { LP12, LP24, HP12, HP24, BP12, Notch };
enum class LfoWave    { Sine, Triangle, Saw, Square, SampleHold };
enum class PlayMode   { Poly, Mono, Legato };
enum class ArpMode    { Up, Down, UpDown, Random };

enum class ModSource  { Off, Velocity, ModWheel, Aftertouch, KeyTrack, AmpEnv, FilterEnv, ModEnv, Lfo1, Lfo2, Random, PitchBend, Count };
enum class ModDest    { Off, Osc1Pitch, Osc2Pitch, OscAllPitch, Osc1Fine, Osc2Fine, Osc1Level, Osc2Level, Osc1Pan, Osc2Pan,
                        Osc1PW, Osc2PW, Cutoff, Reso, AmpLevel, Lfo1Rate, Lfo2Rate, FxMix, StereoWidth, Count };
enum class LfoDest    { Off, PitchAll, Osc1Pitch, Osc2Pitch, Cutoff, AmpLevel, Pan, PWAll, FxMix };
enum class ModEnvDest { Off, PitchAll, Osc1Pitch, Osc2Pitch, Osc1PW, Osc2PW, Cutoff, Lfo1Rate, Lfo2Rate, NoiseLevel };

//==============================================================================
// Parameter IDs
//==============================================================================
namespace ids
{
    // Oscillators: use osc(n, suffix) → "osc1Wave" etc.
    inline juce::String osc (int oscIndex1Based, const char* suffix) { return "osc" + juce::String(oscIndex1Based) + suffix; }
    inline juce::String lfo (int lfoIndex1Based, const char* suffix) { return "lfo" + juce::String(lfoIndex1Based) + suffix; }
    inline juce::String slot(int slotIndex1Based, const char* suffix){ return "mat" + juce::String(slotIndex1Based) + suffix; }

    // Sub / noise
    inline constexpr const char* subOn      = "subOn";
    inline constexpr const char* subWave    = "subWave";
    inline constexpr const char* subLevel   = "subLevel";
    inline constexpr const char* noiseType  = "noiseType";
    inline constexpr const char* noiseLevel = "noiseLevel";
    inline constexpr const char* noiseTone  = "noiseTone";

    // Filter
    inline constexpr const char* filterOn     = "filterOn";
    inline constexpr const char* filterType   = "filterType";
    inline constexpr const char* cutoff       = "cutoff";
    inline constexpr const char* resonance    = "resonance";
    inline constexpr const char* filterDrive  = "filterDrive";
    inline constexpr const char* keyTrack     = "keyTrack";
    inline constexpr const char* filterEnvAmt = "filterEnvAmt";

    // Envelopes
    inline constexpr const char* ampA = "ampAttack";   inline constexpr const char* ampD = "ampDecay";
    inline constexpr const char* ampS = "ampSustain";  inline constexpr const char* ampR = "ampRelease";
    inline constexpr const char* filA = "filtAttack";  inline constexpr const char* filD = "filtDecay";
    inline constexpr const char* filS = "filtSustain"; inline constexpr const char* filR = "filtRelease";
    inline constexpr const char* modA = "modAttack";   inline constexpr const char* modD = "modDecay";
    inline constexpr const char* modS = "modSustain";  inline constexpr const char* modR = "modRelease";
    inline constexpr const char* modEnvAmt  = "modEnvAmt";
    inline constexpr const char* modEnvDest = "modEnvDest";

    // Play / global
    inline constexpr const char* playMode     = "playMode";
    inline constexpr const char* glideTime    = "glideTime";
    inline constexpr const char* pitchBendRange = "pitchBendRange";
    inline constexpr const char* velAmount    = "velAmount";
    inline constexpr const char* polyLimit    = "polyLimit";
    inline constexpr const char* notePriority = "notePriority";
    inline constexpr const char* stereoWidth  = "stereoWidth";
    inline constexpr const char* masterLevel  = "masterLevel";

    // Arp
    inline constexpr const char* arpOn   = "arpOn";
    inline constexpr const char* arpMode = "arpMode";
    inline constexpr const char* arpDiv  = "arpDiv";
    inline constexpr const char* arpGate = "arpGate";
    inline constexpr const char* arpOct  = "arpOct";
    inline constexpr const char* arpHold = "arpHold";

    // FX
    inline constexpr const char* distOn    = "distOn";
    inline constexpr const char* distDrive = "distDrive";
    inline constexpr const char* distTone  = "distTone";
    inline constexpr const char* distMix   = "distMix";

    inline constexpr const char* chOn    = "chorusOn";
    inline constexpr const char* chRate  = "chorusRate";
    inline constexpr const char* chDepth = "chorusDepth";
    inline constexpr const char* chWidth = "chorusWidth";
    inline constexpr const char* chMix   = "chorusMix";

    inline constexpr const char* dlyOn   = "delayOn";
    inline constexpr const char* dlyTime = "delayTime";
    inline constexpr const char* dlySync = "delaySync";
    inline constexpr const char* dlyDiv  = "delayDiv";
    inline constexpr const char* dlyFb   = "delayFeedback";
    inline constexpr const char* dlyTone = "delayTone";
    inline constexpr const char* dlyMix  = "delayMix";

    inline constexpr const char* rvOn    = "reverbOn";
    inline constexpr const char* rvSize  = "reverbSize";
    inline constexpr const char* rvDamp  = "reverbDamp";
    inline constexpr const char* rvWidth = "reverbWidth";
    inline constexpr const char* rvMix   = "reverbMix";

    inline constexpr const char* eqOn   = "eqOn";
    inline constexpr const char* eqLow  = "eqLow";
    inline constexpr const char* eqMid  = "eqMid";
    inline constexpr const char* eqHigh = "eqHigh";
}

//==============================================================================
// Cached atomic parameter pointers — the audio thread reads only these.
//==============================================================================
struct OscRefs
{
    std::atomic<float> *on, *wave, *oct, *semi, *fine, *level, *pan, *pw,
                       *phase, *randPhase, *uniCount, *uniDetune, *uniSpread, *drift;
};
struct EnvRefs  { std::atomic<float> *a, *d, *s, *r; };
struct LfoRefs  { std::atomic<float> *wave, *rate, *sync, *div, *fade, *phase, *bipolar, *retrig, *dest, *amount; };
struct SlotRefs { std::atomic<float> *src, *dst, *amt, *bipolar; };

struct ParamRefs
{
    explicit ParamRefs (juce::AudioProcessorValueTreeState& s);

    std::array<OscRefs, 2> osc;
    std::atomic<float> *subOn, *subWave, *subLevel;
    std::atomic<float> *noiseType, *noiseLevel, *noiseTone;

    std::atomic<float> *filterOn, *filterType, *cutoff, *resonance, *filterDrive, *keyTrack, *filterEnvAmt;

    EnvRefs amp, fil, mod;
    std::atomic<float> *modEnvAmt, *modEnvDest;

    std::array<LfoRefs, 2>  lfo;
    std::array<SlotRefs, 8> slot;

    std::atomic<float> *playMode, *glideTime, *pitchBendRange, *velAmount, *polyLimit, *notePriority, *stereoWidth, *masterLevel;
    std::atomic<float> *arpOn, *arpMode, *arpDiv, *arpGate, *arpOct, *arpHold;

    std::atomic<float> *distOn, *distDrive, *distTone, *distMix;
    std::atomic<float> *chOn, *chRate, *chDepth, *chWidth, *chMix;
    std::atomic<float> *dlyOn, *dlyTime, *dlySync, *dlyDiv, *dlyFb, *dlyTone, *dlyMix;
    std::atomic<float> *rvOn, *rvSize, *rvDamp, *rvWidth, *rvMix;
    std::atomic<float> *eqOn, *eqLow, *eqMid, *eqHigh;
};

/** Builds the full APVTS layout (all ~145 automatable parameters). */
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Human-readable tooltip for a parameter ID (used by the UI). */
juce::String getTooltip (const juce::String& paramID);

} // namespace ydc
