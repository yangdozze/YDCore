// GLOBUS — oscillator, sub & noise section components (OSC tab).
#pragma once
#include "Controls.h"
#include "WaveformDisplay.h"
#include "../Presets/PresetManager.h"

namespace ydc
{
class OscSection : public SectionPanel
{
public:
    OscSection (juce::AudioProcessorValueTreeState& apvts, int oscNum, PresetManager* pmForLock = nullptr)
        : SectionPanel ("OSCILLATOR " + juce::String (oscNum),
                        oscNum == 1 ? theme::accent : theme::accent2),
          on ("ON"), wave ("", false),
          oct ("OCT"), semi ("SEMI"), fine ("FINE"), level ("LEVEL"), pan ("PAN"), pw ("PW"),
          uni ("UNISON"), det ("DETUNE"), spread ("SPREAD"), drift ("DRIFT"), phase ("PHASE"),
          randPhase ("RND PH"),
          display (apvts, oscNum, oscNum == 1 ? theme::accent : theme::accent2)
    {
        const auto accent = oscNum == 1 ? theme::accent : theme::accent2;
        for (Knob* k : { &oct, &semi, &fine, &level, &pan, &pw, &uni, &det, &spread, &drift, &phase })
        {
            k->setAccent (accent);
            addAndMakeVisible (*k);
        }
        addAndMakeVisible (on);
        addAndMakeVisible (wave);
        addAndMakeVisible (randPhase);
        addAndMakeVisible (display);

        if (pmForLock != nullptr)
        {
            lock = std::make_unique<LockButton> (
                [pmForLock] { return pmForLock->isSectionLocked (PresetManager::LockOsc); },
                [pmForLock] (bool v) { pmForLock->setSectionLocked (PresetManager::LockOsc, v); },
                "oscillator/sub/noise");
            addAndMakeVisible (*lock);
        }

        on.attach (apvts, ids::osc (oscNum, "On"));
        wave.attach (apvts, ids::osc (oscNum, "Wave"));
        oct.attach (apvts, ids::osc (oscNum, "Oct"));
        semi.attach (apvts, ids::osc (oscNum, "Semi"));
        fine.attach (apvts, ids::osc (oscNum, "Fine"));
        level.attach (apvts, ids::osc (oscNum, "Level"));
        pan.attach (apvts, ids::osc (oscNum, "Pan"));
        pw.attach (apvts, ids::osc (oscNum, "PW"));
        uni.attach (apvts, ids::osc (oscNum, "UniCount"));
        det.attach (apvts, ids::osc (oscNum, "UniDetune"));
        spread.attach (apvts, ids::osc (oscNum, "UniSpread"));
        drift.attach (apvts, ids::osc (oscNum, "Drift"));
        phase.attach (apvts, ids::osc (oscNum, "Phase"));
        randPhase.attach (apvts, ids::osc (oscNum, "RandPhase"));
    }

    void resized() override
    {
        auto c = content();
        // header row: ON, wave selector, random phase, optional lock
        on.setBounds (118, 2, 48, 18);
        wave.setBounds (170, 1, 112, 20);
        randPhase.setBounds (288, 2, 70, 18);
        if (lock != nullptr)
            lock->setBounds (getWidth() - 26, 2, 20, 18);

        auto body = c.withTrimmedTop (2);
        display.setBounds (body.removeFromRight (150));
        body.removeFromRight (5);

        const int rowH = body.getHeight() / 2;
        auto row1 = body.removeFromTop (rowH);
        Knob* top[]    = { &oct, &semi, &fine, &level, &pan, &pw };
        Knob* bottom[] = { &uni, &det, &spread, &drift, &phase };
        auto cells1 = rowCells (row1, 6, 3);
        for (size_t i = 0; i < 6; ++i)
            top[i]->setBounds (cells1[i]);
        auto cells2 = rowCells (body, 6, 3);
        for (size_t i = 0; i < 5; ++i)
            bottom[i]->setBounds (cells2[i]);
    }

private:
    Toggle on;
    Selector wave;
    Knob oct, semi, fine, level, pan, pw, uni, det, spread, drift, phase;
    Toggle randPhase;
    WaveformDisplay display;
    std::unique_ptr<LockButton> lock;
};

//==============================================================================
class SubSection : public SectionPanel
{
public:
    explicit SubSection (juce::AudioProcessorValueTreeState& apvts)
        : SectionPanel ("SUB"),
          on ("ON"), wave ("", false), level ("LEVEL"), pan ("PAN")
    {
        addAndMakeVisible (on);
        addAndMakeVisible (wave);
        addAndMakeVisible (level);
        addAndMakeVisible (pan);
        on.attach (apvts, ids::subOn);
        wave.attach (apvts, ids::subWave);
        level.attach (apvts, ids::subLevel);
        pan.attach (apvts, ids::subPan);
    }

    void resized() override
    {
        auto c = content();
        on.setBounds (52, 2, 46, 18);
        wave.setBounds (c.getX(), c.getY() + c.getHeight() / 2 - 11, 82, 22);
        auto knobs = c.withTrimmedLeft (88);
        auto cells = rowCells (knobs, 2, 3);
        level.setBounds (cells[0]);
        pan.setBounds (cells[1]);
    }

private:
    Toggle on;
    Selector wave;
    Knob level, pan;
};

//==============================================================================
class NoiseSection : public SectionPanel
{
public:
    explicit NoiseSection (juce::AudioProcessorValueTreeState& apvts)
        : SectionPanel ("NOISE", theme::accent2),
          type ("", false), level ("LEVEL"), tone ("TONE"), pan ("PAN")
    {
        addAndMakeVisible (type);
        for (Knob* k : { &level, &tone, &pan })
        {
            k->setAccent (theme::accent2);
            addAndMakeVisible (*k);
        }
        type.attach (apvts, ids::noiseType);
        level.attach (apvts, ids::noiseLevel);
        tone.attach (apvts, ids::noiseTone);
        pan.attach (apvts, ids::noisePan);
    }

    void resized() override
    {
        auto c = content();
        type.setBounds (c.getX(), c.getY() + c.getHeight() / 2 - 11, 78, 22);
        auto knobs = c.withTrimmedLeft (84);
        auto cells = rowCells (knobs, 3, 3);
        level.setBounds (cells[0]);
        tone.setBounds (cells[1]);
        pan.setBounds (cells[2]);
    }

private:
    Selector type;
    Knob level, tone, pan;
};

} // namespace ydc
