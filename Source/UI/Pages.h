// GLOBUS — the four synthesis pages behind the main tabs (OSC / FX / MATRIX /
// GLOBAL). Every control is bound to an existing APVTS parameter.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Controls.h"
#include "SectionsOsc.h"
#include "SectionsMod.h"
#include "SectionsFx.h"
#include "../PluginProcessor.h"

namespace ydc
{
//==============================================================================
// OSC page: oscillators, sub/noise, filter, envelopes, LFOs, keyboard
//==============================================================================
class OscPage : public juce::Component
{
public:
    explicit OscPage (YDCoreAudioProcessor& p)
        : osc1 (p.getApvts(), 1),
          osc2 (p.getApvts(), 2),
          subNoise (p.getApvts()),
          lfo1 (p.getApvts(), 1),
          lfo2 (p.getApvts(), 2),
          filter (p.getApvts()),
          ampEnv (p.getApvts(), "AMP ENV", ids::ampA, ids::ampD, ids::ampS, ids::ampR, theme::accent),
          filterEnv (p.getApvts(), "FILTER ENV", ids::filA, ids::filD, ids::filS, ids::filR, theme::accent2),
          modEnv (p.getApvts()),
          keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        for (juce::Component* c : { (juce::Component*) &osc1, (juce::Component*) &osc2,
                                    (juce::Component*) &subNoise, (juce::Component*) &lfo1,
                                    (juce::Component*) &lfo2, (juce::Component*) &filter,
                                    (juce::Component*) &ampEnv, (juce::Component*) &filterEnv,
                                    (juce::Component*) &modEnv, (juce::Component*) &keyboard })
            addAndMakeVisible (*c);
        keyboard.setKeyWidth (26.0f);
        keyboard.setLowestVisibleKey (24);
    }

    void resized() override
    {
        // reference layout: 1200 x 696
        osc1.setBounds (8, 4, 590, 168);
        osc2.setBounds (8, 176, 590, 168);
        subNoise.setBounds (8, 348, 590, 84);
        lfo1.setBounds (8, 436, 590, 106);
        lfo2.setBounds (8, 546, 590, 106);

        filter.setBounds (602, 4, 590, 168);
        ampEnv.setBounds (602, 176, 590, 160);
        filterEnv.setBounds (602, 340, 590, 160);
        modEnv.setBounds (602, 504, 590, 148);

        keyboard.setBounds (8, 656, 1184, 36);
    }

private:
    OscSection osc1, osc2;
    SubNoiseSection subNoise;
    LfoSection lfo1, lfo2;
    FilterSection filter;
    EnvSection ampEnv, filterEnv;
    ModEnvSection modEnv;
    juce::MidiKeyboardComponent keyboard;
};

//==============================================================================
// FX page
//==============================================================================
/** Large effect panel: header with bypass, one generous knob row,
    optional sync toggle + division selector (delay). */
class FxBigPanel : public SectionPanel
{
public:
    FxBigPanel (juce::AudioProcessorValueTreeState& apvts, const juce::String& panelTitle, juce::Colour accent,
                const char* onId, std::vector<std::pair<juce::String, const char*>> knobDefs,
                const char* syncId = nullptr, const char* divId = nullptr)
        : SectionPanel (panelTitle, accent)
    {
        if (onId != nullptr)
        {
            on = std::make_unique<Toggle> ("ON");
            on->attach (apvts, onId);
            addAndMakeVisible (*on);
        }
        for (auto& [label, id] : knobDefs)
        {
            auto k = std::make_unique<Knob> (label);
            k->setAccent (accent);
            k->attach (apvts, id);
            addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }
        if (syncId != nullptr && divId != nullptr)
        {
            sync = std::make_unique<Toggle> ("SYNC");
            sync->attach (apvts, syncId);
            addAndMakeVisible (*sync);
            div = std::make_unique<Selector> ("DIVISION");
            div->attach (apvts, divId);
            addAndMakeVisible (*div);
        }
    }

    void resized() override
    {
        auto c = content().withTrimmedTop (4);
        c = c.withSizeKeepingCentre (c.getWidth(), juce::jmin (c.getHeight(), 150));
        if (on != nullptr)
            on->setBounds (getWidth() - 70, 4, 56, 20);
        if (sync != nullptr && div != nullptr)
        {
            auto col = c.removeFromLeft (104);
            sync->setBounds (col.removeFromTop (30).reduced (0, 4));
            div->setBounds (col.removeFromTop (44));
            c.removeFromLeft (8);
        }
        auto cells = rowCells (c.reduced (10, 0), (int) knobs.size(), 6);
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (cells[i]);
    }

private:
    std::unique_ptr<Toggle> on, sync;
    std::unique_ptr<Selector> div;
    std::vector<std::unique_ptr<Knob>> knobs;
};

class FxPage : public juce::Component
{
public:
    explicit FxPage (YDCoreAudioProcessor& p)
    {
        auto& s = p.getApvts();
        panels.push_back (std::make_unique<FxBigPanel> (s, "DISTORTION", theme::warn, ids::distOn,
            std::vector<std::pair<juce::String, const char*>> {
                { "DRIVE", ids::distDrive }, { "TONE", ids::distTone }, { "MIX", ids::distMix } }));
        panels.push_back (std::make_unique<FxBigPanel> (s, "CHORUS", theme::accent, ids::chOn,
            std::vector<std::pair<juce::String, const char*>> {
                { "RATE", ids::chRate }, { "DEPTH", ids::chDepth }, { "WIDTH", ids::chWidth }, { "MIX", ids::chMix } }));
        panels.push_back (std::make_unique<FxBigPanel> (s, "DELAY", theme::accent, ids::dlyOn,
            std::vector<std::pair<juce::String, const char*>> {
                { "TIME", ids::dlyTime }, { "FEEDBACK", ids::dlyFb }, { "TONE", ids::dlyTone }, { "MIX", ids::dlyMix } },
            ids::dlySync, ids::dlyDiv));
        panels.push_back (std::make_unique<FxBigPanel> (s, "REVERB", theme::accent2, ids::rvOn,
            std::vector<std::pair<juce::String, const char*>> {
                { "SIZE", ids::rvSize }, { "DAMP", ids::rvDamp }, { "WIDTH", ids::rvWidth }, { "MIX", ids::rvMix } }));
        panels.push_back (std::make_unique<FxBigPanel> (s, "EQUALIZER", theme::accent, ids::eqOn,
            std::vector<std::pair<juce::String, const char*>> {
                { "LOW 120Hz", ids::eqLow }, { "MID 1kHz", ids::eqMid }, { "HIGH 8kHz", ids::eqHigh } }));
        panels.push_back (std::make_unique<FxBigPanel> (s, "OUTPUT", theme::accent2, nullptr,
            std::vector<std::pair<juce::String, const char*>> {
                { "STEREO WIDTH", ids::stereoWidth }, { "MASTER", ids::masterLevel } }));
        for (auto& pnl : panels)
            addAndMakeVisible (*pnl);
    }

    void resized() override
    {
        const int colX[2] = { 8, 602 };
        const int colW = 590;
        const int rowY[3] = { 4, 236, 468 };
        const int rowH = 224;
        for (size_t i = 0; i < panels.size(); ++i)
            panels[i]->setBounds (colX[i % 2], rowY[i / 2], colW, rowH);
    }

private:
    std::vector<std::unique_ptr<FxBigPanel>> panels;
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
        srcSel = std::make_unique<Selector> ("SOURCE");
        dstSel = std::make_unique<Selector> ("DESTINATION");
        srcSel->attach (apvts, ids::slot (slotNumber, "Src"));
        dstSel->attach (apvts, ids::slot (slotNumber, "Dst"));
        addAndMakeVisible (*srcSel);
        addAndMakeVisible (*dstSel);

        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
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

        // slot number + active LED
        g.setColour (active ? theme::accent2 : theme::track);
        g.fillEllipse (14.0f, b.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (active ? theme::text : theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (14.0f, true));
        g.drawText (juce::String (slotNumber), 26, 0, 26, getHeight(), juce::Justification::centred);

        // arrow between source and destination
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (14.0f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), arrowX, 0, 24, getHeight(), juce::Justification::centred);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 6);
        b.removeFromLeft (48);                        // number zone
        srcSel->setBounds (b.removeFromLeft (218).withTrimmedTop (2));
        arrowX = b.getX() + 2;
        b.removeFromLeft (26);
        dstSel->setBounds (b.removeFromLeft (238).withTrimmedTop (2));
        b.removeFromLeft (14);
        bipolar->setBounds (b.removeFromRight (52));
        b.removeFromRight (6);
        amount.setBounds (b.withTrimmedTop (10));
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
    {
        for (int i = 0; i < 8; ++i)
        {
            rows[(size_t) i] = std::make_unique<MatrixRow> (p.getApvts(), i + 1);
            addAndMakeVisible (*rows[(size_t) i]);
        }
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::text);
        g.setFont (LookAndFeelYD::uiFont (15.0f, true));
        g.drawText ("MODULATION MATRIX", 16, 8, 400, 22, juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (11.5f));
        g.drawText ("8 slots  |  route velocity, wheel, aftertouch, envelopes, LFOs, random and pitch bend "
                    "to pitch, level, pan, PW, filter, amp, LFO rates, FX mix and width",
                    16, 30, 1000, 16, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 0).withTrimmedTop (54).withTrimmedBottom (8);
        const int rowH = b.getHeight() / 8;
        for (auto& r : rows)
        {
            r->setBounds (b.removeFromTop (rowH).reduced (0, 3));
        }
    }

private:
    void timerCallback() override
    {
        for (auto& r : rows)
            r->setActiveDisplay (r->computeActive());
    }

    std::array<std::unique_ptr<MatrixRow>, 8> rows;
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
        auto c = content().reduced (10, 6);
        const int rowH = 34;

        auto drawLed = [&g] (juce::Rectangle<int> row, const juce::String& text, bool lit, juce::Colour col)
        {
            g.setColour (lit ? col : theme::track);
            g.fillEllipse ((float) row.getX(), (float) row.getCentreY() - 5.0f, 10.0f, 10.0f);
            g.setColour (theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (12.5f));
            g.drawText (text, row.withTrimmedLeft (20), juce::Justification::centredLeft);
        };

        auto row = c.removeFromTop (rowH);
        drawLed (row, midiLit ? "MIDI input: receiving" : "MIDI input: idle", midiLit, theme::accent);

        row = c.removeFromTop (rowH);
        drawLed (row, pedal ? "Sustain pedal: down" : "Sustain pedal: up", pedal, theme::accent2);

        auto drawBar = [&g] (juce::Rectangle<int> r2, const juce::String& text, float frac, juce::Colour col)
        {
            g.setColour (theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (12.5f));
            g.drawText (text, r2.removeFromLeft (110), juce::Justification::centredLeft);
            auto bar = r2.reduced (0, 9).toFloat();
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

        c.removeFromTop (6);
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (11.0f));
        g.drawText ("GLOBUS 1.1.0  -  Ninth Parallel Audio", c.removeFromTop (18), juce::Justification::centredLeft);
        g.drawText ("Polyphonic workstation synthesizer  |  VST3 + Standalone", c.removeFromTop (16), juce::Justification::centredLeft);
    }

private:
    void timerCallback() override
    {
        const bool m = processor.consumeMidiActivity();
        const bool sPedal = processor.getEngine().isPedalDown();
        const float sCpu = processor.getCpuLoad();
        const int v = processor.getEngine().getActiveVoiceCount();
        if (m != midiLit || sPedal != pedal || std::abs (sCpu - cpu) > 0.005f || v != voices)
        {
            midiLit = m; pedal = sPedal; cpu = sCpu; voices = v;
            repaint();
        }
    }

    YDCoreAudioProcessor& processor;
    bool midiLit = false, pedal = false;
    float cpu = 0.0f;
    int voices = 0;
};

class GlobalPage : public juce::Component
{
public:
    explicit GlobalPage (YDCoreAudioProcessor& p)
        : play (p.getApvts()),
          arp (p.getApvts()),
          output (p.getApvts(), "OUTPUT", theme::accent2, nullptr,
                  std::vector<std::pair<juce::String, const char*>> {
                      { "MASTER", ids::masterLevel }, { "STEREO WIDTH", ids::stereoWidth } }),
          status (p)
    {
        addAndMakeVisible (play);
        addAndMakeVisible (arp);
        addAndMakeVisible (output);
        addAndMakeVisible (status);
    }

    void resized() override
    {
        play.setBounds (8, 4, 590, 336);
        arp.setBounds (602, 4, 590, 336);
        output.setBounds (8, 348, 590, 336);
        status.setBounds (602, 348, 590, 336);
    }

private:
    GlobalSection play;
    ArpSection arp;
    FxBigPanel output;
    StatusPanel status;
};

} // namespace ydc
