// GLOBUS — arpeggiator, global/play and master-FX strip sections.
#pragma once
#include "Controls.h"

namespace ydc
{
class ArpSection : public SectionPanel
{
public:
    explicit ArpSection (juce::AudioProcessorValueTreeState& apvts)
        : SectionPanel ("ARPEGGIATOR", theme::warn),
          on ("ON"), hold ("HOLD"), mode ("MODE"), div ("RATE"), gate ("GATE"), oct ("OCTAVES")
    {
        addAndMakeVisible (on);
        addAndMakeVisible (hold);
        addAndMakeVisible (mode);
        addAndMakeVisible (div);
        addAndMakeVisible (gate);
        addAndMakeVisible (oct);
        gate.setAccent (theme::warn);
        oct.setAccent (theme::warn);

        on.attach (apvts, ids::arpOn);
        hold.attach (apvts, ids::arpHold);
        mode.attach (apvts, ids::arpMode);
        div.attach (apvts, ids::arpDiv);
        gate.attach (apvts, ids::arpGate);
        oct.attach (apvts, ids::arpOct);
    }

    void resized() override
    {
        auto c = content().withTrimmedTop (2);
        c = c.withSizeKeepingCentre (c.getWidth(), juce::jmin (c.getHeight(), 240));
        auto top = c.removeFromTop (24);
        on.setBounds (top.removeFromLeft (56));
        hold.setBounds (top.removeFromLeft (64));

        auto selRow = c.removeFromTop (40).reduced (0, 2);
        mode.setBounds (selRow.removeFromLeft (c.getWidth() / 2 - 3));
        selRow.removeFromLeft (6);
        div.setBounds (selRow);

        auto knobs = rowCells (c.reduced (30, 4), 2, 6);
        gate.setBounds (knobs[0]);
        oct.setBounds (knobs[1]);
    }

private:
    Toggle on, hold;
    Selector mode, div;
    Knob gate, oct;
};

//==============================================================================
class GlobalSection : public SectionPanel
{
public:
    explicit GlobalSection (juce::AudioProcessorValueTreeState& apvts)
        : SectionPanel ("PLAY / GLOBAL"),
          mode ("MODE"), priority ("PRIORITY"),
          glide ("GLIDE"), bend ("BEND RNG"), vel ("VELOCITY"), poly ("VOICES")
    {
        addAndMakeVisible (mode);
        addAndMakeVisible (priority);
        for (Knob* k : { &glide, &bend, &vel, &poly })
            addAndMakeVisible (*k);

        mode.attach (apvts, ids::playMode);
        priority.attach (apvts, ids::notePriority);
        glide.attach (apvts, ids::glideTime);
        bend.attach (apvts, ids::pitchBendRange);
        vel.attach (apvts, ids::velAmount);
        poly.attach (apvts, ids::polyLimit);
    }

    void resized() override
    {
        auto c = content().withTrimmedTop (2);
        c = c.withSizeKeepingCentre (c.getWidth(), juce::jmin (c.getHeight(), 240));
        auto selRow = c.removeFromTop (40).reduced (0, 2);
        mode.setBounds (selRow.removeFromLeft (c.getWidth() / 2 - 3));
        selRow.removeFromLeft (6);
        priority.setBounds (selRow);

        auto cells = rowCells (c.reduced (10, 4), 4, 4);
        Knob* knobs[] = { &glide, &bend, &vel, &poly };
        for (size_t i = 0; i < 4; ++i)
            knobs[i]->setBounds (cells[i]);
    }

private:
    Selector mode, priority;
    Knob glide, bend, vel, poly;
};

} // namespace ydc
