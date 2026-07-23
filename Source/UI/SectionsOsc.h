// GLOBUS — oscillator, sub & noise section components (OSC tab).
#pragma once
#include "Controls.h"
#include "WaveformDisplay.h"

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
        // header row next to the title
        on.setBounds (150, 3, 52, 20);
        wave.setBounds (206, 3, 116, 20);
        randPhase.setBounds (330, 3, 72, 20);

        auto body = c.withTrimmedTop (4);
        display.setBounds (body.removeFromRight (158));
        body.removeFromRight (6);

        const int rowH = body.getHeight() / 2;
        auto row1 = body.removeFromTop (rowH);
        auto row2 = body;

        Knob* top[]    = { &oct, &semi, &fine, &level, &pan, &pw };
        Knob* bottom[] = { &uni, &det, &spread, &drift, &phase };
        auto cells1 = rowCells (row1, 6, 4);
        for (size_t i = 0; i < 6; ++i)
            top[i]->setBounds (cells1[i]);
        auto cells2 = rowCells (row2, 6, 4);   // 6 cells keeps columns aligned; last stays empty
        for (size_t i = 0; i < 5; ++i)
            bottom[i]->setBounds (cells2[i]);
    }

private:
    Toggle on;
    Selector wave;
    Knob oct, semi, fine, level, pan, pw, uni, det, spread, drift, phase;
    Toggle randPhase;
    WaveformDisplay display;
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
        const int knobW = 58;
        subOn.setBounds (c.getX(), c.getCentreY() - 10, 54, 20);
        subWave.setBounds (c.getX() + 58, c.getCentreY() - 11, 84, 22);
        subLevel.setBounds (c.getX() + 152, c.getY(), knobW, c.getHeight());

        const int nx = c.getCentreX() - 16;
        noiseType.setBounds (nx, c.getCentreY() - 11, 84, 22);
        noiseLevel.setBounds (nx + 92, c.getY(), knobW, c.getHeight());
        noiseTone.setBounds (nx + 92 + knobW + 8, c.getY(), knobW, c.getHeight());
    }

private:
    Toggle subOn;
    Selector subWave;
    Knob subLevel;
    Selector noiseType;
    Knob noiseLevel, noiseTone;
};

} // namespace ydc
