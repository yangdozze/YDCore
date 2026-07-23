// YD Core — arpeggiator, global/play and master-FX strip sections.
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
        auto top = c.removeFromTop (24);
        on.setBounds (top.removeFromLeft (56));
        hold.setBounds (top.removeFromLeft (64));

        auto selRow = c.removeFromTop (36);
        mode.setBounds (selRow.removeFromLeft (c.getWidth() / 2 - 3));
        selRow.removeFromLeft (6);
        div.setBounds (selRow);

        auto knobs = rowCells (c, 2, 6);
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
        auto selRow = c.removeFromTop (36);
        mode.setBounds (selRow.removeFromLeft (c.getWidth() / 2 - 3));
        selRow.removeFromLeft (6);
        priority.setBounds (selRow);

        auto cells = rowCells (c, 4, 4);
        Knob* knobs[] = { &glide, &bend, &vel, &poly };
        for (size_t i = 0; i < 4; ++i)
            knobs[i]->setBounds (cells[i]);
    }

private:
    Selector mode, priority;
    Knob glide, bend, vel, poly;
};

//==============================================================================
/** One mini panel inside the FX strip. */
class FxPanel : public juce::Component
{
public:
    FxPanel (const juce::String& titleText, juce::Colour accentCol)
        : title (titleText), accent (accentCol) {}

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::panelLight.darker (0.08f));
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (b, 5.0f, 1.0f);
        g.setColour (accent.withAlpha (0.9f));
        g.setFont (LookAndFeelYD::uiFont (10.5f, true));
        g.drawText (title, 26, 3, getWidth() - 30, 14, juce::Justification::centredLeft);
    }

    juce::Rectangle<int> content() const { return getLocalBounds().reduced (5).withTrimmedTop (15); }

private:
    juce::String title;
    juce::Colour accent;
};

//==============================================================================
class FxSection : public juce::Component
{
public:
    explicit FxSection (juce::AudioProcessorValueTreeState& apvts)
        : distPanel ("DISTORTION", theme::warn),
          chPanel ("CHORUS", theme::accent),
          dlyPanel ("DELAY", theme::accent),
          rvPanel ("REVERB", theme::accent2),
          eqPanel ("EQ", theme::accent),
          outPanel ("OUTPUT", theme::accent2),
          distOn (""), chOn (""), dlyOn (""), rvOn (""), eqOn (""), dlySync ("SYNC"),
          distDrive ("DRIVE"), distTone ("TONE"), distMix ("MIX"),
          chRate ("RATE"), chDepth ("DEPTH"), chWidth ("WIDTH"), chMix ("MIX"),
          dlyTime ("TIME"), dlyDiv ("", false), dlyFb ("FDBK"), dlyTone ("TONE"), dlyMix ("MIX"),
          rvSize ("SIZE"), rvDamp ("DAMP"), rvWidth ("WIDTH"), rvMix ("MIX"),
          eqLow ("LOW"), eqMid ("MID"), eqHigh ("HIGH"),
          width ("WIDTH"), master ("MASTER")
    {
        for (auto* pnl : { &distPanel, &chPanel, &dlyPanel, &rvPanel, &eqPanel, &outPanel })
            addAndMakeVisible (*pnl);

        auto add = [] (FxPanel& pnl, std::initializer_list<juce::Component*> comps)
        {
            for (auto* comp : comps)
                pnl.addAndMakeVisible (*comp);
        };
        add (distPanel, { &distOn, &distDrive, &distTone, &distMix });
        add (chPanel,   { &chOn, &chRate, &chDepth, &chWidth, &chMix });
        add (dlyPanel,  { &dlyOn, &dlySync, &dlyTime, &dlyDiv, &dlyFb, &dlyTone, &dlyMix });
        add (rvPanel,   { &rvOn, &rvSize, &rvDamp, &rvWidth, &rvMix });
        add (eqPanel,   { &eqOn, &eqLow, &eqMid, &eqHigh });
        add (outPanel,  { &width, &master });

        distOn.attach (apvts, ids::distOn);   distDrive.attach (apvts, ids::distDrive);
        distTone.attach (apvts, ids::distTone); distMix.attach (apvts, ids::distMix);
        chOn.attach (apvts, ids::chOn);       chRate.attach (apvts, ids::chRate);
        chDepth.attach (apvts, ids::chDepth); chWidth.attach (apvts, ids::chWidth);
        chMix.attach (apvts, ids::chMix);
        dlyOn.attach (apvts, ids::dlyOn);     dlySync.attach (apvts, ids::dlySync);
        dlyTime.attach (apvts, ids::dlyTime); dlyDiv.attach (apvts, ids::dlyDiv);
        dlyFb.attach (apvts, ids::dlyFb);     dlyTone.attach (apvts, ids::dlyTone);
        dlyMix.attach (apvts, ids::dlyMix);
        rvOn.attach (apvts, ids::rvOn);       rvSize.attach (apvts, ids::rvSize);
        rvDamp.attach (apvts, ids::rvDamp);   rvWidth.attach (apvts, ids::rvWidth);
        rvMix.attach (apvts, ids::rvMix);
        eqOn.attach (apvts, ids::eqOn);       eqLow.attach (apvts, ids::eqLow);
        eqMid.attach (apvts, ids::eqMid);     eqHigh.attach (apvts, ids::eqHigh);
        width.attach (apvts, ids::stereoWidth);
        master.attach (apvts, ids::masterLevel);
        master.setAccent (theme::accent2);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        const int gap = 5;
        // proportional panel widths: dist 15, chorus 17, delay 23, reverb 17, eq 14, out 14 (of 100)
        const int totalW = b.getWidth() - gap * 5;
        auto place = [&b, gap, totalW] (FxPanel& pnl, int pct)
        {
            pnl.setBounds (b.removeFromLeft (totalW * pct / 100));
            b.removeFromLeft (gap);
        };
        place (distPanel, 15);
        place (chPanel, 17);
        place (dlyPanel, 23);
        place (rvPanel, 17);
        place (eqPanel, 14);
        outPanel.setBounds (b);

        layoutPanel (distPanel, &distOn, nullptr, nullptr, { &distDrive, &distTone, &distMix });
        layoutPanel (chPanel, &chOn, nullptr, nullptr, { &chRate, &chDepth, &chWidth, &chMix });
        layoutPanel (dlyPanel, &dlyOn, &dlySync, &dlyDiv, { &dlyTime, &dlyFb, &dlyTone, &dlyMix });
        layoutPanel (rvPanel, &rvOn, nullptr, nullptr, { &rvSize, &rvDamp, &rvWidth, &rvMix });
        layoutPanel (eqPanel, &eqOn, nullptr, nullptr, { &eqLow, &eqMid, &eqHigh });
        layoutPanel (outPanel, nullptr, nullptr, nullptr, { &width, &master });
    }

private:
    void layoutPanel (FxPanel& pnl, Toggle* onToggle, Toggle* syncToggle, Selector* divSel,
                      std::initializer_list<Knob*> knobs)
    {
        if (onToggle != nullptr)
            onToggle->setBounds (5, 2, 20, 16);

        auto c = pnl.content();
        if (syncToggle != nullptr && divSel != nullptr)
        {
            auto col = c.removeFromLeft (58);
            syncToggle->setBounds (col.removeFromTop (c.getHeight() / 2));
            divSel->setBounds (col.reduced (0, 4));
            c.removeFromLeft (2);
        }
        auto cells = rowCells (c, (int) knobs.size(), 2);
        size_t i = 0;
        for (auto* k : knobs)
            k->setBounds (cells[i++]);
    }

    FxPanel distPanel, chPanel, dlyPanel, rvPanel, eqPanel, outPanel;
    Toggle distOn, chOn, dlyOn, rvOn, eqOn, dlySync;
    Knob distDrive, distTone, distMix;
    Knob chRate, chDepth, chWidth, chMix;
    Knob dlyTime;
    Selector dlyDiv;
    Knob dlyFb, dlyTone, dlyMix;
    Knob rvSize, rvDamp, rvWidth, rvMix;
    Knob eqLow, eqMid, eqHigh;
    Knob width, master;
};

} // namespace ydc
