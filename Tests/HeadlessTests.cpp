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
    --screenshots <dir>: dump every tab as tab0_osc.png .. tab4_presets.png.
    Requires a display / Xvfb; also validates that the editor constructs. */
static int runScreenshot (const juce::String& outPath, bool allTabs)
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
    editor->setSize (1200, 760);

    int failures2 = 0;
    if (allTabs)
    {
        const char* tabNames[] = { "osc", "fx", "matrix", "global", "presets" };
        juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (outPath);
        dir.createDirectory();
        for (int i = 0; i < YDCoreAudioProcessorEditor::kNumTabs; ++i)
        {
            editor->setActiveTab (i);
            const auto f = dir.getChildFile ("tab" + juce::String (i) + "_" + tabNames[i] + ".png");
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
        return runScreenshot (juce::String (argv[2]), true);

    std::cout << "GLOBUS headless verification suite" << std::endl;

    for (double sr : { 44100.0, 48000.0 })
        for (int bs : { 32, 64, 256, 1024, 4096 })
            testBasicTone (sr, bs);

    testClickFreeEdges (44100.0);
    testAllPresetsLoadAndSound();
    testStateRoundTrip();
    testRapidPresetSwitchDuringPlayback();
    testPolyAndMonoModes();
    testVoiceStress();
    testAutomationSweep();
    testArpeggiator();
    testParameterSystem();
    testSaveUserPreset();

    std::cout << "----------------------------------------" << std::endl;
    std::cout << checks << " checks, " << failures << " failures" << std::endl;
    return failures == 0 ? 0 : 1;
}
