// GLOBUS — the four synthesis pages behind the main tabs (OSC / FX / MATRIX /
// GLOBAL). Every control is bound to an existing APVTS parameter.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Controls.h"
#include "SectionsOsc.h"
#include "SectionsMod.h"
#include "SectionsFx.h"
#include "ModEditor.h"
#include "WavetableBrowser.h"
#include "../PluginProcessor.h"

namespace ydc
{
//==============================================================================
// OSC page: signal-flow layout — oscillators → sub/noise → filter, modulation
// editor beneath, keyboard at the bottom.
//==============================================================================
class OscPage : public juce::Component
{
public:
    explicit OscPage (YDCoreAudioProcessor& p)
        : osc1 (p, 1, &p.getPresetManager()),
          osc2 (p, 2),
          sub (p.getApvts()),
          noise (p.getApvts()),
          filter (p.getApvts(), &p.getPresetManager()),
          modEditor (p),
          keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard),
          browser1 (p, 1), browser2 (p, 2)
    {
        for (juce::Component* c : { (juce::Component*) &osc1, (juce::Component*) &osc2,
                                    (juce::Component*) &sub, (juce::Component*) &noise,
                                    (juce::Component*) &filter, (juce::Component*) &modEditor,
                                    (juce::Component*) &keyboard })
            addAndMakeVisible (*c);
        keyboard.setKeyWidth (24.0f);
        keyboard.setLowestVisibleKey (24);

        // wavetable browser overlays: hidden until opened; opening never changes the sound
        addChildComponent (browser1);
        addChildComponent (browser2);
        browser1.onClose = [this] { browser1.setVisible (false); };
        browser2.onClose = [this] { browser2.setVisible (false); };
        osc1.onOpenBrowser = [this] { browser2.setVisible (false); browser1.setVisible (true);
                                      browser1.toFront (true); };
        osc2.onOpenBrowser = [this] { browser1.setVisible (false); browser2.setVisible (true);
                                      browser2.toFront (true); };
        osc1.onStepWavetable = [this] (int d) { browser1.stepSelection (d); };
        osc2.onStepWavetable = [this] (int d) { browser2.stepSelection (d); };
    }

    void resized() override
    {
        // reference layout: 1200 x 696
        osc1.setBounds (8, 4, 590, 204);
        osc2.setBounds (604, 4, 588, 204);

        sub.setBounds (8, 214, 262, 152);
        noise.setBounds (274, 214, 324, 152);
        filter.setBounds (604, 214, 588, 152);

        modEditor.setBounds (8, 372, 1184, 268);

        keyboard.setBounds (8, 644, 1184, 46);

        browser1.setBounds (250, 90, 700, 430);
        browser2.setBounds (250, 90, 700, 430);
    }

private:
    OscSection osc1, osc2;
    SubSection sub;
    NoiseSection noise;
    FilterSection filter;
    ModEditor modEditor;
    juce::MidiKeyboardComponent keyboard;
    WavetableBrowser browser1, browser2;
};

//==============================================================================
// FX page: the real signal chain as one row of modules
//==============================================================================
/** One effect module in the chain: header with bypass + state, knob grid,
    live mix readout. State display uses text + illumination (not colour only). */
class FxModule : public juce::Component, private juce::Timer
{
public:
    FxModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& name, juce::Colour accentCol,
              const char* onId, std::vector<std::pair<juce::String, const char*>> knobDefs,
              const char* mixId = nullptr, const char* syncId = nullptr, const char* divId = nullptr)
        : title (name), accent (accentCol)
    {
        if (onId != nullptr)
        {
            on = std::make_unique<Toggle> ("");
            on->attach (apvts, onId);
            addAndMakeVisible (*on);
            onRaw = apvts.getRawParameterValue (onId);
        }
        if (syncId != nullptr && divId != nullptr)
        {
            sync = std::make_unique<Toggle> ("SYNC");
            sync->attach (apvts, syncId);
            addAndMakeVisible (*sync);
            div = std::make_unique<Selector> ("", false);
            div->attach (apvts, divId);
            addAndMakeVisible (*div);
        }
        for (auto& [label, id] : knobDefs)
        {
            auto k = std::make_unique<Knob> (label);
            k->setAccent (accentCol);
            k->attach (apvts, id);
            addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }
        if (mixId != nullptr)
            mixParam = apvts.getParameter (mixId);
        startTimerHz (8);
    }

    void paint (juce::Graphics& g) override
    {
        const bool active = onRaw == nullptr || onRaw->load() > 0.5f;
        auto b = getLocalBounds().toFloat().reduced (1.0f);

        g.setGradientFill (juce::ColourGradient (
            active ? theme::panel.brighter (0.05f) : theme::panel.darker (0.12f), 0, 0,
            active ? theme::panel : theme::bg.brighter (0.02f), 0, b.getBottom(), false));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (active ? accent.withAlpha (0.55f) : theme::outline);
        g.drawRoundedRectangle (b, 6.0f, active ? 1.3f : 1.0f);

        // header: title + state text + indicator lamp
        g.setColour (active ? theme::text : theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (11.5f, true));
        g.drawText (title, 8, 5, getWidth() - 16, 14, juce::Justification::centredLeft);

        const bool hasBypass = onRaw != nullptr;
        if (hasBypass)
        {
            g.setColour (active ? accent : theme::track);
            g.fillEllipse ((float) getWidth() - 40.0f, 26.0f, 8.0f, 8.0f);
            if (active)
            {
                g.setColour (accent.withAlpha (0.35f));
                g.fillEllipse ((float) getWidth() - 42.5f, 23.5f, 13.0f, 13.0f);
            }
            g.setColour (active ? accent : theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (9.5f, true));
            g.drawText (active ? "ON" : "BYPASSED", 8, 22, getWidth() - 52, 14, juce::Justification::centredLeft);
        }

        // live mix readout
        if (mixParam != nullptr)
        {
            g.setColour (active ? theme::textDim : theme::textDim.withAlpha (0.6f));
            g.setFont (LookAndFeelYD::uiFont (10.0f));
            g.drawText ("MIX " + mixParam->getText (mixParam->getValue(), 8),
                        8, getHeight() - 18, getWidth() - 16, 14, juce::Justification::centred);
        }
    }

    void resized() override
    {
        if (on != nullptr)
            on->setBounds (getWidth() - 26, 3, 22, 18);

        auto c = getLocalBounds().reduced (7).withTrimmedTop (38);
        if (mixParam != nullptr)
            c.removeFromBottom (16);

        if (sync != nullptr)
        {
            auto row = c.removeFromTop (22);
            sync->setBounds (row.removeFromLeft (58));
            row.removeFromLeft (4);
            div->setBounds (row);
            c.removeFromTop (2);
        }

        // knob grid: 2 columns, compact rows centred vertically
        const int rows = ((int) knobs.size() + 1) / 2;
        const int rh = juce::jmin (116, c.getHeight() / juce::jmax (1, rows));
        auto block = c.withSizeKeepingCentre (c.getWidth(), rh * rows);
        for (int r = 0; r < rows; ++r)
        {
            auto rowArea = block.removeFromTop (rh);
            const int inRow = juce::jmin (2, (int) knobs.size() - r * 2);
            auto cells = rowCells (rowArea, 2, 3);
            for (int i = 0; i < inRow; ++i)
                knobs[(size_t) (r * 2 + i)]->setBounds (cells[(size_t) i]);
        }
    }

private:
    void timerCallback() override
    {
        const bool active = onRaw == nullptr || onRaw->load() > 0.5f;
        if (active != lastActive)
        {
            lastActive = active;
            repaint();
        }
        else if (mixParam != nullptr && std::abs (mixParam->getValue() - lastMix) > 1.0e-3f)
        {
            lastMix = mixParam->getValue();
            repaint (0, getHeight() - 20, getWidth(), 20);
        }
    }

    juce::String title;
    juce::Colour accent;
    std::unique_ptr<Toggle> on, sync;
    std::unique_ptr<Selector> div;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::atomic<float>* onRaw = nullptr;
    juce::RangedAudioParameter* mixParam = nullptr;
    bool lastActive = true;
    float lastMix = -1.0f;
};

class FxPage : public juce::Component
{
public:
    explicit FxPage (YDCoreAudioProcessor& p)
        : fxLock ([&p] { return p.getPresetManager().isSectionLocked (PresetManager::LockFx); },
                  [&p] (bool v) { p.getPresetManager().setSectionLocked (PresetManager::LockFx, v); },
                  "FX")
    {
        auto& s = p.getApvts();
        using KD = std::vector<std::pair<juce::String, const char*>>;
        modules.push_back (std::make_unique<FxModule> (s, "DISTORTION", theme::warn, ids::distOn,
            KD { { "DRIVE", ids::distDrive }, { "TONE", ids::distTone }, { "MIX", ids::distMix } }, ids::distMix));
        modules.push_back (std::make_unique<FxModule> (s, "CHORUS", theme::accent, ids::chOn,
            KD { { "RATE", ids::chRate }, { "DEPTH", ids::chDepth }, { "WIDTH", ids::chWidth }, { "MIX", ids::chMix } }, ids::chMix));
        modules.push_back (std::make_unique<FxModule> (s, "DELAY", theme::accent, ids::dlyOn,
            KD { { "TIME", ids::dlyTime }, { "FEEDBACK", ids::dlyFb }, { "TONE", ids::dlyTone }, { "MIX", ids::dlyMix } },
            ids::dlyMix, ids::dlySync, ids::dlyDiv));
        modules.push_back (std::make_unique<FxModule> (s, "REVERB", theme::accent2, ids::rvOn,
            KD { { "SIZE", ids::rvSize }, { "DAMP", ids::rvDamp }, { "WIDTH", ids::rvWidth }, { "MIX", ids::rvMix } }, ids::rvMix));
        modules.push_back (std::make_unique<FxModule> (s, "EQUALIZER", theme::accent, ids::eqOn,
            KD { { "LOW", ids::eqLow }, { "MID", ids::eqMid }, { "HIGH", ids::eqHigh } }));
        modules.push_back (std::make_unique<FxModule> (s, "OUTPUT", theme::accent2, nullptr,
            KD { { "WIDTH", ids::stereoWidth }, { "MASTER", ids::masterLevel } }));
        for (auto& m : modules)
            addAndMakeVisible (*m);
        addAndMakeVisible (fxLock);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::text);
        g.setFont (LookAndFeelYD::uiFont (14.0f, true));
        g.drawText ("EFFECTS CHAIN", 16, 8, 300, 20, juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (11.0f));
        g.drawText ("engine output runs left to right through every active module",
                    16, 28, 800, 16, juce::Justification::centredLeft);

        // chain arrows between modules
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (16.0f));
        for (size_t i = 0; i + 1 < modules.size(); ++i)
        {
            const int x = modules[i]->getRight();
            const int w = modules[i + 1]->getX() - x;
            g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), x - 2, modules[i]->getY() + 60, w + 4, 20,
                        juce::Justification::centred);
        }

        if (! modules.empty())
        {
            g.setColour (theme::textDim.withAlpha (0.8f));
            g.setFont (LookAndFeelYD::uiFont (11.0f));
            g.drawText ("every module bypasses through a 10 ms crossfade - toggling an effect never clicks   |   "
                        "hover any knob for its exact value   |   padlock = keep FX settings during randomize",
                        16, modules[0]->getBottom() + 14, getWidth() - 32, 16, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        fxLock.setBounds (getWidth() - 34, 8, 22, 20);
        auto b = getLocalBounds().reduced (8, 0).withTrimmedTop (52).withTrimmedBottom (52);
        const int gap = 20;
        const int w = (b.getWidth() - gap * 5) / 6;
        for (auto& m : modules)
        {
            m->setBounds (b.removeFromLeft (w));
            b.removeFromLeft (gap);
        }
    }

private:
    std::vector<std::unique_ptr<FxModule>> modules;
    LockButton fxLock;
};

//==============================================================================
// MATRIX page
//==============================================================================
class MatrixRow : public juce::Component
{
public:
    MatrixRow (juce::AudioProcessorValueTreeState& apvts, int slotNum1Based)
        : slotNumber (slotNum1Based)
    {
        srcSel = std::make_unique<Selector> ("", false);
        dstSel = std::make_unique<Selector> ("", false);
        srcSel->attach (apvts, ids::slot (slotNumber, "Src"));
        dstSel->attach (apvts, ids::slot (slotNumber, "Dst"));
        addAndMakeVisible (*srcSel);
        addAndMakeVisible (*dstSel);

        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 18);
        amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, ids::slot (slotNumber, "Amt"), amount);
        amount.setDoubleClickReturnValue (true, 0.0);
        amount.setTooltip (getTooltip (ids::slot (slotNumber, "Amt")));
        addAndMakeVisible (amount);

        bipolar = std::make_unique<Toggle> ("BI");
        bipolar->attach (apvts, ids::slot (slotNumber, "Bipolar"));
        addAndMakeVisible (*bipolar);

        srcRaw = apvts.getRawParameterValue (ids::slot (slotNumber, "Src"));
        dstRaw = apvts.getRawParameterValue (ids::slot (slotNumber, "Dst"));
        amtRaw = apvts.getRawParameterValue (ids::slot (slotNumber, "Amt"));
    }

    bool computeActive() const
    {
        return srcRaw->load() > 0.5f && dstRaw->load() > 0.5f && std::abs (amtRaw->load()) > 0.001f;
    }

    void setActiveDisplay (bool a)
    {
        if (a != active)
        {
            active = a;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (active ? theme::panelLight.brighter (0.03f) : theme::panel);
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (active ? theme::accent2.withAlpha (0.55f) : theme::outline);
        g.drawRoundedRectangle (b, 5.0f, 1.0f);

        g.setColour (active ? theme::accent2 : theme::track);
        g.fillEllipse (12.0f, b.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (active ? theme::text : theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (13.0f, true));
        g.drawText (juce::String (slotNumber), 24, 0, 22, getHeight(), juce::Justification::centred);
        g.setFont (LookAndFeelYD::uiFont (10.0f, ! active));
        g.setColour (theme::textDim);
        g.drawText (active ? "ACTIVE" : "EMPTY", 8, getHeight() - 15, 52, 12, juce::Justification::centred);

        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (14.0f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), arrowX, 0, 22, getHeight(), juce::Justification::centred);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 5);
        b.removeFromLeft (54);                        // number + state zone
        srcSel->setBounds (b.removeFromLeft (208).withSizeKeepingCentre (208, 24));
        arrowX = b.getX();
        b.removeFromLeft (22);
        dstSel->setBounds (b.removeFromLeft (228).withSizeKeepingCentre (228, 24));
        b.removeFromLeft (12);
        bipolar->setBounds (b.removeFromRight (46).withSizeKeepingCentre (46, 20));
        b.removeFromRight (6);
        amount.setBounds (b.withSizeKeepingCentre (b.getWidth(), 26));
    }

private:
    int slotNumber;
    int arrowX = 0;
    bool active = false;
    std::unique_ptr<Selector> srcSel, dstSel;
    juce::Slider amount;
    std::unique_ptr<Toggle> bipolar;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::atomic<float> *srcRaw = nullptr, *dstRaw = nullptr, *amtRaw = nullptr;
};

class MatrixPage : public juce::Component, private juce::Timer
{
public:
    explicit MatrixPage (YDCoreAudioProcessor& p)
        : lfoMtxLock ([&p] { return p.getPresetManager().isSectionLocked (PresetManager::LockLfoMatrix); },
                      [&p] (bool v) { p.getPresetManager().setSectionLocked (PresetManager::LockLfoMatrix, v); },
                      "LFO and matrix")
    {
        for (int i = 0; i < 8; ++i)
        {
            rows[(size_t) i] = std::make_unique<MatrixRow> (p.getApvts(), i + 1);
            addAndMakeVisible (*rows[(size_t) i]);
        }
        addAndMakeVisible (lfoMtxLock);
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::text);
        g.setFont (LookAndFeelYD::uiFont (14.0f, true));
        g.drawText ("MODULATION MATRIX", 16, 8, 400, 20, juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (11.0f));
        g.drawText ("8 slots  |  velocity, wheel, aftertouch, envelopes, LFOs, random and pitch bend "
                    "\xe2\x86\x92  pitch, level, pan, PW, filter, amp, LFO rates, FX mix, width  |  BI = bipolar source",
                    16, 28, 1050, 16, juce::Justification::centredLeft);
    }

    void resized() override
    {
        lfoMtxLock.setBounds (getWidth() - 34, 8, 22, 20);
        auto b = getLocalBounds().reduced (8, 0).withTrimmedTop (50).withTrimmedBottom (10);
        const int rowH = juce::jmin (76, b.getHeight() / 8);
        for (auto& r : rows)
            r->setBounds (b.removeFromTop (rowH).reduced (0, 3));
    }

private:
    void timerCallback() override
    {
        for (auto& r : rows)
            r->setActiveDisplay (r->computeActive());
    }

    std::array<std::unique_ptr<MatrixRow>, 8> rows;
    LockButton lfoMtxLock;
};

//==============================================================================
// GLOBAL page
//==============================================================================
/** Live status panel: MIDI, sustain pedal, CPU and voice meters, identity. */
class StatusPanel : public SectionPanel, private juce::Timer
{
public:
    explicit StatusPanel (YDCoreAudioProcessor& p)
        : SectionPanel ("STATUS", theme::accent), processor (p)
    {
        startTimerHz (8);
    }

    void paint (juce::Graphics& g) override
    {
        SectionPanel::paint (g);
        auto c = content().reduced (10, 4);
        const int rowH = juce::jmax (26, c.getHeight() / 7);

        auto drawLed = [&g] (juce::Rectangle<int> row, const juce::String& text, bool lit, juce::Colour col)
        {
            g.setColour (lit ? col : theme::track);
            g.fillEllipse ((float) row.getX(), (float) row.getCentreY() - 5.0f, 10.0f, 10.0f);
            g.setColour (theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (12.0f));
            g.drawText (text, row.withTrimmedLeft (20), juce::Justification::centredLeft);
        };

        auto row = c.removeFromTop (rowH);
        drawLed (row, midiLit ? "MIDI input: receiving" : "MIDI input: idle", midiLit, theme::accent);
        row = c.removeFromTop (rowH);
        drawLed (row, pedal ? "Sustain pedal: down" : "Sustain pedal: up", pedal, theme::accent2);
        row = c.removeFromTop (rowH);
        const char* modeNames[] = { "Poly", "Mono", "Legato" };
        drawLed (row, juce::String ("Play mode: ") + modeNames[juce::jlimit (0, 2, mode)], true,
                 mode == 0 ? theme::accent : theme::warn);
        row = c.removeFromTop (rowH);
        drawLed (row, arpOn ? "Arpeggiator: running" : "Arpeggiator: off", arpOn, theme::warn);

        auto drawBar = [&g] (juce::Rectangle<int> r2, const juce::String& text, float frac, juce::Colour col)
        {
            g.setColour (theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (12.0f));
            g.drawText (text, r2.removeFromLeft (104), juce::Justification::centredLeft);
            auto bar = r2.reduced (0, 8).toFloat();
            g.setColour (theme::bg);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (col.withAlpha (0.85f));
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, frac))), 3.0f);
            g.setColour (theme::outline);
            g.drawRoundedRectangle (bar, 3.0f, 1.0f);
        };
        row = c.removeFromTop (rowH);
        drawBar (row, "CPU " + juce::String (juce::roundToInt (cpu * 100.0f)) + "%", cpu, theme::accent);
        row = c.removeFromTop (rowH);
        drawBar (row, "Voices " + juce::String (voices) + "/32", (float) voices / 32.0f, theme::accent2);

        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (10.5f));
        g.drawText ("GLOBUS 1.2.0  |  Ninth Parallel Audio  |  VST3 + Standalone",
                    c.removeFromTop (rowH), juce::Justification::centredLeft);
    }

private:
    void timerCallback() override
    {
        const bool m = processor.consumeMidiActivity();
        const bool sPedal = processor.getEngine().isPedalDown();
        const float sCpu = processor.getCpuLoad();
        const int v = processor.getEngine().getActiveVoiceCount();
        const int md = (int) processor.getApvts().getRawParameterValue ("playMode")->load();
        const bool arp = processor.getApvts().getRawParameterValue ("arpOn")->load() > 0.5f;
        if (m != midiLit || sPedal != pedal || std::abs (sCpu - cpu) > 0.005f || v != voices
            || md != mode || arp != arpOn)
        {
            midiLit = m; pedal = sPedal; cpu = sCpu; voices = v; mode = md; arpOn = arp;
            repaint();
        }
    }

    YDCoreAudioProcessor& processor;
    bool midiLit = false, pedal = false, arpOn = false;
    float cpu = 0.0f;
    int voices = 0, mode = 0;
};

/** v1.2 — global quality mode selector with an honest per-mode description. */
class QualitySection : public SectionPanel,
                       private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit QualitySection (juce::AudioProcessorValueTreeState& apvts)
        : SectionPanel ("QUALITY", theme::accent), state (apvts), mode ("", false)
    {
        mode.attach (apvts, ids::qualityMode);
        addAndMakeVisible (mode);
        raw = apvts.getRawParameterValue (ids::qualityMode);
        apvts.addParameterListener (ids::qualityMode, this);
    }

    ~QualitySection() override { state.removeParameterListener (ids::qualityMode, this); }

    void paint (juce::Graphics& g) override
    {
        SectionPanel::paint (g);
        static const char* info[4] =
        {
            "Exact 1.1 processing everywhere. Old presets and projects sound identical. Zero added latency.",
            "New engines at the lowest CPU cost: improved interpolation, no oversampling. Zero added latency.",
            "Default for new sounds: 2x oversampled distortion, oversampled ladder/OTA filters, FDN reverb, smoothed EQ. Reports 2 samples of latency.",
            "Maximum real-time quality: 4x distortion oversampling, everything from HIGH. Reports 4 samples of latency."
        };
        const int idx = juce::jlimit (0, 3, raw != nullptr ? (int) raw->load() : 0);
        auto c = content().withTrimmedTop (30).reduced (6, 0);
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (11.0f));
        g.drawFittedText (info[idx], c, juce::Justification::topLeft, 4);
    }

    void resized() override
    {
        auto c = content();
        mode.setBounds (c.removeFromTop (24).removeFromLeft (200));
    }

private:
    void parameterChanged (const juce::String&, float) override
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<QualitySection> (this)]
        {
            if (safe != nullptr)
                safe->repaint();
        });
    }

    juce::AudioProcessorValueTreeState& state;
    Selector mode;
    std::atomic<float>* raw = nullptr;
};

class GlobalPage : public juce::Component
{
public:
    explicit GlobalPage (YDCoreAudioProcessor& p)
        : play (p.getApvts()),
          arp (p.getApvts()),
          output (p.getApvts(), "OUTPUT", theme::accent2, nullptr,
                  std::vector<std::pair<juce::String, const char*>> {
                      { "MASTER", ids::masterLevel }, { "STEREO WIDTH", ids::stereoWidth } }, nullptr),
          quality (p.getApvts()),
          status (p)
    {
        addAndMakeVisible (play);
        addAndMakeVisible (arp);
        addAndMakeVisible (output);
        addAndMakeVisible (quality);
        addAndMakeVisible (status);
    }

    void resized() override
    {
        play.setBounds (8, 4, 590, 336);
        arp.setBounds (604, 4, 588, 336);
        output.setBounds (8, 344, 590, 226);
        quality.setBounds (8, 576, 590, 108);
        status.setBounds (604, 344, 588, 340);
    }

private:
    GlobalSection play;
    ArpSection arp;
    FxModule output;
    QualitySection quality;
    StatusPanel status;
};

} // namespace ydc
