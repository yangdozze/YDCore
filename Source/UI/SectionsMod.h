// GLOBUS — filter, envelope and LFO section components.
#pragma once
#include "Controls.h"
#include "EnvelopeDisplay.h"
#include "../Presets/PresetManager.h"

namespace ydc
{
class FilterSection : public SectionPanel
{
public:
    explicit FilterSection (juce::AudioProcessorValueTreeState& apvts, PresetManager* pmForLock = nullptr)
        : SectionPanel ("FILTER"),
          on ("ON"), type ("", false),
          cutoff ("CUTOFF"), reso ("RESO"), drive ("DRIVE"), key ("KEY TRK"), envAmt ("ENV AMT")
    {
        addAndMakeVisible (on);
        addAndMakeVisible (type);
        for (Knob* k : { &cutoff, &reso, &drive, &key, &envAmt })
            addAndMakeVisible (*k);

        if (pmForLock != nullptr)
        {
            lock = std::make_unique<LockButton> (
                [pmForLock] { return pmForLock->isSectionLocked (PresetManager::LockFilter); },
                [pmForLock] (bool v) { pmForLock->setSectionLocked (PresetManager::LockFilter, v); },
                "filter");
            addAndMakeVisible (*lock);
        }

        on.attach (apvts, ids::filterOn);
        type.attach (apvts, ids::filterType);
        cutoff.attach (apvts, ids::cutoff);
        reso.attach (apvts, ids::resonance);
        drive.attach (apvts, ids::filterDrive);
        key.attach (apvts, ids::keyTrack);
        envAmt.attach (apvts, ids::filterEnvAmt);
        envAmt.setAccent (theme::accent2);
    }

    void resized() override
    {
        auto c = content();
        on.setBounds (66, 2, 48, 18);
        type.setBounds (118, 1, 90, 20);
        if (lock != nullptr)
            lock->setBounds (getWidth() - 26, 2, 20, 18);

        auto area = c.withTrimmedTop (3);
        cutoff.setBounds (area.removeFromLeft (juce::jmax (72, area.getWidth() / 5)));
        auto cells = rowCells (area, 4, 3);
        Knob* knobs[] = { &reso, &drive, &key, &envAmt };
        for (size_t i = 0; i < 4; ++i)
            knobs[i]->setBounds (cells[i]);
    }

private:
    Toggle on;
    Selector type;
    Knob cutoff, reso, drive, key, envAmt;
    std::unique_ptr<LockButton> lock;
};

//==============================================================================
/** Generic ADSR panel with live display (amp & filter envelopes). */
class EnvSection : public SectionPanel
{
public:
    EnvSection (juce::AudioProcessorValueTreeState& apvts, const juce::String& sectionTitle,
                const char* aId, const char* dId, const char* sId, const char* rId,
                juce::Colour accent = theme::accent)
        : SectionPanel (sectionTitle, accent),
          a ("ATTACK"), d ("DECAY"), s ("SUSTAIN"), r ("RELEASE"),
          display (apvts, aId, dId, sId, rId, accent)
    {
        for (Knob* k : { &a, &d, &s, &r })
        {
            k->setAccent (accent);
            addAndMakeVisible (*k);
        }
        addAndMakeVisible (display);
        a.attach (apvts, aId);
        d.attach (apvts, dId);
        s.attach (apvts, sId);
        r.attach (apvts, rId);
    }

    void resized() override
    {
        auto c = content().withTrimmedTop (4);
        display.setBounds (c.removeFromRight (juce::jmax (80, c.getWidth() * 42 / 100)));
        c.removeFromRight (6);
        // compact knob strip, vertically centred beside the full-height graph
        auto strip = c.withSizeKeepingCentre (c.getWidth(), juce::jmin (c.getHeight(), 128));
        auto cells = rowCells (strip, 4, 4);
        Knob* knobs[] = { &a, &d, &s, &r };
        for (size_t i = 0; i < 4; ++i)
            knobs[i]->setBounds (cells[i]);
    }

private:
    Knob a, d, s, r;
    EnvelopeDisplay display;
};

//==============================================================================
/** Mod envelope: ADSR + amount + destination, with display. */
class ModEnvSection : public SectionPanel
{
public:
    explicit ModEnvSection (juce::AudioProcessorValueTreeState& apvts)
        : SectionPanel ("MOD ENV", theme::accent2),
          a ("A"), d ("D"), s ("S"), r ("R"), amt ("AMT"),
          dest ("", false),
          display (apvts, ids::modA, ids::modD, ids::modS, ids::modR, theme::accent2)
    {
        for (Knob* k : { &a, &d, &s, &r, &amt })
        {
            k->setAccent (theme::accent2);
            addAndMakeVisible (*k);
        }
        addAndMakeVisible (dest);
        addAndMakeVisible (display);
        a.attach (apvts, ids::modA);
        d.attach (apvts, ids::modD);
        s.attach (apvts, ids::modS);
        r.attach (apvts, ids::modR);
        amt.attach (apvts, ids::modEnvAmt);
        dest.attach (apvts, ids::modEnvDest);
    }

    void resized() override
    {
        auto c = content().withTrimmedTop (2);
        display.setBounds (c.removeFromRight (juce::jmax (80, c.getWidth() * 42 / 100)));
        c.removeFromRight (6);
        auto strip = c.withSizeKeepingCentre (c.getWidth(), juce::jmin (c.getHeight(), 158));
        auto destRow = strip.removeFromBottom (26);
        dest.setBounds (destRow.withSizeKeepingCentre (juce::jmin (170, destRow.getWidth()), 24));
        strip.removeFromBottom (2);
        auto cells = rowCells (strip, 5, 4);
        Knob* knobs[] = { &a, &d, &s, &r, &amt };
        for (size_t i = 0; i < 5; ++i)
            knobs[i]->setBounds (cells[i]);
    }

private:
    Knob a, d, s, r, amt;
    Selector dest;
    EnvelopeDisplay display;
};

//==============================================================================
class LfoSection : public SectionPanel
{
public:
    LfoSection (juce::AudioProcessorValueTreeState& apvts, int lfoNum)
        : SectionPanel ("LFO " + juce::String (lfoNum), lfoNum == 1 ? theme::accent : theme::accent2),
          wave ("WAVE"), rate ("RATE"), div ("DIV"), fade ("FADE"), phase ("PHASE"),
          dest ("DEST"), amount ("AMOUNT"),
          sync ("SYNC"), bipolar ("BIPOL"), retrig ("RETRIG")
    {
        const auto accent = lfoNum == 1 ? theme::accent : theme::accent2;
        for (Knob* k : { &rate, &fade, &phase, &amount })
        {
            k->setAccent (accent);
            addAndMakeVisible (*k);
        }
        addAndMakeVisible (wave);
        addAndMakeVisible (div);
        addAndMakeVisible (dest);
        addAndMakeVisible (sync);
        addAndMakeVisible (bipolar);
        addAndMakeVisible (retrig);

        wave.attach (apvts, ids::lfo (lfoNum, "Wave"));
        rate.attach (apvts, ids::lfo (lfoNum, "Rate"));
        sync.attach (apvts, ids::lfo (lfoNum, "Sync"));
        div.attach (apvts, ids::lfo (lfoNum, "Div"));
        fade.attach (apvts, ids::lfo (lfoNum, "Fade"));
        phase.attach (apvts, ids::lfo (lfoNum, "Phase"));
        bipolar.attach (apvts, ids::lfo (lfoNum, "Bipolar"));
        retrig.attach (apvts, ids::lfo (lfoNum, "Retrig"));
        dest.attach (apvts, ids::lfo (lfoNum, "Dest"));
        amount.attach (apvts, ids::lfo (lfoNum, "Amount"));
    }

    void resized() override
    {
        auto c = content().withTrimmedTop (2);
        c = c.withSizeKeepingCentre (c.getWidth(), juce::jmin (c.getHeight(), 170));

        auto selCol = c.removeFromLeft (86);
        wave.setBounds (selCol.removeFromTop (34));
        div.setBounds (selCol.removeFromBottom (34));

        c.removeFromLeft (6);
        auto togCol = c.removeFromRight (64);
        const int th = togCol.getHeight() / 3;
        sync.setBounds (togCol.removeFromTop (th));
        bipolar.setBounds (togCol.removeFromTop (th));
        retrig.setBounds (togCol);

        c.removeFromRight (4);
        auto destCol = c.removeFromRight (96);
        dest.setBounds (destCol.withSizeKeepingCentre (96, 34));

        auto cells = rowCells (c, 4, 3);
        Knob* knobs[] = { &rate, &fade, &phase, &amount };
        for (size_t i = 0; i < 4; ++i)
            knobs[i]->setBounds (cells[i]);
    }

private:
    Selector wave;
    Knob rate;
    Selector div;
    Knob fade, phase;
    Selector dest;
    Knob amount;
    Toggle sync, bipolar, retrig;
};

} // namespace ydc
