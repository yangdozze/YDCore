// GLOBUS — headless verification suite. Renders the real processor offline:
// no audio device, no window. Exit code 0 = all checks passed.
#include <juce_events/juce_events.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

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
    const int kRuns = 1002;

    pm.initPatch();
    for (int i = 0; i < kRuns; ++i)
    {
        const auto strength = strengths[i % 3];
        const auto before = snapshotAll();
        const float masterBefore = apvts.getRawParameterValue ("masterLevel")->load();
        const float modeBefore   = apvts.getRawParameterValue ("playMode")->load();

        pm.randomizePatch (strength);

        if (allValid()) ++validCount;
        if (juce::exactlyEqual (apvts.getRawParameterValue ("masterLevel")->load(), masterBefore)
            && juce::exactlyEqual (apvts.getRawParameterValue ("playMode")->load(), modeBefore))
            ++protectedOk;

        // one-step undo restores the exact previous sound
        pm.undoRandomize();
        const auto after = snapshotAll();
        bool same = after.size() == before.size();
        if (same)
            for (size_t k = 0; k < after.size(); ++k)
                if (std::abs (after[k] - before[k]) > 1.0e-5f) { same = false; break; }
        if (same) ++undoOk;

        // re-apply (leave the random patch in place) and occasionally render it
        pm.randomizePatch (strength);
        if (i % 40 == 0)
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
    expect (undoOk == kRuns, "randomizer: undo restores exactly (" + juce::String (undoOk) + "/" + juce::String (kRuns) + ")");
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
    --screenshots <dir> [WxH]: dump every tab at the given window size
    (default 1200x760). Requires a display / Xvfb; also validates that the
    editor constructs. */
static int runScreenshot (const juce::String& outPath, bool allTabs, int w = 1200, int h = 760)
{
    YDCoreAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    loadPresetByName (proc, "Neon Bite");

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

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

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
        return runScreenshot (juce::String (argv[2]), true, w, h);
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

    std::cout << "----------------------------------------" << std::endl;
    std::cout << checks << " checks, " << failures << " failures" << std::endl;
    return failures == 0 ? 0 : 1;
}
