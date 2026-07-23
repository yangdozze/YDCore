// YD Core — oscillator, sub & noise section components.
#pragma once
#include "Controls.h"

namespace ydc
{
class OscSection : public SectionPanel
{
public:
    OscSection (juce::AudioProcessorValueTreeState& apvts, int oscNum)
        : SectionPanel ("OSCILLATOR " + juce::String (oscNum),
                        oscNum == 1 ? theme::accent : theme::accent2),
          on ("ON"), wave ("", false),
          oct ("OCT"), semi ("SEMI"), fine ("FINE"), level ("LEVEL"), pan ("PAN"), pw ("PW"),
          uni ("UNISON"), det ("DETUNE"), spread ("SPREAD"), drift ("DRIFT"), phase ("PHASE"),
          randPhase ("RND PH")
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
        // header row (next to the panel title)
        on.setBounds (150, 3, 52, 20);
        wave.setBounds (205, 3, 110, 20);
        randPhase.setBounds (322, 3, 70, 20);

        auto cells = rowCells (c.withTrimmedTop (6), 11, 3);
        Knob* knobs[] = { &oct, &semi, &fine, &level, &pan, &pw, &uni, &det, &spread, &drift, &phase };
        for (size_t i = 0; i < 11; ++i)
            knobs[i]->setBounds (cells[i]);
    }

private:
    Toggle on;
    Selector wave;
    Knob oct, semi, fine, level, pan, pw, uni, det, spread, drift, phase;
    Toggle randPhase;
};

//==============================================================================
class SubNoiseSection : public SectionPanel
{
public:
    explicit SubNoiseSection (juce::AudioProcessorValueTreeState& apvts)
        : SectionPanel ("SUB / NOISE"),
          subOn ("SUB"), subWave ("", false), subLevel ("SUB LVL"),
          noiseType ("", false), noiseLevel ("NOISE LVL"), noiseTone ("TONE")
    {
        addAndMakeVisible (subOn);
        addAndMakeVisible (subWave);
        addAndMakeVisible (subLevel);
        addAndMakeVisible (noiseType);
        addAndMakeVisible (noiseLevel);
        addAndMakeVisible (noiseTone);

        subOn.attach (apvts, ids::subOn);
        subWave.attach (apvts, ids::subWave);
        subLevel.attach (apvts, ids::subLevel);
        noiseType.attach (apvts, ids::noiseType);
        noiseLevel.attach (apvts, ids::noiseLevel);
        noiseTone.attach (apvts, ids::noiseTone);
        noiseLevel.setAccent (theme::accent2);
        noiseTone.setAccent (theme::accent2);
    }

    void paint (juce::Graphics& g) override
    {
        SectionPanel::paint (g);
        g.setColour (theme::outline);
        const auto c = content();
        g.drawVerticalLine (c.getCentreX() - 30, (float) c.getY() + 4.0f, (float) c.getBottom() - 4.0f);
    }

    void resized() override
    {
        auto c = content();
        const int knobW = 56;
        subOn.setBounds (c.getX(), c.getCentreY() - 10, 54, 20);
        subWave.setBounds (c.getX() + 58, c.getCentreY() - 11, 84, 22);
        subLevel.setBounds (c.getX() + 150, c.getY(), knobW, c.getHeight());

        const int nx = c.getCentreX() - 16;
        noiseType.setBounds (nx, c.getCentreY() - 11, 84, 22);
        noiseLevel.setBounds (nx + 92, c.getY(), knobW, c.getHeight());
        noiseTone.setBounds (nx + 92 + knobW + 6, c.getY(), knobW, c.getHeight());
    }

private:
    Toggle subOn;
    Selector subWave;
    Knob subLevel;
    Selector noiseType;
    Knob noiseLevel, noiseTone;
};

} // namespace ydc
