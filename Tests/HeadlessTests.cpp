// GLOBUS — headless verification suite. Renders the real processor offline:
// no audio device, no window. Exit code 0 = all checks passed.
#include <juce_events/juce_events.h>
#include <juce_dsp/juce_dsp.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Engine/WavetableImport.h"

namespace
{
int failures = 0;
int checks = 0;

void expect (bool condition, const juce::String& what)
{
    ++checks;
    if (! condition)
    {
        ++failures;
        std::cout << "[FAIL] " << what << std::endl;
    }
}

struct BufferStats
{
    float peak = 0.0f, rms = 0.0f, maxJump = 0.0f;
    bool finite = true, hasDenormals = false;
};

BufferStats analyse (const juce::AudioBuffer<float>& buf, float prevL = 0.0f, float prevR = 0.0f)
{
    BufferStats st;
    double sumSq = 0.0;
    float lastCh[2] = { prevL, prevR };
    for (int c = 0; c < buf.getNumChannels(); ++c)
    {
        const float* d = buf.getReadPointer (c);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const float v = d[i];
            if (! std::isfinite (v)) st.finite = false;
            const float a = std::abs (v);
            if (a > 0.0f && a < 1.0e-25f) st.hasDenormals = true;
            st.peak = std::max (st.peak, a);
            sumSq += (double) v * v;
            st.maxJump = std::max (st.maxJump, std::abs (v - lastCh[c]));
            lastCh[c] = v;
        }
    }
    st.rms = (float) std::sqrt (sumSq / std::max (1, buf.getNumSamples() * buf.getNumChannels()));
    return st;
}

/** Render helper: n samples in blocks of `blockSize` with optional MIDI at offsets. */
BufferStats renderBlocks (YDCoreAudioProcessor& proc, int totalSamples, int blockSize,
                          std::function<void (juce::MidiBuffer&, int blockStart, int blockLen)> midiFn = nullptr)
{
    juce::AudioBuffer<float> block (2, blockSize);
    juce::AudioBuffer<float> all (2, totalSamples);
    all.clear();
    int done = 0;
    while (done < totalSamples)
    {
        const int n = std::min (blockSize, totalSamples - done);
        block.setSize (2, n, false, false, true);
        block.clear();
        juce::MidiBuffer midi;
        if (midiFn) midiFn (midi, done, n);
        proc.processBlock (block, midi);
        for (int c = 0; c < 2; ++c)
            all.copyFrom (c, done, block, c, 0, n);
        done += n;
    }
    return analyse (all);
}

void loadPresetByName (YDCoreAudioProcessor& proc, const juce::String& name)
{
    auto& pm = proc.getPresetManager();
    for (int i = 0; i < pm.getNumPresets(); ++i)
        if (pm.getPresets()[(size_t) i].name == name)
        {
            pm.loadPresetAt (i);
            return;
        }
    expect (false, "preset not found: " + name);
}

//==============================================================================
void testBasicTone (double sr, int blockSize)
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (sr, blockSize);
    proc.getPresetManager().initPatch();

    // note on at t=0, off after 0.5 s, tail 0.5 s
    const int onLen  = (int) (0.5 * sr);
    const int total  = (int) (1.0 * sr);
    auto stats = renderBlocks (proc, total, blockSize,
        [&] (juce::MidiBuffer& midi, int start, int len)
        {
            if (start == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            if (onLen >= start && onLen < start + len)
                midi.addEvent (juce::MidiMessage::noteOff (1, 60), onLen - start);
        });

    const juce::String tag = " @" + juce::String (sr, 0) + "/" + juce::String (blockSize);
    expect (stats.finite, "output is finite" + tag);
    expect (! stats.hasDenormals, "no denormals in output" + tag);
    expect (stats.rms > 0.005f, "note produces audible signal" + tag + " rms=" + juce::String (stats.rms, 6));
    expect (stats.peak < 1.5f, "output within safe range" + tag + " peak=" + juce::String (stats.peak, 3));

    // silence after release
    auto tail = renderBlocks (proc, (int) (0.5 * sr), blockSize);
    expect (tail.peak < 1.0e-3f, "voice fully releases to silence" + tag + " peak=" + juce::String (tail.peak, 6));
}

void testClickFreeEdges (double sr)
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (sr, 256);
    proc.getPresetManager().initPatch();

    auto stats = renderBlocks (proc, (int) sr, 256,
        [&] (juce::MidiBuffer& midi, int start, int len)
        {
            // rapid on/off pairs every ~50 ms
            for (int t = 0; t < len; ++t)
            {
                const int abs = start + t;
                if (abs % 4410 == 0)        midi.addEvent (juce::MidiMessage::noteOn (1, 48 + (abs / 4410) % 24, 0.9f), t);
                if (abs % 4410 == 2205)     midi.addEvent (juce::MidiMessage::noteOff (1, 48 + (abs / 4410) % 24), t);
            }
        });
    expect (stats.finite, "rapid notes: finite output");
    // envelope-limited discontinuity: generous bound (waveform slope at full level can be steep)
    expect (stats.maxJump < 0.9f, "rapid notes: no hard discontinuities, maxJump=" + juce::String (stats.maxJump, 4));
}

void testAllPresetsLoadAndSound()
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    auto& pm = proc.getPresetManager();

    expect (pm.getNumPresets() >= 40, "at least 40 factory presets, found " + juce::String (pm.getNumPresets()));

    juce::StringArray categoriesSeen;
    int audible = 0;
    for (int i = 0; i < pm.getNumPresets(); ++i)
    {
        const auto info = pm.getPresets()[(size_t) i];
        expect (pm.loadPresetAt (i), "preset loads: " + info.name);
        categoriesSeen.addIfNotAlreadyThere (info.category);
        if (info.isFactory)
        {
            expect (info.author == "Ninth Parallel Audio", "factory preset author is branded: " + info.name);
            expect (info.description.isNotEmpty(), "factory preset has a description: " + info.name);
        }

        auto stats = renderBlocks (proc, 24000, 512,
            [&] (juce::MidiBuffer& midi, int start, int)
            {
                if (start == 0)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0);
                    midi.addEvent (juce::MidiMessage::noteOn (1, 62, 0.9f), 0);
                }
            });
        expect (stats.finite, "preset renders finite: " + info.name);
        if (stats.rms > 0.001f)
            ++audible;
        else
            std::cout << "[WARN] quiet preset (0.5 s window): " << info.name << " rms=" << stats.rms << std::endl;

        // release everything before the next preset
        renderBlocks (proc, 4096, 512, [] (juce::MidiBuffer& midi, int start, int)
        {
            if (start == 0) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        });
    }
    expect (audible >= 40, "at least 40 presets audible within 0.5 s, got " + juce::String (audible));
    expect (categoriesSeen.size() >= 10, "10+ categories present, got " + juce::String (categoriesSeen.size()));
}

void testStateRoundTrip()
{
    YDCoreAudioProcessor procA;
    procA.prepareToPlay (44100.0, 512);
    loadPresetByName (procA, "Neon Bite");

    // tweak a couple of parameters on top of the preset
    auto& apvts = procA.getApvts();
    apvts.getParameter ("cutoff")->setValueNotifyingHost (0.42f);
    apvts.getParameter ("reverbMix")->setValueNotifyingHost (0.77f);

    juce::MemoryBlock state;
    procA.getStateInformation (state);

    YDCoreAudioProcessor procB;
    procB.prepareToPlay (44100.0, 512);
    procB.setStateInformation (state.getData(), (int) state.getSize());

    for (auto* p : procA.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr) continue;
        auto* rpB = procB.getApvts().getParameter (rp->paramID);
        expect (rpB != nullptr && std::abs (rpB->getValue() - rp->getValue()) < 1.0e-5f,
                "state restores param " + rp->paramID);
        if (failures > 20) return; // avoid noise explosion
    }
    expect (procB.getPresetManager().getCurrentName() == "Neon Bite", "preset name restored with state");
}

void testRapidPresetSwitchDuringPlayback()
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    auto& pm = proc.getPresetManager();

    auto stats = renderBlocks (proc, 48000 * 2, 256,
        [&] (juce::MidiBuffer& midi, int start, int)
        {
            if (start == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 45, 0.9f), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);
            }
            // switch preset every ~6 blocks while audio is running
            const int block = start / 256;
            if (block % 6 == 0)
                pm.loadPresetAt (block / 6 % pm.getNumPresets());
        });
    expect (stats.finite, "rapid preset switching: finite output");
    expect (stats.peak < 2.0f, "rapid preset switching: bounded output");
}

void testPolyAndMonoModes()
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);
    proc.getPresetManager().initPatch();

    // poly chord
    auto poly = renderBlocks (proc, 22050, 512, [] (juce::MidiBuffer& midi, int start, int)
    {
        if (start == 0)
            for (int n : { 48, 52, 55, 59, 62, 65, 69, 72 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
    });
    expect (poly.rms > 0.01f, "poly chord sounds");

    // release the chord fully before the mono check
    renderBlocks (proc, 44100, 512, [] (juce::MidiBuffer& midi, int start, int)
    {
        if (start == 0) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    });

    // mono mode: two overlapping notes → single voice
    proc.getPresetManager().initPatch();
    proc.getApvts().getParameter ("playMode")->setValueNotifyingHost (
        proc.getApvts().getParameter ("playMode")->getNormalisableRange().convertTo0to1 (1.0f));
    auto mono = renderBlocks (proc, 22050, 512, [] (juce::MidiBuffer& midi, int start, int)
    {
        if (start == 0)   midi.addEvent (juce::MidiMessage::noteOn (1, 40, 0.9f), 0);
        if (start == 5120) midi.addEvent (juce::MidiMessage::noteOn (1, 52, 0.9f), 0);
    });
    expect (mono.rms > 0.01f, "mono mode sounds");
    expect (proc.getEngine().getActiveVoiceCount() <= 1, "mono mode uses a single voice, got "
            + juce::String (proc.getEngine().getActiveVoiceCount()));
}

void testVoiceStress()
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (48000.0, 128);
    proc.getPresetManager().initPatch();

    juce::Random rnd (1234);
    auto stats = renderBlocks (proc, 48000 * 2, 128,
        [&] (juce::MidiBuffer& midi, int, int len)
        {
            for (int i = 0; i < 3; ++i)
            {
                const int pos = rnd.nextInt (len);
                if (rnd.nextBool())
                    midi.addEvent (juce::MidiMessage::noteOn (1, 30 + rnd.nextInt (60), 0.5f + 0.5f * rnd.nextFloat()), pos);
                else
                    midi.addEvent (juce::MidiMessage::noteOff (1, 30 + rnd.nextInt (60)), pos);
            }
        });
    expect (stats.finite, "voice stress (hundreds of random notes): finite");
    expect (stats.peak < 2.0f, "voice stress: bounded, peak=" + juce::String (stats.peak, 3));
    expect (proc.getEngine().getActiveVoiceCount() <= 32, "voice count within limit");
}

void testAutomationSweep()
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (44100.0, 256);
    proc.getPresetManager().initPatch();
    auto* cutoff = proc.getApvts().getParameter ("cutoff");
    auto* reso   = proc.getApvts().getParameter ("resonance");

    auto stats = renderBlocks (proc, 44100 * 2, 256,
        [&] (juce::MidiBuffer& midi, int start, int)
        {
            if (start == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0);
            const float ph = (float) start / 88200.0f;
            cutoff->setValueNotifyingHost (0.5f + 0.5f * std::sin (ph * 25.0f));
            reso->setValueNotifyingHost (0.5f + 0.45f * std::sin (ph * 13.0f));
        });
    expect (stats.finite, "cutoff/resonance automation sweep: finite");
    expect (stats.peak < 2.0f, "automation sweep: bounded, peak=" + juce::String (stats.peak, 3));
}

void testArpeggiator()
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);
    proc.getPresetManager().initPatch();
    auto& apvts = proc.getApvts();
    apvts.getParameter ("arpOn")->setValueNotifyingHost (1.0f);
    apvts.getParameter ("ampSustain")->setValueNotifyingHost (1.0f);

    auto stats = renderBlocks (proc, 44100 * 2, 512,
        [] (juce::MidiBuffer& midi, int start, int)
        {
            if (start == 0)
                for (int n : { 48, 52, 55 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.9f), 0);
        });
    expect (stats.finite, "arpeggiator: finite output");
    expect (stats.rms > 0.005f, "arpeggiator produces sound, rms=" + juce::String (stats.rms, 5));
}

//==============================================================================
// Polyphony regression suite (Task: overlapping notes must sound immediately)
//==============================================================================

/** Goertzel magnitude at `hz` over the mono sum of a buffer region. */
float pitchEnergy (const juce::AudioBuffer<float>& buf, double sr, float hz, int start, int len)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / sr;
    const double k = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    const float* l = buf.getReadPointer (0);
    const float* r = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : l;
    len = std::min (len, buf.getNumSamples() - start);
    for (int i = start; i < start + len; ++i)
    {
        const double x = 0.5 * ((double) l[i] + (double) r[i]);
        const double s0 = x + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (float) (std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - k * s1 * s2)) / std::max (1, len));
}

float midiHz (int note) { return 440.0f * std::exp2 ((note - 69) / 12.0f); }

/** Renders a MIDI scenario and returns the full output. */
juce::AudioBuffer<float> renderScenario (YDCoreAudioProcessor& proc, int totalSamples, int blockSize,
                                         const std::vector<std::tuple<int,int,bool>>& events) // sample, note, isOn
{
    juce::AudioBuffer<float> all (2, totalSamples);
    all.clear();
    juce::AudioBuffer<float> block (2, blockSize);
    int done = 0;
    while (done < totalSamples)
    {
        const int n = std::min (blockSize, totalSamples - done);
        block.setSize (2, n, false, false, true);
        block.clear();
        juce::MidiBuffer midi;
        for (auto& [pos, note, isOn] : events)
            if (pos >= done && pos < done + n)
                midi.addEvent (isOn ? juce::MidiMessage::noteOn (1, note, 0.9f)
                                    : juce::MidiMessage::noteOff (1, note), pos - done);
        proc.processBlock (block, midi);
        for (int c = 0; c < 2; ++c)
            all.copyFrom (c, done, block, c, 0, n);
        done += n;
    }
    return all;
}

void preparePolyInit (YDCoreAudioProcessor& proc, double sr, int blockSize)
{
    proc.prepareToPlay (sr, blockSize);
    proc.getPresetManager().initPatch();
    auto set = [&] (const char* id, float norm) { proc.getApvts().getParameter (id)->setValueNotifyingHost (norm); };
    set ("arpOn", 0.0f);
    set ("reverbOn", 0.0f);
    set ("delayOn", 0.0f);
    set ("chorusOn", 0.0f);
    set ("glideTime", 0.0f);
    set ("ampSustain", 1.0f);   // steady tone for energy analysis
}

void testPolyOverlapBaseline (double sr, int blockSize)
{
    YDCoreAudioProcessor proc;
    preparePolyInit (proc, sr, blockSize);
    const juce::String tag = " @" + juce::String (sr, 0) + "/" + juce::String (blockSize);

    const int q = (int) (0.4 * sr); // quarter segment
    // 60 on at 0 | 64 on at q | 60 off at 2q | 64 off at 3q | tail to 4q
    auto out = renderScenario (proc, 4 * q, blockSize,
        { { 0, 60, true }, { q, 64, true }, { 2 * q, 60, false }, { 3 * q, 64, false } });

    const int w = q / 2;
    const float e60_a = pitchEnergy (out, sr, midiHz (60), q / 4, w);              // only 60 held
    const float e60_b = pitchEnergy (out, sr, midiHz (60), q + q / 4, w);          // both held
    const float e64_b = pitchEnergy (out, sr, midiHz (64), q + q / 4, w);          // both held
    const float e64_c = pitchEnergy (out, sr, midiHz (64), 2 * q + q / 4, w);      // only 64 held
    const float e60_c = pitchEnergy (out, sr, midiHz (60), 2 * q + q / 3, w);      // 60 released

    expect (e60_a > 1.0e-4f, "poly: first note sounds" + tag);
    expect (e64_b > 1.0e-4f, "poly: second note sounds immediately while first is held" + tag
            + " e=" + juce::String (e64_b, 7));
    expect (e60_b > 1.0e-4f, "poly: first note keeps sounding under overlap" + tag);
    expect (e64_c > 1.0e-4f, "poly: releasing the first note does not stop the second" + tag);
    expect (e60_c < e60_b * 0.7f, "poly: released note decays" + tag);

    // tail: everything released
    auto tail = renderBlocks (proc, (int) sr, blockSize);
    expect (tail.peak < 1.0e-3f, "poly overlap: no stuck voices" + tag);
}

void testPolyChordsAndRepeats()
{
    const double sr = 48000.0;
    YDCoreAudioProcessor proc;
    preparePolyInit (proc, sr, 256);
    const int q = (int) (0.3 * sr);

    // three then four overlapping notes at staggered offsets (incl. odd in-block offsets)
    auto out = renderScenario (proc, 5 * q, 256,
        { { 0, 48, true }, { q + 37, 55, true }, { 2 * q + 111, 60, true }, { 3 * q + 200, 64, true },
          { 4 * q, 48, false }, { 4 * q, 55, false }, { 4 * q, 60, false }, { 4 * q, 64, false } });
    const int w = q / 2;
    for (int note : { 48, 55, 60, 64 })
        expect (pitchEnergy (out, sr, midiHz (note), 3 * q + q / 3, w) > 8.0e-5f,
                "chord of 4: note " + juce::String (note) + " audible");
    expect (proc.getEngine().getActiveVoiceCount() <= 8, "chord: sensible voice count");

    // repeated same pitch: two note-ons, two note-offs -> one voice per event pair
    YDCoreAudioProcessor p2;
    preparePolyInit (p2, sr, 256);
    auto out2 = renderScenario (p2, 4 * q, 256,
        { { 0, 60, true }, { q, 60, true }, { 2 * q, 60, false }, { 3 * q, 60, false } });
    const float eBoth = pitchEnergy (out2, sr, midiHz (60), q + q / 4, q / 2);
    expect (eBoth > 1.0e-4f, "repeated pitch: still sounding after 2nd on");
    // after the FIRST off, exactly one voice must remain at FULL sustain level
    // (ownership: one note-off stops one voice, not every voice of that pitch)
    const float eAfterFirstOff = pitchEnergy (out2, sr, midiHz (60), 2 * q + (2 * q) / 3, q / 4);
    expect (eAfterFirstOff > eBoth * 0.25f,
            "repeated pitch: one note-off leaves the second voice at sustain (ownership), e="
            + juce::String (eAfterFirstOff, 7) + " vs both=" + juce::String (eBoth, 7));
    auto tail2 = renderBlocks (p2, (int) sr, 256);
    expect (tail2.peak < 1.0e-3f, "repeated pitch: both offs clear all voices");

    // note-on and note-off inside a single block
    YDCoreAudioProcessor p3;
    preparePolyInit (p3, sr, 1024);
    auto out3 = renderScenario (p3, (int) sr, 1024, { { 100, 60, true }, { 600, 60, false }, { 2000, 64, true }, { (int) (0.8 * sr), 64, false } });
    expect (pitchEnergy (out3, sr, midiHz (64), (int) (0.3 * sr), (int) (0.2 * sr)) > 1.0e-4f,
            "on/off inside one block does not break later notes");
}

void testSustainOverlap()
{
    const double sr = 44100.0;
    YDCoreAudioProcessor proc;
    preparePolyInit (proc, sr, 256);
    const int q = (int) (0.3 * sr);

    // pedal down, play+release two notes, both must keep sounding; pedal up clears
    juce::AudioBuffer<float> all (2, 6 * q);
    all.clear();
    juce::AudioBuffer<float> block (2, 256);
    int done = 0;
    while (done < 6 * q)
    {
        const int n = std::min (256, 6 * q - done);
        block.setSize (2, n, false, false, true);
        block.clear();
        juce::MidiBuffer midi;
        auto at = [&] (int pos, juce::MidiMessage m) { if (pos >= done && pos < done + n) midi.addEvent (m, pos - done); };
        at (0,     juce::MidiMessage::controllerEvent (1, 64, 127));
        at (10,    juce::MidiMessage::noteOn (1, 60, 0.9f));
        at (q,     juce::MidiMessage::noteOn (1, 64, 0.9f));
        at (2 * q, juce::MidiMessage::noteOff (1, 60));
        at (2 * q, juce::MidiMessage::noteOff (1, 64));
        at (4 * q, juce::MidiMessage::controllerEvent (1, 64, 0));
        proc.processBlock (block, midi);
        for (int c = 0; c < 2; ++c)
            all.copyFrom (c, done, block, c, 0, n);
        done += n;
    }
    expect (pitchEnergy (all, sr, midiHz (60), 3 * q, q / 2) > 1.0e-4f, "sustain: released note 60 held by pedal");
    expect (pitchEnergy (all, sr, midiHz (64), 3 * q, q / 2) > 1.0e-4f, "sustain: released note 64 held by pedal");
    auto tail = renderBlocks (proc, (int) sr, 256);
    expect (tail.peak < 1.0e-3f, "sustain: pedal release clears voices");
}

void testModesPrioritiesAndGlide()
{
    const double sr = 44100.0;
    auto normOf = [] (YDCoreAudioProcessor& p, const char* id, float real)
    {
        auto* rp = p.getApvts().getParameter (id);
        return rp->getNormalisableRange().convertTo0to1 (real);
    };

    // Mono LAST priority: second note takes over immediately
    {
        YDCoreAudioProcessor p;
        preparePolyInit (p, sr, 256);
        p.getApvts().getParameter ("playMode")->setValueNotifyingHost (normOf (p, "playMode", 1.0f));
        const int q = (int) (0.3 * sr);
        auto out = renderScenario (p, 3 * q, 256, { { 0, 60, true }, { q, 64, true }, { 2*q, 60, false }, { 2*q, 64, false } });
        expect (pitchEnergy (out, sr, midiHz (64), q + q / 3, q / 2) > 1.0e-4f, "mono last: new note takes over");
        expect (p.getEngine().getActiveVoiceCount() <= 1, "mono: single voice");
    }
    // Mono LOW priority: higher second note intentionally waits (documented behavior)
    {
        YDCoreAudioProcessor p;
        preparePolyInit (p, sr, 256);
        p.getApvts().getParameter ("playMode")->setValueNotifyingHost (normOf (p, "playMode", 1.0f));
        p.getApvts().getParameter ("notePriority")->setValueNotifyingHost (normOf (p, "notePriority", 2.0f));
        const int q = (int) (0.3 * sr);
        auto out = renderScenario (p, 4 * q, 256, { { 0, 48, true }, { q, 60, true }, { 2 * q, 48, false }, { 3 * q, 60, false } });
        expect (pitchEnergy (out, sr, midiHz (48), q + q / 3, q / 2) > 1.0e-4f, "mono low: held low note keeps priority");
        expect (pitchEnergy (out, sr, midiHz (60), 2 * q + q / 4, q / 2) > 1.0e-4f, "mono low: higher note sounds after low released");
    }
    // Legato: pitch changes without retrigger, glide works
    {
        YDCoreAudioProcessor p;
        preparePolyInit (p, sr, 256);
        p.getApvts().getParameter ("playMode")->setValueNotifyingHost (normOf (p, "playMode", 2.0f));
        p.getApvts().getParameter ("glideTime")->setValueNotifyingHost (normOf (p, "glideTime", 0.05f));
        const int q = (int) (0.3 * sr);
        auto out = renderScenario (p, 3 * q, 256, { { 0, 60, true }, { q, 67, true }, { 2*q, 60, false }, { 2*q, 67, false } });
        expect (pitchEnergy (out, sr, midiHz (67), q + q / 2, q / 3) > 1.0e-4f, "legato: glides to the new pitch");
    }
    // Poly is immune to note priority
    {
        YDCoreAudioProcessor p;
        preparePolyInit (p, sr, 256);
        p.getApvts().getParameter ("notePriority")->setValueNotifyingHost (normOf (p, "notePriority", 2.0f));
        const int q = (int) (0.3 * sr);
        auto out = renderScenario (p, 3 * q, 256, { { 0, 48, true }, { q, 60, true }, { 2*q, 48, false }, { 2*q, 60, false } });
        expect (pitchEnergy (out, sr, midiHz (60), q + q / 3, q / 2) > 1.0e-4f, "poly: note priority does not block new notes");
    }
    // All-notes-off and all-sound-off clear everything
    {
        YDCoreAudioProcessor p;
        preparePolyInit (p, sr, 256);
        renderBlocks (p, 8192, 256, [] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0)
                for (int n : { 50, 55, 60 }) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.9f), 0);
        });
        renderBlocks (p, 8192, 256, [] (juce::MidiBuffer& m, int start, int)
        { if (start == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });
        renderBlocks (p, 22050, 256);   // let releases settle (~0.5 s)
        auto tail = renderBlocks (p, 44100, 256);
        expect (tail.peak < 1.0e-3f, "all-notes-off clears voices");
    }
}

void testVoiceStealingAtLimit()
{
    const double sr = 44100.0;
    YDCoreAudioProcessor proc;
    preparePolyInit (proc, sr, 256);
    auto* rp = proc.getApvts().getParameter ("polyLimit");
    rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (4.0f));

    // hold 4 notes then a 5th: it must sound (steal) without clicks or stuck voices
    const int q = (int) (0.25 * sr);
    auto out = renderScenario (proc, 6 * q, 256,
        { { 0, 40, true }, { 100, 45, true }, { 200, 52, true }, { 300, 57, true }, { 2 * q, 72, true },
          { 4*q, 40, false }, { 4*q, 45, false }, { 4*q, 52, false }, { 4*q, 57, false }, { 4*q, 72, false } });
    expect (pitchEnergy (out, sr, midiHz (72), 3 * q, q) > 1.0e-4f, "stealing: newest note sounds at the poly limit");
    auto st = analyse (out);
    expect (st.finite, "stealing: finite");
    auto tail = renderBlocks (proc, 44100, 256);
    expect (tail.peak < 1.0e-3f, "stealing: no stuck voices");
}

void testPresetModeScanAndOverlap()
{
    YDCoreAudioProcessor proc;
    const double sr = 48000.0;
    proc.prepareToPlay (sr, 512);
    auto& pm = proc.getPresetManager();

    int nPoly = 0, nMono = 0, nLegato = 0, nArp = 0, overlapOk = 0, polyCount = 0;
    std::cout << "---- preset mode classification ----" << std::endl;
    for (int i = 0; i < pm.getNumPresets(); ++i)
    {
        pm.loadPresetAt (i);
        const auto info = pm.getPresets()[(size_t) i];
        const int mode = (int) proc.getApvts().getRawParameterValue ("playMode")->load();
        const bool arp = proc.getApvts().getRawParameterValue ("arpOn")->load() > 0.5f;
        if (mode == 0) ++nPoly; else if (mode == 1) ++nMono; else ++nLegato;
        if (arp) ++nArp;
        std::cout << (mode == 0 ? "[POLY]  " : mode == 1 ? "[MONO]  " : "[LEGATO]")
                  << (arp ? " [ARP] " : "       ") << info.name << std::endl;

        if (mode == 0) // poly presets must pass the overlap check (arp/sustain forced off)
        {
            ++polyCount;
            auto set = [&] (const char* id, float norm) { proc.getApvts().getParameter (id)->setValueNotifyingHost (norm); };
            set ("arpOn", 0.0f);
            const int q = (int) (0.35 * sr);
            auto out = renderScenario (proc, 3 * q, 512, { { 0, 55, true }, { q, 64, true }, { 2*q, 55, false }, { 2*q, 64, false } });
            const float e = pitchEnergy (out, sr, midiHz (64), q + q / 3, q / 2);
            if (e > 3.0e-5f)
                ++overlapOk;
            else
                std::cout << "  [OVERLAP FAIL] " << info.name << " e64=" << e << std::endl;
            // flush before next preset
            renderBlocks (proc, 8192, 512, [] (juce::MidiBuffer& m, int start, int)
            { if (start == 0) m.addEvent (juce::MidiMessage::allSoundOff (1), 0); });
        }
    }
    std::cout << "modes: poly=" << nPoly << " mono=" << nMono << " legato=" << nLegato
              << " arp=" << nArp << std::endl;
    expect (overlapOk == polyCount, "every Poly factory preset passes the overlap test ("
            + juce::String (overlapOk) + "/" + juce::String (polyCount) + ")");
    expect (nPoly + nMono + nLegato == pm.getNumPresets(), "all presets classified");
}

//==============================================================================
// Randomizer suite: strengths, locks, undo, safety (1000+ runs)
//==============================================================================
void testRandomizerSuite()
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);
    auto& pm = proc.getPresetManager();
    auto& apvts = proc.getApvts();

    auto snapshotAll = [&proc]
    {
        std::vector<float> v;
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                v.push_back (rp->getValue());
        return v;
    };
    auto allValid = [&proc]
    {
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const float v = rp->getValue();
                if (! std::isfinite (v) || v < -0.0001f || v > 1.0001f)
                    return false;
            }
        return true;
    };

    using S = ydc::PresetManager::Strength;
    const S strengths[3] = { S::Subtle, S::Normal, S::Wild };
    int validCount = 0, undoOk = 0, protectedOk = 0, audibleChecked = 0, audibleOk = 0;
    const int kRuns = 3003;   // v1.2: ≥1000 per mode with the new engines in the recipe

    pm.initPatch();
    int wtRefsOk = 0, subtleEngineOk = 0, subtleCount = 0, qualityOk = 0;
    for (int i = 0; i < kRuns; ++i)
    {
        const auto strength = strengths[i % 3];
        const auto before = snapshotAll();
        const float masterBefore = apvts.getRawParameterValue ("masterLevel")->load();
        const float modeBefore   = apvts.getRawParameterValue ("playMode")->load();
        const float qualityBefore = apvts.getRawParameterValue ("qualityMode")->load();
        const float engineBefore[2] { apvts.getRawParameterValue ("osc1Engine")->load(),
                                      apvts.getRawParameterValue ("osc2Engine")->load() };
        const juce::String wtBefore[2] { proc.getOscWavetableName (0), proc.getOscWavetableName (1) };

        pm.randomizePatch (strength);

        if (allValid()) ++validCount;
        if (juce::exactlyEqual (apvts.getRawParameterValue ("masterLevel")->load(), masterBefore)
            && juce::exactlyEqual (apvts.getRawParameterValue ("playMode")->load(), modeBefore))
            ++protectedOk;
        if (juce::exactlyEqual (apvts.getRawParameterValue ("qualityMode")->load(), qualityBefore))
            ++qualityOk;
        if (! proc.isOscWavetableMissing (0) && ! proc.isOscWavetableMissing (1))
            ++wtRefsOk;
        if (strength == S::Subtle)
        {
            ++subtleCount;
            if (juce::exactlyEqual (apvts.getRawParameterValue ("osc1Engine")->load(), engineBefore[0])
                && juce::exactlyEqual (apvts.getRawParameterValue ("osc2Engine")->load(), engineBefore[1])
                && proc.getOscWavetableName (0) == wtBefore[0]
                && proc.getOscWavetableName (1) == wtBefore[1])
                ++subtleEngineOk;
        }

        // one-step undo restores the exact previous sound
        pm.undoRandomize();
        const auto after = snapshotAll();
        bool same = after.size() == before.size();
        if (same)
            for (size_t k = 0; k < after.size(); ++k)
                if (std::abs (after[k] - before[k]) > 1.0e-5f) { same = false; break; }
        if (same && proc.getOscWavetableName (0) != wtBefore[0])
            same = false;                                   // undo must restore table identity too
        if (same) ++undoOk;

        // re-apply (leave the random patch in place) and occasionally render it
        pm.randomizePatch (strength);
        if (i % 100 == 0)
        {
            ++audibleChecked;
            auto stats = renderBlocks (proc, 22050, 512, [] (juce::MidiBuffer& m, int start, int)
            {
                if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 52, 0.9f), 0);
            });
            if (stats.finite && ! stats.hasDenormals && stats.rms > 5.0e-5f && stats.peak < 2.5f)
                ++audibleOk;
            renderBlocks (proc, 22050, 512, [] (juce::MidiBuffer& m, int start, int)
            { if (start == 0) m.addEvent (juce::MidiMessage::allSoundOff (1), 0); });
        }
    }
    expect (validCount == kRuns, "randomizer: all values valid over " + juce::String (kRuns)
            + " runs (" + juce::String (validCount) + ")");
    expect (protectedOk == kRuns, "randomizer: master/play-mode never randomized (" + juce::String (protectedOk) + ")");
    expect (qualityOk == kRuns, "randomizer: quality mode never randomized (" + juce::String (qualityOk) + ")");
    expect (wtRefsOk == kRuns, "randomizer: never creates missing wavetable refs (" + juce::String (wtRefsOk) + ")");
    expect (subtleEngineOk == subtleCount, "randomizer: SUBTLE never flips engine or bank ("
            + juce::String (subtleEngineOk) + "/" + juce::String (subtleCount) + ")");
    expect (undoOk == kRuns, "randomizer: undo restores exactly incl. wavetables (" + juce::String (undoOk) + "/" + juce::String (kRuns) + ")");
    expect (audibleOk == audibleChecked, "randomizer: rendered patches finite & audible ("
            + juce::String (audibleOk) + "/" + juce::String (audibleChecked) + ")");

    // ---- section locks preserve locked parameters exactly
    pm.initPatch();
    apvts.getParameter ("cutoff")->setValueNotifyingHost (0.31f);
    apvts.getParameter ("resonance")->setValueNotifyingHost (0.62f);
    pm.setSectionLocked (ydc::PresetManager::LockFilter, true);
    int lockOk = 0;
    for (int i = 0; i < 60; ++i)
    {
        pm.randomizePatch (strengths[i % 3]);
        if (std::abs (apvts.getParameter ("cutoff")->getValue() - 0.31f) < 1.0e-5f
            && std::abs (apvts.getParameter ("resonance")->getValue() - 0.62f) < 1.0e-5f)
            ++lockOk;
    }
    pm.setSectionLocked (ydc::PresetManager::LockFilter, false);
    expect (lockOk == 60, "randomizer: FILTER lock preserves parameters exactly (" + juce::String (lockOk) + "/60)");

    // ---- modified flag + save/reload round-trip
    pm.loadPresetAt (0);
    expect (! pm.isModified(), "loaded preset reads as unmodified");
    pm.randomizePatch (S::Normal);
    expect (pm.isModified(), "randomized sound reads as modified (requires Save As)");
    const float savedCutoff = apvts.getParameter ("cutoff")->getValue();
    expect (pm.saveUserPreset ("Randomizer RT", "User"), "randomized patch saves");
    expect (! pm.isModified(), "saved patch reads as clean");
    pm.initPatch();
    for (int i = 0; i < pm.getNumPresets(); ++i)
        if (pm.getPresets()[(size_t) i].name == "Randomizer RT")
        {
            pm.loadPresetAt (i);
            break;
        }
    expect (std::abs (apvts.getParameter ("cutoff")->getValue() - savedCutoff) < 1.0e-4f,
            "randomized patch reloads exactly");
    for (int i = 0; i < pm.getNumPresets(); ++i)
        if (pm.getPresets()[(size_t) i].name == "Randomizer RT")
        {
            pm.deleteUserPreset (i);
            break;
        }

    // ---- rapid randomization during playback stays stable
    pm.initPatch();
    auto stats = renderBlocks (proc, 44100 * 2, 256,
        [&] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, 45, 0.9f), 0);
            if ((start / 256) % 5 == 0)
                pm.randomizePatch (strengths[(start / 1280) % 3]);
        });
    expect (stats.finite && ! stats.hasDenormals, "rapid randomize during playback: finite, no denormals");
    expect (stats.peak < 2.5f, "rapid randomize during playback: bounded");
}

void testParameterSystem()
{
    YDCoreAudioProcessor proc;
    const auto& params = proc.getParameters();
    expect (params.size() >= 140, "140+ automatable parameters, got " + juce::String (params.size()));

    juce::StringArray ids;
    for (auto* p : params)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            ids.add (rp->paramID);
    juce::StringArray unique = ids;
    unique.removeDuplicates (false);
    expect (unique.size() == ids.size(), "parameter IDs are unique");

    for (const char* must : { "cutoff", "resonance", "ampAttack", "osc1Wave", "masterLevel",
                              "reverbMix", "delayTime", "arpOn", "mat1Src", "lfo1Rate" })
        expect (ids.contains (must), juce::String ("required parameter exists: ") + must);
}

void testSaveUserPreset()
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);
    auto& pm = proc.getPresetManager();
    loadPresetByName (proc, "Dark Pressure");
    proc.getApvts().getParameter ("cutoff")->setValueNotifyingHost (0.33f);
    const float savedNorm = proc.getApvts().getParameter ("cutoff")->getValue();

    expect (pm.saveUserPreset ("Headless Test Patch", "User"), "user preset saves");
    pm.initPatch();

    // reload it
    bool found = false;
    for (int i = 0; i < pm.getNumPresets(); ++i)
        if (pm.getPresets()[(size_t) i].name == "Headless Test Patch")
        {
            found = true;
            expect (pm.loadPresetAt (i), "user preset reloads");
            break;
        }
    expect (found, "saved user preset appears in list");
    expect (std::abs (proc.getApvts().getParameter ("cutoff")->getValue() - savedNorm) < 1.0e-4f,
            "user preset restores tweaked value");

    // cleanup
    for (int i = 0; i < pm.getNumPresets(); ++i)
        if (pm.getPresets()[(size_t) i].name == "Headless Test Patch")
        {
            pm.deleteUserPreset (i);
            break;
        }
}

//==============================================================================
// v1.2 — append-only contract guards + legacy state migration
//==============================================================================
juce::StringArray v12ParamIds()
{
    juce::StringArray out;
    for (int i = 1; i <= 2; ++i)
        for (const char* s : { "Engine", "WtPos", "WarpMode", "WarpAmt" })
            out.add (ydc::ids::osc (i, s));
    out.add (ydc::ids::qualityMode);
    for (const char* s : { ydc::ids::ampCurveA, ydc::ids::ampCurveD, ydc::ids::ampCurveR,
                           ydc::ids::filCurveA, ydc::ids::filCurveD, ydc::ids::filCurveR,
                           ydc::ids::modCurveA, ydc::ids::modCurveD, ydc::ids::modCurveR })
        out.add (s);
    return out;
}

void testV12ParameterContract()
{
    using namespace ydc;

    // ---- frozen legacy prefixes: indices are stored in presets as real values
    auto checkPrefix = [] (const juce::StringArray& list, std::initializer_list<const char*> legacy,
                           const juce::String& name)
    {
        int i = 0;
        for (const char* s : legacy)
        {
            expect (i < list.size() && list[i] == s,
                    name + "[" + juce::String (i) + "] stays \"" + s + "\" (got \""
                    + (i < list.size() ? list[i] : juce::String ("<missing>")) + "\")");
            ++i;
        }
    };
    checkPrefix (choices::oscWaves,    { "Sine", "Triangle", "Saw", "Square", "Pulse", "SuperSaw", "Noise" }, "oscWaves");
    checkPrefix (choices::subWaves,    { "Sine", "Square" }, "subWaves");
    checkPrefix (choices::noiseTypes,  { "White", "Pink" }, "noiseTypes");
    checkPrefix (choices::filterTypes, { "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "Notch" }, "filterTypes");
    checkPrefix (choices::lfoWaves,    { "Sine", "Triangle", "Saw", "Square", "S&H" }, "lfoWaves");
    checkPrefix (choices::lfoDivisions,{ "1/1", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, "lfoDivisions");
    checkPrefix (choices::delayDivisions, { "1/2", "1/4D", "1/4", "1/4T", "1/8D", "1/8", "1/8T", "1/16" }, "delayDivisions");
    checkPrefix (choices::arpDivisions,{ "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, "arpDivisions");
    checkPrefix (choices::arpModes,    { "Up", "Down", "Up/Down", "Random" }, "arpModes");
    checkPrefix (choices::playModes,   { "Poly", "Mono", "Legato" }, "playModes");
    checkPrefix (choices::notePriorities, { "Last", "High", "Low" }, "notePriorities");
    checkPrefix (choices::lfoDests,    { "Off", "Pitch 1+2", "Osc 1 Pitch", "Osc 2 Pitch", "Filter Cutoff", "Amp Level", "Pan", "PW 1+2", "FX Mix" }, "lfoDests");
    checkPrefix (choices::modEnvDests, { "Off", "Pitch 1+2", "Osc 1 Pitch", "Osc 2 Pitch", "Osc 1 PW", "Osc 2 PW", "Filter Cutoff", "LFO 1 Rate", "LFO 2 Rate", "Noise Level" }, "modEnvDests");
    checkPrefix (choices::modSources,  { "Off", "Velocity", "Mod Wheel", "Aftertouch", "Key Track", "Amp Env", "Filter Env", "Mod Env", "LFO 1", "LFO 2", "Random", "Pitch Bend" }, "modSources");
    checkPrefix (choices::modDests,    { "Off", "Osc 1 Pitch", "Osc 2 Pitch", "Osc 1+2 Pitch", "Osc 1 Fine", "Osc 2 Fine",
                                         "Osc 1 Level", "Osc 2 Level", "Osc 1 Pan", "Osc 2 Pan", "Osc 1 PW", "Osc 2 PW",
                                         "Filter Cutoff", "Filter Reso", "Amp Level", "LFO 1 Rate", "LFO 2 Rate", "FX Mix", "Stereo Width" }, "modDests");

    // ---- enum sizes track the appended lists
    expect (choices::modDests.size()    == (int) ModDest::Count, "ModDest enum matches modDests list size");
    expect (choices::filterTypes.size() == 10, "filterTypes has the 4 appended models");
    expect (choices::oscEngines.size()  == 3 && choices::warpModes.size() == 6 && choices::qualityModes.size() == 4,
            "v1.2 choice lists have the expected sizes");

    // ---- every v1.2 parameter exists and defaults to the LEGACY behaviour
    YDCoreAudioProcessor proc;
    for (const auto& id : v12ParamIds())
    {
        auto* rp = proc.getApvts().getParameter (id);
        expect (rp != nullptr, "v1.2 parameter exists: " + id);
        if (rp != nullptr)
            expect (std::abs (rp->getValue() - rp->getDefaultValue()) < 1.0e-6f
                    && std::abs (proc.getApvts().getRawParameterValue (id)->load()) < 1.0e-6f,
                    "v1.2 parameter defaults to legacy (real value 0): " + id);
    }
    expect (proc.getParameters().size() >= 158, "parameter count grew append-only, got "
            + juce::String (proc.getParameters().size()));
}

void testV12StateMigration()
{
    const auto v12 = v12ParamIds();

    // ---- A: a patch with BOTH legacy tweaks and non-default v1.2 values
    YDCoreAudioProcessor a;
    a.prepareToPlay (48000.0, 512);
    a.getPresetManager().initPatch();
    auto setReal = [] (YDCoreAudioProcessor& p, const juce::String& id, float real)
    {
        auto* rp = p.getApvts().getParameter (id);
        rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (real));
    };
    setReal (a, "cutoff", 1234.0f);
    setReal (a, "reverbMix", 0.77f);
    setReal (a, "osc1Engine", 2.0f);       // Wavetable
    setReal (a, "osc1WtPos", 0.66f);
    setReal (a, "osc1WarpMode", 3.0f);     // Sync
    setReal (a, "qualityMode", 3.0f);      // Ultra
    setReal (a, "ampAttackCurve", -0.5f);

    auto fullXml = a.getApvts().copyState().createXml();
    expect (fullXml != nullptr, "state serialises to XML");

    // ---- simulate a 1.1-era state: remove every v1.2 PARAM entry
    auto strippedXml = std::make_unique<juce::XmlElement> (*fullXml);
    std::vector<juce::XmlElement*> doomed;
    for (auto* child : strippedXml->getChildWithTagNameIterator ("PARAM"))
        if (v12.contains (child->getStringAttribute ("id")))
            doomed.push_back (child);
    for (auto* d : doomed)
        strippedXml->removeChildElement (d, true);
    expect ((int) doomed.size() == v12.size(), "stripped state removes all v1.2 params ("
            + juce::String ((int) doomed.size()) + "/" + juce::String (v12.size()) + ")");

    // ---- B: poison the new params, then load the OLD state → all must reset to LEGACY defaults
    YDCoreAudioProcessor b;
    b.prepareToPlay (48000.0, 512);
    setReal (b, "osc1Engine", 1.0f);
    setReal (b, "osc2Engine", 2.0f);
    setReal (b, "qualityMode", 2.0f);
    setReal (b, "osc2WarpAmt", 0.9f);
    setReal (b, "modReleaseCurve", 0.8f);

    juce::MemoryBlock oldBlob;
    juce::AudioProcessor::copyXmlToBinary (*strippedXml, oldBlob);
    b.setStateInformation (oldBlob.getData(), (int) oldBlob.getSize());

    expect (std::abs (b.getApvts().getRawParameterValue ("cutoff")->load() - 1234.0f) < 1.0f,
            "old state: legacy parameter restored");
    for (const auto& id : v12)
        expect (std::abs (b.getApvts().getParameter (id)->getValue()
                          - b.getApvts().getParameter (id)->getDefaultValue()) < 1.0e-6f,
                "old state resolves v1.2 param to LEGACY default: " + id);

    // ---- C: a FULL v1.2 state round-trips its new parameters
    YDCoreAudioProcessor c;
    c.prepareToPlay (48000.0, 512);
    juce::MemoryBlock newBlob;
    a.getStateInformation (newBlob);
    c.setStateInformation (newBlob.getData(), (int) newBlob.getSize());
    expect (std::abs (c.getApvts().getRawParameterValue ("osc1Engine")->load() - 2.0f) < 1.0e-4f
            && std::abs (c.getApvts().getRawParameterValue ("osc1WtPos")->load() - 0.66f) < 1.0e-3f
            && std::abs (c.getApvts().getRawParameterValue ("qualityMode")->load() - 3.0f) < 1.0e-4f
            && std::abs (c.getApvts().getRawParameterValue ("ampAttackCurve")->load() + 0.5f) < 1.0e-3f,
            "v1.2 state round-trips new parameters");

    // ---- D: the 51 LEGACY factory presets load to exact v1.2 defaults even from
    //      a poisoned state; the v1.2 presets are exactly the ones that don't
    YDCoreAudioProcessor d;
    d.prepareToPlay (48000.0, 512);
    auto& pm = d.getPresetManager();
    int cleanCount = 0, dirtyCount = 0;
    for (int i = 0; i < pm.getNumPresets(); ++i)
    {
        if (! pm.getPresets()[(size_t) i].isFactory)
            continue;
        setReal (d, "osc1Engine", 2.0f);   // poison, then load
        setReal (d, "qualityMode", 3.0f);
        pm.loadPresetAt (i);
        bool clean = true;
        for (const auto& id : v12)
            if (std::abs (d.getApvts().getParameter (id)->getValue()
                          - d.getApvts().getParameter (id)->getDefaultValue()) > 1.0e-6f)
                clean = false;
        if (clean) ++cleanCount;
        else       ++dirtyCount;
    }
    expect (cleanCount == 51, "exactly the 51 legacy factory presets select LEGACY v1.2 defaults ("
            + juce::String (cleanCount) + ")");
    expect (dirtyCount >= 18, "18+ factory presets intentionally use the new engines ("
            + juce::String (dirtyCount) + ")");
}

//==============================================================================
// v1.2 — wavetable library invariants
//==============================================================================
void testWavetableLibrary()
{
    const auto& lib = ydc::WavetableLibrary::get();
    expect (lib.numBanks() >= 24, "at least 24 factory wavetable banks, got " + juce::String (lib.numBanks()));

    juce::StringArray categories, names;
    for (const auto& b : lib.banks())
    {
        names.add (b->name);
        categories.addIfNotAlreadyThere (b->category);
        expect (b->name.isNotEmpty() && b->description.isNotEmpty(), "bank has name+description: " + b->name);
        expect (b->numFrames >= 1 && b->numFrames <= ydc::kWtMaxFrames, "bank frame count sane: " + b->name);
        expect ((int) b->levels.size() == ydc::kWtNumLevels, "bank has all mip levels: " + b->name);

        bool structureOk = true, finiteOk = true, guardOk = true;
        for (int L = 0; L < ydc::kWtNumLevels; ++L)
        {
            const auto& lv = b->levels[(size_t) L];
            if (lv.tableSize != std::max (64, ydc::kWtFrameSize >> L)
                || lv.rowStride != lv.tableSize + ydc::kWtGuard
                || (int) lv.data.size() != b->numFrames * lv.rowStride)
                structureOk = false;
            for (float v : lv.data)
                if (! std::isfinite (v)) { finiteOk = false; break; }
            for (int f = 0; f < b->numFrames; ++f)
            {
                const float* row = b->frameRow (L, f);
                if (std::abs (row[0] - row[lv.tableSize]) > 1.0e-6f
                    || std::abs (row[lv.tableSize + 1] - row[1]) > 1.0e-6f)
                    guardOk = false;
            }
        }
        expect (structureOk, "bank mip structure correct: " + b->name);
        expect (finiteOk, "bank data finite: " + b->name);
        expect (guardOk, "bank guard samples wrap correctly: " + b->name);
    }
    juce::StringArray unique = names;
    unique.removeDuplicates (false);
    expect (unique.size() == names.size(), "bank names are unique");
    for (const char* cat : { "Analog", "Digital", "Harmonic", "Formant", "Metallic",
                             "Motion", "Soft", "Aggressive", "Experimental" })
        expect (categories.contains (cat), juce::String ("bank category present: ") + cat);

    expect (lib.hqShapes().numFrames == 2, "internal HQ shape bank has saw+triangle");
}

//==============================================================================
// v1.2 — engine render behaviour: position, warp, mip levels, identity
//==============================================================================
void prepareV12Single (YDCoreAudioProcessor& proc, double sr, int engine, int wave)
{
    proc.prepareToPlay (sr, 512);
    proc.getPresetManager().initPatch();
    auto set = [&] (const juce::String& id, float real)
    {
        auto* rp = proc.getApvts().getParameter (id);
        rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (real));
    };
    set ("osc1Engine", (float) engine);
    set ("osc1Wave", (float) wave);
    set ("osc1UniCount", 1.0f);
    set ("osc1Drift", 0.0f);
    set ("osc1RandPhase", 0.0f);
    set ("osc2On", 0.0f);
    set ("subOn", 0.0f);
    set ("noiseLevel", 0.0f);
    set ("filterOn", 0.0f);
    set ("ampSustain", 1.0f);
    set ("ampRelease", 0.05f);
}

/** Non-harmonic (aliasing) energy ratio of a sustained single note. */
float aliasRatio (YDCoreAudioProcessor& proc, double sr, int midiNote)
{
    const int total = (int) (0.8 * sr);
    auto out = renderScenario (proc, total, 512, { { 0, midiNote, true } });

    constexpr int order = 14, fftSize = 1 << order;    // 16384
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) fftSize * 2, 0.0f);
    const int start = (int) (0.25 * sr);
    const float* l = out.getReadPointer (0);
    const float* r = out.getReadPointer (1);
    for (int i = 0; i < fftSize && start + i < total; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (ydc::kTwoPi * (float) i / (float) fftSize);
        data[(size_t) i] = 0.5f * (l[start + i] + r[start + i]) * w;
    }
    fft.performRealOnlyForwardTransform (data.data());

    const double binHz = sr / fftSize;
    const double f0 = (double) midiHz (midiNote);
    double harmE = 0.0, aliasE = 0.0;
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const double f = k * binHz;
        if (f < 100.0)
            continue;
        const double re = data[(size_t) (2 * k)], im = data[(size_t) (2 * k + 1)];
        const double e = re * re + im * im;
        const double harmonic = f / f0;
        const double dist = std::abs (harmonic - std::round (harmonic)) * f0 / binHz; // distance in bins
        if (dist < 4.0)
            harmE += e;
        else
            aliasE += e;
    }
    const double totalE = harmE + aliasE;
    return totalE > 0.0 ? (float) (aliasE / totalE) : 0.0f;
}

void testHqAliasingImprovement()
{
    const double sr = 48000.0;
    const struct { int wave; const char* name; float mustImprove; } cases[] =
    {
        { 1, "triangle", 0.35f },   // naive → table: expect a big win
        { 2, "saw",      0.90f },   // polyBLEP → table: still measurably lower
        { 3, "square",   0.90f },
        { 4, "pulse",    0.90f },
    };
    for (const auto& c : cases)
    {
        float legacy = 0.0f, hq = 0.0f;
        {
            YDCoreAudioProcessor p;
            prepareV12Single (p, sr, 0, c.wave);
            if (c.wave == 4) { auto* rp = p.getApvts().getParameter ("osc1PW");
                               rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (0.3f)); }
            legacy = aliasRatio (p, sr, 103);
        }
        {
            YDCoreAudioProcessor p;
            prepareV12Single (p, sr, 1, c.wave);
            if (c.wave == 4) { auto* rp = p.getApvts().getParameter ("osc1PW");
                               rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (0.3f)); }
            hq = aliasRatio (p, sr, 103);
        }
        std::cout << "  aliasing " << c.name << ": legacy=" << legacy << " hq=" << hq << std::endl;
        expect (hq < legacy * c.mustImprove,
                juce::String ("BASIC HQ aliasing lower than LEGACY for ") + c.name
                + " (hq=" + juce::String (hq, 6) + " legacy=" + juce::String (legacy, 6) + ")");
    }

    // wavetable engine aliasing stays bounded at a high note
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 2, 2);
        const float wt = aliasRatio (p, sr, 103);
        std::cout << "  aliasing wavetable(default bank)=" << wt << std::endl;
        expect (wt < 0.05f, "WAVETABLE aliasing bounded at high notes, ratio=" + juce::String (wt, 6));
    }
}

void testWavetableEngineBehaviour()
{
    const double sr = 48000.0;
    auto setReal = [] (YDCoreAudioProcessor& p, const juce::String& id, float real)
    {
        auto* rp = p.getApvts().getParameter (id);
        rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (real));
    };

    // ---- every factory bank renders finite and audible at positions 0 / 0.5 / 1
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 2, 2);
        const auto& lib = ydc::WavetableLibrary::get();
        int okBanks = 0;
        for (const auto& b : lib.banks())
        {
            p.setOscWavetableByName (0, b->name);
            bool ok = true;
            for (float pos : { 0.0f, 0.5f, 1.0f })
            {
                setReal (p, "osc1WtPos", pos);
                auto st = renderBlocks (p, 12000, 512, [] (juce::MidiBuffer& m, int start, int)
                {
                    if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0);
                });
                if (! st.finite || st.hasDenormals || st.rms < 1.0e-4f || st.peak > 2.0f)
                {
                    ok = false;
                    std::cout << "  [WT FAIL] " << b->name << " pos=" << pos
                              << " rms=" << st.rms << " peak=" << st.peak << std::endl;
                }
                renderBlocks (p, 6000, 512, [] (juce::MidiBuffer& m, int start, int)
                { if (start == 0) m.addEvent (juce::MidiMessage::allSoundOff (1), 0); });
            }
            if (ok) ++okBanks;
        }
        expect (okBanks == lib.numBanks(), "all factory banks render at pos 0/0.5/1 ("
                + juce::String (okBanks) + "/" + juce::String (lib.numBanks()) + ")");
    }

    // ---- fast position modulation is click-safe and finite
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 2, 2);
        setReal (p, "lfo1Wave", 0.0f);
        setReal (p, "lfo1Rate", 18.0f);
        setReal (p, "lfo1Dest", 9.0f);        // WT Pos 1+2 (appended lfoDest index)
        setReal (p, "lfo1Amount", 1.0f);
        setReal (p, "osc1WtPos", 0.5f);
        auto st = renderBlocks (p, (int) sr, 512, [] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0);
        });
        expect (st.finite && ! st.hasDenormals, "fast WT position modulation: finite");
        expect (st.maxJump < 0.6f, "fast WT position modulation: click-safe, maxJump="
                + juce::String (st.maxJump, 4));
    }

    // ---- every warp mode stays finite and bounded across its range
    {
        int okRuns = 0, runs = 0;
        for (int mode = 1; mode <= 5; ++mode)
            for (float amt : { 0.25f, 0.6f, 1.0f })
            {
                ++runs;
                YDCoreAudioProcessor p;
                prepareV12Single (p, sr, 2, 2);
                setReal (p, "osc1WarpMode", (float) mode);
                setReal (p, "osc1WarpAmt", amt);
                setReal (p, "osc1WtPos", 0.4f);
                auto st = renderBlocks (p, 16000, 512, [] (juce::MidiBuffer& m, int start, int)
                {
                    if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 62, 0.9f), 0);
                });
                if (st.finite && ! st.hasDenormals && st.peak < 2.0f && st.rms > 1.0e-4f)
                    ++okRuns;
                else
                    std::cout << "  [WARP FAIL] mode=" << mode << " amt=" << amt
                              << " peak=" << st.peak << " rms=" << st.rms << std::endl;
            }
        expect (okRuns == runs, "all warp modes finite/bounded (" + juce::String (okRuns)
                + "/" + juce::String (runs) + ")");
    }

    // ---- warp amount automation is zipper-safe
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 2, 2);
        setReal (p, "osc1WarpMode", 3.0f);   // Sync
        auto* warp = p.getApvts().getParameter ("osc1WarpAmt");
        auto st = renderBlocks (p, (int) sr, 256, [&] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 55, 0.9f), 0);
            warp->setValueNotifyingHost (0.5f + 0.5f * std::sin ((float) start / sr * 20.0f));
        });
        expect (st.finite && st.peak < 2.0f, "warp automation sweep: finite + bounded, peak="
                + juce::String (st.peak, 3));
    }

    // ---- mip transitions: level steps across the keyboard stay smooth
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 2, 2);
        setReal (p, "osc1WtPos", 0.3f);
        float prevRms = -1.0f;
        bool smooth = true, finiteAll = true;
        for (int note = 24; note <= 108; note += 4)
        {
            auto st = renderBlocks (p, 8192, 512, [note] (juce::MidiBuffer& m, int start, int)
            {
                if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
            });
            if (! st.finite) finiteAll = false;
            if (prevRms > 0.0f && st.rms > 1.0e-5f)
            {
                const float ratioDb = std::abs (20.0f * std::log10 (st.rms / prevRms));
                if (ratioDb > 4.0f)   // adjacent 4-semitone steps must not jump wildly
                {
                    smooth = false;
                    std::cout << "  [MIP JUMP] note " << note << " Δ=" << ratioDb << " dB" << std::endl;
                }
            }
            prevRms = st.rms;
            renderBlocks (p, 6000, 512, [] (juce::MidiBuffer& m, int start, int)
            { if (start == 0) m.addEvent (juce::MidiMessage::allSoundOff (1), 0); });
        }
        expect (finiteAll, "keyboard sweep: finite at every pitch");
        expect (smooth, "keyboard sweep: no severe level jumps at mip boundaries");
    }

    // ---- high sample rates: engines stay stable and audible at 96/192 kHz
    for (double hiSr : { 96000.0, 192000.0 })
        for (int engine : { 1, 2 })
        {
            YDCoreAudioProcessor p;
            prepareV12Single (p, hiSr, engine, 2);
            setReal (p, "osc1WtPos", 0.4f);
            setReal (p, "osc1WarpMode", engine == 2 ? 3.0f : 0.0f);
            setReal (p, "osc1WarpAmt", 0.4f);
            auto st = renderBlocks (p, (int) (0.4 * hiSr), 512, [] (juce::MidiBuffer& m, int start, int)
            {
                if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 84, 0.9f), 0);
            });
            expect (st.finite && ! st.hasDenormals && st.rms > 5.0e-5f && st.peak < 2.0f,
                    "engine " + juce::String (engine) + " stable at " + juce::String (hiSr, 0) + " Hz");
        }

    // ---- wavetable identity: state + preset round-trips, missing-name recovery
    {
        YDCoreAudioProcessor a;
        a.prepareToPlay (sr, 512);
        a.getPresetManager().initPatch();
        a.setOscWavetableByName (0, "FM Chrome");
        a.setOscWavetableByName (1, "Vowel Morph");
        juce::MemoryBlock blob;
        a.getStateInformation (blob);

        YDCoreAudioProcessor b;
        b.prepareToPlay (sr, 512);
        b.setStateInformation (blob.getData(), (int) blob.getSize());
        expect (b.getOscWavetableName (0) == "FM Chrome" && b.getOscWavetableName (1) == "Vowel Morph",
                "state round-trip preserves wavetable identity");
        expect (! b.isOscWavetableMissing (0) && ! b.isOscWavetableMissing (1),
                "restored factory banks resolve (not missing)");

        // unknown name: keeps requested identity, plays the default bank, flags missing
        b.setOscWavetableByName (0, "Not A Real Table");
        expect (b.getOscWavetableName (0) == "Not A Real Table", "missing table name preserved in state");
        expect (b.isOscWavetableMissing (0), "missing table flagged");
        auto* rp = b.getApvts().getParameter ("osc1Engine");
        rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (2.0f));
        auto st = renderBlocks (b, 12000, 512, [] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0);
        });
        expect (st.finite && st.rms > 1.0e-4f, "missing table still renders audibly (default bank fallback)");

        // preset save/load round-trip
        YDCoreAudioProcessor c;
        c.prepareToPlay (sr, 512);
        auto& pm = c.getPresetManager();
        pm.initPatch();
        c.setOscWavetableByName (0, "Saw Stack");
        expect (pm.saveUserPreset ("WT RT Test", "User"), "wavetable preset saves");
        pm.initPatch();
        expect (c.getOscWavetableName (0) != "Saw Stack", "init resets wavetable");
        for (int i = 0; i < pm.getNumPresets(); ++i)
            if (pm.getPresets()[(size_t) i].name == "WT RT Test")
            {
                pm.loadPresetAt (i);
                break;
            }
        expect (c.getOscWavetableName (0) == "Saw Stack", "preset round-trip preserves wavetable identity");
        for (int i = 0; i < pm.getNumPresets(); ++i)
            if (pm.getPresets()[(size_t) i].name == "WT RT Test")
            {
                pm.deleteUserPreset (i);
                break;
            }
    }
}

//==============================================================================
// v1.2 — user wavetable import
//==============================================================================
juce::File writeTestWav (const juce::File& dir, const juce::String& name,
                         int channels, int bitsPerSample, bool floatFmt,
                         const std::vector<float>& mono)
{
    auto file = dir.getChildFile (name);
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = wav.createWriterFor (stream,
        juce::AudioFormatWriterOptions{}
            .withSampleRate (44100.0)
            .withNumChannels (channels)
            .withBitsPerSample (bitsPerSample)
            .withSampleFormat (floatFmt ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                        : juce::AudioFormatWriterOptions::SampleFormat::integral));
    if (writer == nullptr)
        return {};
    juce::AudioBuffer<float> buf (channels, (int) mono.size());
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < (int) mono.size(); ++i)
            buf.setSample (c, i, c == 1 ? mono[(size_t) i] * 0.5f : mono[(size_t) i]);
    writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    writer.reset();
    return file;
}

std::vector<float> testCycles (int frameLen, int frames)
{
    std::vector<float> v ((size_t) frameLen * (size_t) frames);
    for (int f = 0; f < frames; ++f)
        for (int i = 0; i < frameLen; ++i)
        {
            const float t = (float) i / (float) frameLen;
            const float morph = frames > 1 ? (float) f / (float) (frames - 1) : 0.0f;
            v[(size_t) f * (size_t) frameLen + (size_t) i]
                = (1.0f - morph) * std::sin (ydc::kTwoPi * t)
                + morph * (2.0f * t - 1.0f) * 0.8f;
        }
    return v;
}

void testWavetableImport()
{
    auto tmpDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("globus_wt_import_tests");
    tmpDir.createDirectory();

    // ---- valid: mono float32, 8 × 2048 frames
    {
        auto f = writeTestWav (tmpDir, "valid_mono_f32.wav", 1, 32, true, testCycles (2048, 8));
        auto r = ydc::importWavetableFromFile (f);
        expect (r.ok(), "import: mono float32 multi-frame ok (" + r.error + ")");
        if (r.ok())
        {
            expect (r.framesDetected == 8 && r.sourceFrameSize == 2048, "import: 8×2048 frames detected");
            expect (r.bank->numFrames == 8 && r.bank->name.startsWith ("User/"), "import: bank built with User/ name");
        }
    }
    // ---- valid: stereo 16-bit (documented downmix), 4 × 2048
    {
        auto f = writeTestWav (tmpDir, "valid_stereo_16.wav", 2, 16, false, testCycles (2048, 4));
        auto r = ydc::importWavetableFromFile (f);
        expect (r.ok() && r.wasStereo, "import: stereo 16-bit ok with downmix (" + r.error + ")");
    }
    // ---- valid: 24-bit PCM, 5 × 1024 (resample path; odd count so the layout
    //      is unambiguous — an even count of 1024 IS 2048 frames by convention)
    {
        auto f = writeTestWav (tmpDir, "valid_24_1024.wav", 1, 24, false, testCycles (1024, 5));
        auto r = ydc::importWavetableFromFile (f);
        expect (r.ok() && r.framesDetected == 5 && r.sourceFrameSize == 1024,
                "import: 24-bit 1024-frame table resamples (" + r.error + ")");
    }
    // ---- valid: single cycle 600 samples
    {
        auto f = writeTestWav (tmpDir, "valid_single_600.wav", 1, 16, false, testCycles (600, 1));
        auto r = ydc::importWavetableFromFile (f);
        expect (r.ok() && r.framesDetected == 1, "import: single-cycle resamples to one frame (" + r.error + ")");
    }
    // ---- valid: explicit frame-size hint
    {
        auto f = writeTestWav (tmpDir, "valid_hint_512.wav", 1, 16, false, testCycles (512, 6));
        auto r = ydc::importWavetableFromFile (f, 512);
        expect (r.ok() && r.framesDetected == 6, "import: explicit 512 frame hint (" + r.error + ")");
        auto bad = ydc::importWavetableFromFile (f, 700);
        expect (! bad.ok(), "import: wrong hint fails clearly");
    }
    // ---- malformed inputs fail safely with clear errors
    {
        auto junk = tmpDir.getChildFile ("junk.wav");
        junk.replaceWithText ("this is definitely not a wav file, just text pretending");
        expect (! ydc::importWavetableFromFile (junk).ok(), "import: junk bytes rejected");

        auto empty = tmpDir.getChildFile ("empty.wav");
        empty.deleteFile();
        empty.create();
        expect (! ydc::importWavetableFromFile (empty).ok(), "import: empty file rejected");

        auto good = writeTestWav (tmpDir, "to_truncate.wav", 1, 16, false, testCycles (2048, 4));
        juce::MemoryBlock head;
        good.loadFileAsData (head);
        auto trunc = tmpDir.getChildFile ("truncated.wav");
        trunc.deleteFile();
        trunc.appendData (head.getData(), 60);
        expect (! ydc::importWavetableFromFile (trunc).ok(), "import: truncated file rejected");

        expect (! ydc::importWavetableFromFile (tmpDir.getChildFile ("missing.wav")).ok(),
                "import: missing file rejected");

        // undecidable layout: not divisible, too long for a single cycle
        auto odd = writeTestWav (tmpDir, "odd_layout.wav", 1, 16, false, testCycles (2048 * 40 + 7, 1));
        auto r = ydc::importWavetableFromFile (odd);
        expect (! r.ok() && r.error.isNotEmpty(), "import: undecidable layout rejected with message");
    }

    // ---- async processor import: registers, selects, renders; asset copy + reload
    {
        auto f = writeTestWav (tmpDir, "Async Table.wav", 1, 32, true, testCycles (2048, 16));

        YDCoreAudioProcessor p;
        p.prepareToPlay (48000.0, 512);
        p.getPresetManager().initPatch();
        auto* rp = p.getApvts().getParameter ("osc1Engine");
        rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (2.0f));

        p.importWavetableAsync (0, f);
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 8000.0;
        while (p.getOscWavetableName (0) != "User/Async Table"
               && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        expect (p.getOscWavetableName (0) == "User/Async Table",
                "async import selects the imported table (err: " + p.getLastWavetableImportError() + ")");
        expect (! p.isOscWavetableMissing (0), "async import: not missing");
        auto st = renderBlocks (p, 12000, 512, [] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0);
        });
        expect (st.finite && st.rms > 1.0e-4f, "imported table renders audibly");

        const auto asset = ydc::userWavetableDirectory().getChildFile ("Async Table.wav");
        expect (asset.existsAsFile(), "import copied the wav into the asset directory");

        // a FRESH processor re-resolves the user table from the asset directory
        {
            YDCoreAudioProcessor q;
            q.prepareToPlay (48000.0, 512);
            q.setOscWavetableByName (0, "User/Async Table");
            expect (! q.isOscWavetableMissing (0), "user table reloads from the asset directory");
        }
        asset.deleteFile();

        // with the asset gone, a fresh resolve reports missing but stays audible
        {
            YDCoreAudioProcessor q;
            q.prepareToPlay (48000.0, 512);
            q.setOscWavetableByName (0, "User/Async Table");
            expect (q.isOscWavetableMissing (0), "deleted user asset reports missing");
            expect (q.getOscWavetableName (0) == "User/Async Table", "missing user table name preserved");
            auto* rq = q.getApvts().getParameter ("osc1Engine");
            rq->setValueNotifyingHost (rq->getNormalisableRange().convertTo0to1 (2.0f));
            auto st2 = renderBlocks (q, 8000, 512, [] (juce::MidiBuffer& m, int start, int)
            {
                if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0);
            });
            expect (st2.finite && st2.rms > 1.0e-4f, "missing user table falls back audibly");
        }
    }

    tmpDir.deleteRecursively();
}

//==============================================================================
// v1.2 — appended filter models, envelope curves, quality switching, new mods
//==============================================================================
void testV12Filters()
{
    const double sr = 48000.0;
    auto setReal = [] (YDCoreAudioProcessor& p, const juce::String& id, float real)
    {
        auto* rp = p.getApvts().getParameter (id);
        rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (real));
    };

    // every appended model × resonance × drive × quality renders finite/bounded/audible
    int ok = 0, runs = 0;
    for (int ftype : { 6, 7, 8, 9 })                      // Ladder 24, OTA 24, SEM 12, BP 24
        for (float res : { 0.3f, 0.95f })
            for (float drive : { 0.0f, 0.8f })
                for (float quality : { 1.0f, 2.0f })      // Eco, High
                {
                    ++runs;
                    YDCoreAudioProcessor p;
                    prepareV12Single (p, sr, 1, 2);       // Basic HQ saw source
                    setReal (p, "filterOn", 1.0f);
                    setReal (p, "filterType", (float) ftype);
                    setReal (p, "cutoff", ftype == 9 ? 1200.0f : 800.0f);
                    setReal (p, "resonance", res);
                    setReal (p, "filterDrive", drive);
                    setReal (p, "qualityMode", quality);
                    auto st = renderBlocks (p, 20000, 512, [] (juce::MidiBuffer& m, int start, int)
                    {
                        if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 45, 0.9f), 0);
                    });
                    if (st.finite && ! st.hasDenormals && st.peak < 2.5f && st.rms > 5.0e-5f)
                        ++ok;
                    else
                        std::cout << "  [FILTER FAIL] type=" << ftype << " res=" << res
                                  << " drive=" << drive << " q=" << quality
                                  << " peak=" << st.peak << " rms=" << st.rms << std::endl;
                }
    expect (ok == runs, "appended filter models stable across res/drive/quality ("
            + juce::String (ok) + "/" + juce::String (runs) + ")");

    // high-resonance + full-range automation stays finite and zipper-safe
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 1, 2);
        setReal (p, "filterOn", 1.0f);
        setReal (p, "filterType", 6.0f);
        setReal (p, "resonance", 0.98f);
        setReal (p, "qualityMode", 2.0f);
        auto* cut = p.getApvts().getParameter ("cutoff");
        auto* drv = p.getApvts().getParameter ("filterDrive");
        auto st = renderBlocks (p, (int) sr * 2, 256, [&] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 40, 0.9f), 0);
            const float ph = (float) start / (float) sr;
            cut->setValueNotifyingHost (0.5f + 0.5f * std::sin (ph * 9.0f));
            drv->setValueNotifyingHost (0.5f + 0.5f * std::sin (ph * 4.0f));
        });
        expect (st.finite && ! st.hasDenormals, "ladder @ res 0.98 + automation: finite");
        expect (st.peak < 3.0f, "ladder automation: bounded, peak=" + juce::String (st.peak, 3));
    }

    // legacy models still render through the untouched path with a HQ quality set
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 0, 2);
        setReal (p, "filterOn", 1.0f);
        setReal (p, "filterType", 1.0f);
        setReal (p, "qualityMode", 3.0f);
        auto st = renderBlocks (p, 12000, 512, [] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 45, 0.9f), 0);
        });
        expect (st.finite && st.rms > 1.0e-4f, "legacy filter path unaffected by quality mode");
    }
}

void testV12EnvelopeCurves()
{
    const double fs = 48000.0;

    // ---- attack: + is snappier, − is softer; 0 = classic
    auto attackSamplesToPeak = [fs] (float curve)
    {
        ydc::AdsrEnv env;
        env.prepare (fs);
        env.setParams (0.05f, 0.5f, 0.5f, 0.1f);
        env.setCurves (curve, 0.0f, 0.0f);
        env.noteOn();
        int n = 0;
        while (env.process() < 1.0f && n < (int) fs)
            ++n;
        return n;
    };
    const int aFast = attackSamplesToPeak (1.0f);
    const int aRef  = attackSamplesToPeak (0.0f);
    const int aSoft = attackSamplesToPeak (-1.0f);
    expect (aFast < aRef && aRef < aSoft,
            "attack curve orders shapes (fast " + juce::String (aFast) + " < classic "
            + juce::String (aRef) + " < soft " + juce::String (aSoft) + ")");

    // ---- decay: at the half-time point, linear (−1) sits above classic, steep (+1) below
    auto decayLevelAtHalfTime = [fs] (float curve)
    {
        ydc::AdsrEnv env;
        env.prepare (fs);
        env.setParams (0.001f, 1.0f, 0.0f, 0.1f);
        env.setCurves (0.0f, curve, 0.0f);
        env.noteOn();
        int n = 0;
        while (env.process() < 1.0f && n < (int) fs) ++n;     // ride out the attack
        for (int i = 0; i < (int) (0.5 * fs); ++i) env.process();
        return env.getLevel();
    };
    const float dLin   = decayLevelAtHalfTime (-1.0f);
    const float dRef   = decayLevelAtHalfTime (0.0f);
    const float dSteep = decayLevelAtHalfTime (1.0f);
    expect (dLin > dRef && dRef > dSteep,
            "decay curve orders shapes (lin " + juce::String (dLin, 4) + " > classic "
            + juce::String (dRef, 4) + " > steep " + juce::String (dSteep, 4) + ")");

    // ---- extremes stay finite, never stick, and release fully
    int stuck = 0;
    for (float c : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        ydc::AdsrEnv env;
        env.prepare (fs);
        env.setParams (0.001f, 0.002f, 0.4f, 0.002f);         // stress-short times
        env.setCurves (c, c, c);
        env.noteOn();
        for (int i = 0; i < 4800; ++i) env.process();
        env.noteOff();
        int n = 0;
        while (env.isActive() && n < (int) fs) { env.process(); ++n; }
        if (env.isActive() || ! std::isfinite (env.getLevel()))
            ++stuck;
    }
    expect (stuck == 0, "envelope curves: no stuck stages at extreme settings");

    // ---- retrigger continuity: noteOn mid-release resumes from the current level
    {
        ydc::AdsrEnv env;
        env.prepare (fs);
        env.setParams (0.2f, 0.3f, 0.8f, 1.0f);
        env.setCurves (0.7f, -0.6f, -0.8f);
        env.noteOn();
        for (int i = 0; i < 9600; ++i) env.process();
        env.noteOff();
        for (int i = 0; i < 2400; ++i) env.process();
        const float before = env.getLevel();
        env.noteOn();
        const float after = env.process();
        expect (std::abs (after - before) < 0.05f, "curved retrigger is click-free (Δ="
                + juce::String (std::abs (after - before), 5) + ")");
    }

    // ---- full-voice render with curves engaged stays clean
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, 48000.0, 1, 2);
        for (const char* id : { "ampAttackCurve", "ampDecayCurve", "ampReleaseCurve" })
        {
            auto* rp = p.getApvts().getParameter (id);
            rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (-0.8f));
        }
        auto st = renderBlocks (p, 24000, 512, [] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0)  m.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0);
            if (start == 12288) m.addEvent (juce::MidiMessage::noteOff (1, 50), 0);
        });
        expect (st.finite && ! st.hasDenormals && st.maxJump < 0.9f,
                "curved envelopes render click-free in the voice");
    }
}

void testV12QualitySwitchingAndNewMods()
{
    const double sr = 48000.0;
    auto setReal = [] (YDCoreAudioProcessor& p, const juce::String& id, float real)
    {
        auto* rp = p.getApvts().getParameter (id);
        rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (real));
    };

    // quality flips mid-playback: no crash, no NaN, bounded (Q17)
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 2, 2);
        setReal (p, "filterOn", 1.0f);
        setReal (p, "filterType", 6.0f);
        setReal (p, "cutoff", 2000.0f);
        auto* q = p.getApvts().getParameter ("qualityMode");
        auto st = renderBlocks (p, (int) sr * 2, 256, [&] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 45, 0.9f), 0);
            if ((start / 256) % 4 == 0)
                q->setValueNotifyingHost ((float) ((start / 1024) % 4) / 3.0f);
        });
        expect (st.finite && ! st.hasDenormals, "quality switching during playback: finite");
        expect (st.peak < 2.5f, "quality switching: bounded, peak=" + juce::String (st.peak, 3));
    }

    // appended matrix destinations drive the new engines (detune/spread/warp/pos/drive)
    {
        const struct { int dst; const char* name; } dests[] =
        {
            { 19, "Osc 1 WT Pos" }, { 21, "Osc 1 Warp" }, { 23, "Osc 1 Detune" },
            { 25, "Osc 1 Spread" }, { 27, "Filter Drive" },
        };
        int ok = 0;
        for (const auto& d : dests)
        {
            YDCoreAudioProcessor p;
            prepareV12Single (p, sr, 2, 2);
            setReal (p, "filterOn", 1.0f);
            setReal (p, "filterType", 6.0f);
            setReal (p, "osc1UniCount", 5.0f);
            setReal (p, "osc1WarpMode", 4.0f);
            setReal (p, "mat1Src", 8.0f);            // LFO 1
            setReal (p, "mat1Dst", (float) d.dst);
            setReal (p, "mat1Amt", 0.8f);
            setReal (p, "lfo1Rate", 7.0f);
            auto st = renderBlocks (p, 24000, 512, [] (juce::MidiBuffer& m, int start, int)
            {
                if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0);
            });
            if (st.finite && ! st.hasDenormals && st.peak < 2.5f && st.rms > 5.0e-5f)
                ++ok;
            else
                std::cout << "  [MODDEST FAIL] " << d.name << " peak=" << st.peak
                          << " rms=" << st.rms << std::endl;
        }
        expect (ok == 5, "appended mod destinations render cleanly (" + juce::String (ok) + "/5)");
    }

    // every appended host parameter survives simultaneous random automation,
    // including engine switches mid-note (Q24)
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 0, 2);
        setReal (p, "filterOn", 1.0f);
        setReal (p, "filterType", 7.0f);
        juce::Array<juce::RangedAudioParameter*> newParams;
        for (const auto& id : v12ParamIds())
            newParams.add (p.getApvts().getParameter (id));
        juce::Random rnd (777);
        auto st = renderBlocks (p, (int) sr * 2, 256, [&] (juce::MidiBuffer& m, int start, int)
        {
            if (start % 12000 == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, 40 + (start / 12000) * 5, 0.9f), 0);
            for (auto* rp : newParams)
                if (rnd.nextFloat() < 0.3f)
                    rp->setValueNotifyingHost (rnd.nextFloat());
        });
        expect (st.finite && ! st.hasDenormals, "random automation of every v1.2 parameter: finite");
        expect (st.peak < 2.5f, "random automation of every v1.2 parameter: bounded, peak="
                + juce::String (st.peak, 3));
    }
}

//==============================================================================
// v1.2 — FX quality paths, latency reporting, unison/mono gain staging
//==============================================================================
void testV12FxQualityAndGainStaging()
{
    const double sr = 48000.0;
    auto setReal = [] (YDCoreAudioProcessor& p, const juce::String& id, float real)
    {
        auto* rp = p.getApvts().getParameter (id);
        rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (real));
    };

    // ---- oversampled distortion: finite, bounded, DC-free, correct latency
    for (float quality : { 2.0f, 3.0f })
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 1, 2);
        setReal (p, "qualityMode", quality);
        setReal (p, "distOn", 1.0f);
        setReal (p, "distDrive", 0.7f);
        setReal (p, "distMix", 1.0f);
        auto out = renderScenario (p, 48000, 512, { { 0, 45, true } });
        auto st = analyse (out);
        double mean = 0.0;
        for (int i = 24000; i < 48000; ++i)
            mean += out.getSample (0, i);
        mean /= 24000.0;
        expect (st.finite && ! st.hasDenormals && st.peak < 2.0f,
                "oversampled distortion clean at quality " + juce::String ((int) quality));
        expect (std::abs (mean) < 0.01, "oversampled distortion DC-free (mean="
                + juce::String (mean, 6) + ")");
        const int lat = p.getLatencySamples();
        const int want = quality > 2.5f ? ydc::Distortion::kLatencyUltra : ydc::Distortion::kLatencyHigh;
        expect (lat == want, "latency reported for quality " + juce::String ((int) quality)
                + " (" + juce::String (lat) + " == " + juce::String (want) + ")");
    }
    // legacy quality reports zero latency
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 0, 2);
        renderBlocks (p, 2048, 512);
        expect (p.getLatencySamples() == 0, "LEGACY quality reports zero latency");
    }

    // ---- HQ reverb: dense tail that decays and never runs away
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 1, 2);
        setReal (p, "qualityMode", 2.0f);
        setReal (p, "reverbOn", 1.0f);
        setReal (p, "reverbMix", 0.5f);
        setReal (p, "reverbSize", 0.9f);
        // 0.5 s note, then 3 s of tail
        auto out = renderScenario (p, (int) (3.5 * sr), 512, { { 0, 50, true }, { (int) (0.5 * sr), 50, false } });
        auto st = analyse (out);
        expect (st.finite && ! st.hasDenormals, "HQ reverb: finite, no denormals");
        auto rmsWindow = [&out] (int start, int len)
        {
            double s = 0.0;
            for (int c = 0; c < 2; ++c)
            {
                const float* d = out.getReadPointer (c);
                for (int i = start; i < start + len; ++i)
                    s += (double) d[i] * d[i];
            }
            return std::sqrt (s / (2.0 * len));
        };
        const double early = rmsWindow ((int) (1.0 * sr), (int) (0.5 * sr));
        const double late  = rmsWindow ((int) (2.8 * sr), (int) (0.5 * sr));
        expect (late < early, "HQ reverb tail decays (early=" + juce::String (early, 5)
                + " late=" + juce::String (late, 5) + ")");
        expect (early > 1.0e-4, "HQ reverb actually produces a tail");
    }

    // ---- chorus + delay HQ interpolation paths render clean under automation
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 1, 2);
        setReal (p, "qualityMode", 1.0f);      // Eco engages the interpolation upgrades
        setReal (p, "chorusOn", 1.0f);
        setReal (p, "chorusMix", 0.6f);
        setReal (p, "delayOn", 1.0f);
        setReal (p, "delayMix", 0.4f);
        setReal (p, "delaySync", 0.0f);
        auto* depth = p.getApvts().getParameter ("chorusDepth");
        auto* time  = p.getApvts().getParameter ("delayTime");
        auto st = renderBlocks (p, (int) sr * 2, 256, [&] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0);
            const float ph = (float) start / (float) sr;
            depth->setValueNotifyingHost (0.5f + 0.5f * std::sin (ph * 5.0f));
            time->setValueNotifyingHost (0.3f + 0.25f * std::sin (ph * 2.0f));
        });
        expect (st.finite && ! st.hasDenormals && st.peak < 2.0f,
                "HQ chorus+delay automation: clean, peak=" + juce::String (st.peak, 3));
    }

    // ---- EQ smoothing: fast gain automation stays finite/bounded
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 1, 2);
        setReal (p, "qualityMode", 2.0f);
        setReal (p, "eqOn", 1.0f);
        auto* low = p.getApvts().getParameter ("eqLow");
        auto st = renderBlocks (p, (int) sr, 256, [&] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 40, 0.9f), 0);
            low->setValueNotifyingHost ((start / 256) % 2 == 0 ? 1.0f : 0.0f);   // worst case flips
        });
        expect (st.finite && st.peak < 2.5f, "smoothed EQ under worst-case automation: clean");
    }

    // ---- unison level compensation: 1..7 voices stay within a 4 dB band (HQ + WT).
    // Methodology: detuned unison BEATS — the average must be measured across
    // several beat cycles (35-cent detune, 1.5 s window) and on a harmonically
    // rich frame (a pure sine cancels/reinforces wholesale and proves nothing).
    for (int engine : { 1, 2 })
    {
        float lo = 1.0e9f, hi = 0.0f;
        for (int uni = 1; uni <= 7; ++uni)
        {
            YDCoreAudioProcessor p;
            prepareV12Single (p, sr, engine, 2);
            setReal (p, "osc1UniCount", (float) uni);
            setReal (p, "osc1UniDetune", 35.0f);
            setReal (p, "osc1RandPhase", 1.0f);      // realistic phases
            if (engine == 2)
                setReal (p, "osc1WtPos", 0.35f);     // harmonically rich frame region
            const int total = (int) (1.75 * sr);
            auto out = renderScenario (p, total, 512, { { 0, 50, true } });
            double s = 0.0;
            const int start = (int) (0.25 * sr);
            for (int c = 0; c < 2; ++c)
            {
                const float* d = out.getReadPointer (c);
                for (int i = start; i < total; ++i)
                    s += (double) d[i] * d[i];
            }
            const float rms = (float) std::sqrt (s / (2.0 * (total - start)));
            lo = std::min (lo, rms);
            hi = std::max (hi, rms);
        }
        const float spreadDb = 20.0f * std::log10 (hi / std::max (1.0e-9f, lo));
        std::cout << "  unison rms spread engine " << engine << ": " << spreadDb << " dB" << std::endl;
        expect (spreadDb < 4.0f, "unison 1-7 level compensated within 4 dB (engine "
                + juce::String (engine) + ", spread " + juce::String (spreadDb, 2) + " dB)");
    }

    // ---- mono compatibility: full unison+spread must survive a mono fold
    {
        YDCoreAudioProcessor p;
        prepareV12Single (p, sr, 1, 2);
        setReal (p, "osc1UniCount", 7.0f);
        setReal (p, "osc1UniSpread", 1.0f);
        setReal (p, "osc1UniDetune", 25.0f);
        setReal (p, "osc1RandPhase", 1.0f);
        auto out = renderScenario (p, 24000, 512, { { 0, 50, true } });
        double stereoE = 0.0, monoE = 0.0;
        const float* l = out.getReadPointer (0);
        const float* r = out.getReadPointer (1);
        for (int i = 12000; i < 24000; ++i)
        {
            stereoE += 0.5 * ((double) l[i] * l[i] + (double) r[i] * r[i]);
            const double m = 0.5 * (l[i] + r[i]);
            monoE += m * m;
        }
        const float lossDb = (float) (10.0 * std::log10 (std::max (1.0e-12, monoE)
                                                         / std::max (1.0e-12, stereoE)));
        std::cout << "  mono fold loss: " << lossDb << " dB" << std::endl;
        expect (lossDb > -6.0f, "mono fold keeps at least -6 dB of energy (got "
                + juce::String (lossDb, 2) + " dB)");
    }

    // ---- gain staging diagnostics across representative presets (documented bounds)
    {
        YDCoreAudioProcessor p;
        p.prepareToPlay (sr, 512);
        auto& pm = p.getPresetManager();
        float worstPeak = 0.0f;
        int measured = 0;
        for (int i = 0; i < pm.getNumPresets(); i += 7)   // every 7th preset
        {
            pm.loadPresetAt (i);
            auto out = renderScenario (p, 24000, 512,
                { { 0, 50, true }, { 0, 62, true }, { 18000, 50, false }, { 18000, 62, false } });
            auto st = analyse (out);
            worstPeak = std::max (worstPeak, st.peak);
            ++measured;
            renderBlocks (p, 8000, 512, [] (juce::MidiBuffer& m, int start, int)
            { if (start == 0) m.addEvent (juce::MidiMessage::allSoundOff (1), 0); });
        }
        std::cout << "  gain staging: " << measured << " presets, worst peak " << worstPeak << std::endl;
        expect (worstPeak < 1.05f, "preset gain staging: soft-clipped master stays below 1.05 (worst "
                + juce::String (worstPeak, 3) + ")");
    }
}

//==============================================================================
// v1.2 — new factory content: 18+ presets on the new engines, refs resolve,
// legacy names intact, play modes intentional
//==============================================================================
void testV12FactoryPresets()
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    auto& pm = proc.getPresetManager();

    int factoryCount = 0, v12Count = 0, v12Audible = 0, refsOk = 0, refsTotal = 0;
    juce::StringArray v12ModeExceptions;      // intentional non-Poly v1.2 presets
    for (int i = 0; i < pm.getNumPresets(); ++i)
    {
        const auto info = pm.getPresets()[(size_t) i];
        if (! info.isFactory)
            continue;
        ++factoryCount;
        pm.loadPresetAt (i);
        const bool isV12 = proc.getApvts().getRawParameterValue ("qualityMode")->load() > 0.5f;
        if (! isV12)
            continue;
        ++v12Count;

        // wavetable references must resolve (never missing on factory content)
        ++refsTotal;
        if (! proc.isOscWavetableMissing (0) && ! proc.isOscWavetableMissing (1))
            ++refsOk;

        const int mode = (int) proc.getApvts().getRawParameterValue ("playMode")->load();
        if (mode != 0)
            v12ModeExceptions.add (info.name);

        auto st = renderBlocks (proc, 30000, 512, [] (juce::MidiBuffer& m, int start, int)
        {
            if (start == 0)
            {
                m.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0);
                m.addEvent (juce::MidiMessage::noteOn (1, 62, 0.9f), 0);
            }
        });
        if (st.finite && ! st.hasDenormals && st.rms > 1.0e-4f && st.peak < 1.05f)
            ++v12Audible;
        else
            std::cout << "  [V12 PRESET FAIL] " << info.name << " rms=" << st.rms
                      << " peak=" << st.peak << std::endl;
        renderBlocks (proc, 8000, 512, [] (juce::MidiBuffer& m, int start, int)
        { if (start == 0) m.addEvent (juce::MidiMessage::allSoundOff (1), 0); });
    }

    expect (factoryCount >= 69, "51 legacy + 18+ new factory presets, got " + juce::String (factoryCount));
    expect (v12Count >= 18, "at least 18 v1.2 presets, got " + juce::String (v12Count));
    expect (v12Audible == v12Count, "every v1.2 preset renders audibly within bounds ("
            + juce::String (v12Audible) + "/" + juce::String (v12Count) + ")");
    expect (refsOk == refsTotal, "every v1.2 preset resolves its wavetables ("
            + juce::String (refsOk) + "/" + juce::String (refsTotal) + ")");

    // intentional play-mode documentation: exactly these two are non-Poly
    v12ModeExceptions.sort (false);
    expect (v12ModeExceptions.size() == 2
            && v12ModeExceptions.contains ("Neon Vector Bass")
            && v12ModeExceptions.contains ("Glass Caller"),
            "v1.2 non-Poly presets are exactly the documented ones (got: "
            + v12ModeExceptions.joinIntoString (", ") + ")");

    // legacy names intact (spot equivalence — full list guarded by count + migration test)
    for (const char* name : { "Dark Pressure", "Neon Bite", "Random Walk", "Glitch Bloom" })
    {
        bool found = false;
        for (const auto& info : pm.getPresets())
            if (info.isFactory && info.name == name)
                found = true;
        expect (found, juce::String ("legacy preset name intact: ") + name);
    }
}
} // namespace

//==============================================================================
static bool writePng (juce::Component& comp, const juce::File& out)
{
    auto image = comp.createComponentSnapshot (comp.getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream (image, stream);
}

/** --screenshot <file.png>: dump the editor (OSC tab) as a PNG.
    --screenshots <dir> [WxH] [preset]: dump every tab at the given window size
    (default 1200x760, preset "Neon Bite"). Requires a display / Xvfb; also
    validates that the editor constructs. */
static int runScreenshot (const juce::String& outPath, bool allTabs, int w = 1200, int h = 760,
                          const juce::String& presetName = "Neon Bite")
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    loadPresetByName (proc, presetName);

    std::unique_ptr<juce::AudioProcessorEditor> editorBase (proc.createEditor());
    auto* editor = dynamic_cast<YDCoreAudioProcessorEditor*> (editorBase.get());
    if (editor == nullptr)
    {
        std::cout << "[FAIL] createEditor returned null / wrong type" << std::endl;
        return 1;
    }
    editor->setSize (w, h);

    int failures2 = 0;
    if (allTabs)
    {
        const char* tabNames[] = { "osc", "fx", "matrix", "global", "presets" };
        juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (outPath);
        dir.createDirectory();
        for (int i = 0; i < YDCoreAudioProcessorEditor::kNumTabs; ++i)
        {
            editor->setActiveTab (i);
            const auto f = dir.getChildFile ("tab" + juce::String (i) + "_" + tabNames[i]
                                             + "_" + juce::String (w) + "x" + juce::String (h) + ".png");
            const bool ok = writePng (*editor, f);
            std::cout << (ok ? "[OK] " : "[FAIL] ") << f.getFullPathName() << std::endl;
            if (! ok) ++failures2;
        }
    }
    else
    {
        const auto f = juce::File::getCurrentWorkingDirectory().getChildFile (outPath);
        const bool ok = writePng (*editor, f);
        std::cout << (ok ? "[OK] " : "[FAIL] ") << "editor snapshot -> " << f.getFullPathName() << std::endl;
        if (! ok) ++failures2;
    }
    editorBase.reset();
    return failures2 == 0 ? 0 : 1;
}

//==============================================================================
// Baseline render harness — the regression reference for engine work.
//   --baseline write <dir>            render every factory preset through a
//                                     canonical scenario at 44.1/48/96 kHz and
//                                     store float32 WAVs + baseline_stats.json
//   --baseline check <dir> [tol]      re-render identically and compare sample
//                                     by sample against the stored WAVs
// Renders are deterministic for a fresh processor (seeded voice RNG), so a
// rebuilt binary with an untouched legacy path must reproduce them within a
// tiny numeric tolerance (default 1e-4 ≈ -80 dBFS).
//==============================================================================
static juce::String baselineFileStem (const juce::String& presetName, double sr)
{
    return juce::File::createLegalFileName (presetName).replace (" ", "_")
           + "_" + juce::String ((int) sr);
}

static juce::AudioBuffer<float> renderBaselineScenario (const juce::String& presetName,
                                                        double sr, double& wallSeconds)
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (sr, 512);
    loadPresetByName (proc, presetName);

    const int offAt = (int) (1.5 * sr);
    const int total = (int) (3.0 * sr);
    juce::AudioBuffer<float> all (2, total);
    all.clear();
    juce::AudioBuffer<float> block (2, 512);

    const double t0 = juce::Time::getMillisecondCounterHiRes();
    int done = 0;
    while (done < total)
    {
        const int n = std::min (512, total - done);
        block.setSize (2, n, false, false, true);
        block.clear();
        juce::MidiBuffer midi;
        if (done == 0)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 62, 0.9f), 0);
        }
        if (offAt >= done && offAt < done + n)
        {
            midi.addEvent (juce::MidiMessage::noteOff (1, 50), offAt - done);
            midi.addEvent (juce::MidiMessage::noteOff (1, 62), offAt - done);
        }
        proc.processBlock (block, midi);
        for (int c = 0; c < 2; ++c)
            all.copyFrom (c, done, block, c, 0, n);
        done += n;
    }
    wallSeconds = (juce::Time::getMillisecondCounterHiRes() - t0) * 0.001;
    return all;
}

/** Spectral stats over the sustain window (0.5..1.4 s): centroid + band energy. */
static void baselineSpectralStats (const juce::AudioBuffer<float>& buf, double sr,
                                   double& centroidHz, double& lowE, double& midE, double& highE)
{
    constexpr int order = 13, fftSize = 1 << order;           // 8192
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) fftSize * 2, 0.0f);
    const int start = (int) (0.5 * sr);
    const float* l = buf.getReadPointer (0);
    const float* r = buf.getReadPointer (1);
    for (int i = 0; i < fftSize && start + i < buf.getNumSamples(); ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (ydc::kTwoPi * (float) i / (float) fftSize); // Hann
        data[(size_t) i] = 0.5f * (l[start + i] + r[start + i]) * w;
    }
    fft.performRealOnlyForwardTransform (data.data());

    double num = 0.0, den = 0.0;
    centroidHz = 0.0; lowE = midE = highE = 0.0;
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const double re = data[(size_t) (2 * k)], im = data[(size_t) (2 * k + 1)];
        const double mag = std::sqrt (re * re + im * im);
        const double f = (double) k * sr / fftSize;
        num += f * mag;
        den += mag;
        const double e = mag * mag;
        if (f < 200.0)       lowE  += e;
        else if (f < 2000.0) midE  += e;
        else                 highE += e;
    }
    centroidHz = den > 0.0 ? num / den : 0.0;
}

static int runBaseline (const juce::File& dir, bool write, float tolerance)
{
    if (write)
        dir.createDirectory();
    if (! dir.isDirectory())
    {
        std::cout << "[FAIL] baseline directory missing: " << dir.getFullPathName() << std::endl;
        return 1;
    }

    // collect the factory preset list once
    juce::StringArray names;
    {
        YDCoreAudioProcessor proc;
        for (const auto& info : proc.getPresetManager().getPresets())
            if (info.isFactory)
                names.add (info.name);
    }
    std::cout << (write ? "writing" : "checking") << " baseline for "
              << names.size() << " factory presets" << std::endl;

    juce::WavAudioFormat wavFormat;
    juce::DynamicObject::Ptr statsRoot = new juce::DynamicObject();
    int bad = 0, done = 0, skipped = 0, compared = 0;
    float worstDiff = 0.0f;
    juce::String worstTag;

    for (const auto& name : names)
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            double wall = 0.0;
            auto rendered = renderBaselineScenario (name, sr, wall);
            const auto stem = baselineFileStem (name, sr);
            const auto wavFile = dir.getChildFile (stem + ".wav");

            if (write)
            {
                wavFile.deleteFile();
                std::unique_ptr<juce::OutputStream> stream (wavFile.createOutputStream());
                if (stream != nullptr)
                {
                    auto writer = wavFormat.createWriterFor (stream,
                        juce::AudioFormatWriterOptions{}
                            .withSampleRate (sr)
                            .withNumChannels (2)
                            .withBitsPerSample (32)
                            .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
                    if (writer != nullptr)
                        writer->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples());
                    else
                    {
                        std::cout << "[FAIL] cannot create WAV writer: " << stem << std::endl;
                        ++bad;
                    }
                }
                else
                {
                    std::cout << "[FAIL] cannot open for writing: " << stem << std::endl;
                    ++bad;
                }

                auto st = analyse (rendered);
                double centroid = 0.0, lowE = 0.0, midE = 0.0, highE = 0.0;
                baselineSpectralStats (rendered, sr, centroid, lowE, midE, highE);
                juce::DynamicObject::Ptr entry = new juce::DynamicObject();
                entry->setProperty ("peak", st.peak);
                entry->setProperty ("rms", st.rms);
                entry->setProperty ("centroidHz", centroid);
                entry->setProperty ("lowE", lowE);
                entry->setProperty ("midE", midE);
                entry->setProperty ("highE", highE);
                entry->setProperty ("renderSeconds", wall);
                statsRoot->setProperty (stem, juce::var (entry.get()));
            }
            else
            {
                if (! wavFile.existsAsFile())
                {
                    // presets newer than the recorded baseline have no reference —
                    // they are validated by the v1.2 suites instead
                    ++skipped;
                    ++done;
                    continue;
                }
                std::unique_ptr<juce::AudioFormatReader> reader (
                    wavFormat.createReaderFor (wavFile.createInputStream().release(), true));
                if (reader == nullptr)
                {
                    std::cout << "[FAIL] unreadable baseline wav: " << stem << std::endl;
                    ++bad;
                    continue;
                }
                juce::AudioBuffer<float> ref (2, (int) reader->lengthInSamples);
                reader->read (&ref, 0, (int) reader->lengthInSamples, 0, true, true);

                if (ref.getNumSamples() != rendered.getNumSamples())
                {
                    std::cout << "[FAIL] length mismatch: " << stem << std::endl;
                    ++bad;
                    continue;
                }
                float maxDiff = 0.0f;
                for (int c = 0; c < 2; ++c)
                {
                    const float* a = ref.getReadPointer (c);
                    const float* b = rendered.getReadPointer (c);
                    for (int i = 0; i < ref.getNumSamples(); ++i)
                        maxDiff = std::max (maxDiff, std::abs (a[i] - b[i]));
                }
                ++compared;
                if (maxDiff > worstDiff)
                {
                    worstDiff = maxDiff;
                    worstTag = stem;
                }
                if (maxDiff > tolerance)
                {
                    std::cout << "[FAIL] baseline deviation " << stem
                              << " maxDiff=" << maxDiff << " (tol " << tolerance << ")" << std::endl;
                    ++bad;
                }
            }
            ++done;
        }
        std::cout << "  [" << done << "/" << names.size() * 3 << "] " << name << std::endl;
    }

    if (write)
    {
        const auto statsFile = dir.getChildFile ("baseline_stats.json");
        statsFile.replaceWithText (juce::JSON::toString (juce::var (statsRoot.get()), false));
        std::cout << "stats -> " << statsFile.getFullPathName() << std::endl;
    }
    else
    {
        std::cout << "compared " << compared << " renders against the stored baseline";
        if (skipped > 0)
            std::cout << " (skipped " << skipped << " newer presets with no 1.1 reference)";
        std::cout << std::endl;
        std::cout << "worst deviation: " << worstDiff << " (" << worstTag << ")" << std::endl;
        if (compared < 153)
        {
            std::cout << "[FAIL] expected at least 153 legacy comparisons (51 presets x 3 rates), got "
                      << compared << std::endl;
            ++bad;
        }
    }
    std::cout << (bad == 0 ? "BASELINE OK" : "BASELINE FAILURES: " + juce::String (bad)) << std::endl;
    return bad == 0 ? 0 : 1;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 4 && juce::String (argv[1]) == "--baseline")
    {
        const juce::String mode (argv[2]);
        const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[3]));
        const float tol = argc >= 5 ? juce::String (argv[4]).getFloatValue() : 1.0e-4f;
        if (mode == "write" || mode == "check")
            return runBaseline (dir, mode == "write", tol);
        std::cout << "usage: --baseline write|check <dir> [tolerance]" << std::endl;
        return 1;
    }

    if (argc >= 3 && juce::String (argv[1]) == "--screenshot")
        return runScreenshot (juce::String (argv[2]), false);
    if (argc >= 3 && juce::String (argv[1]) == "--screenshots")
    {
        int w = 1200, h = 760;
        if (argc >= 4)
        {
            const juce::String size (argv[3]);
            w = size.upToFirstOccurrenceOf ("x", false, true).getIntValue();
            h = size.fromFirstOccurrenceOf ("x", false, true).getIntValue();
            if (w < 100 || h < 100) { w = 1200; h = 760; }
        }
        return runScreenshot (juce::String (argv[2]), true, w, h,
                              argc >= 5 ? juce::String (argv[4]) : juce::String ("Neon Bite"));
    }

    std::cout << "GLOBUS headless verification suite" << std::endl;

    for (double sr : { 44100.0, 48000.0 })
        for (int bs : { 32, 64, 256, 1024, 4096 })
            testBasicTone (sr, bs);

    testClickFreeEdges (44100.0);

    // polyphony regression suite
    for (double sr : { 44100.0, 48000.0 })
        for (int bs : { 32, 256, 4096 })
            testPolyOverlapBaseline (sr, bs);
    testPolyChordsAndRepeats();
    testSustainOverlap();
    testModesPrioritiesAndGlide();
    testVoiceStealingAtLimit();
    testPresetModeScanAndOverlap();

    testAllPresetsLoadAndSound();
    testStateRoundTrip();
    testRapidPresetSwitchDuringPlayback();
    testPolyAndMonoModes();
    testVoiceStress();
    testAutomationSweep();
    testArpeggiator();
    testRandomizerSuite();
    testParameterSystem();
    testSaveUserPreset();
    testV12ParameterContract();
    testV12StateMigration();
    testWavetableLibrary();
    testHqAliasingImprovement();
    testWavetableEngineBehaviour();
    testWavetableImport();
    testV12Filters();
    testV12EnvelopeCurves();
    testV12QualitySwitchingAndNewMods();
    testV12FxQualityAndGainStaging();
    testV12FactoryPresets();

    std::cout << "----------------------------------------" << std::endl;
    std::cout << checks << " checks, " << failures << " failures" << std::endl;
    return failures == 0 ? 0 : 1;
}
