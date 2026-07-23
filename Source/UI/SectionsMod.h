// YD Core — filter, envelope, LFO and modulation-matrix section components.
#pragma once
#include "Controls.h"
#include "EnvelopeDisplay.h"

namespace ydc
{
class FilterSection : public SectionPanel
{
public:
    explicit FilterSection (juce::AudioProcessorValueTreeState& apvts)
        : SectionPanel ("FILTER"),
          on ("ON"), type ("", false),
          cutoff ("CUTOFF"), reso ("RESO"), drive ("DRIVE"), key ("KEY TRK"), envAmt ("ENV AMT")
    {
        addAndMakeVisible (on);
        addAndMakeVisible (type);
        for (Knob* k : { &cutoff, &reso, &drive, &key, &envAmt })
            addAndMakeVisible (*k);

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
        on.setBounds (78, 3, 50, 20);
        type.setBounds (130, 3, 92, 20);

        auto area = c.withTrimmedTop (6);
        cutoff.setBounds (area.removeFromLeft (86));
        auto cells = rowCells (area, 4, 3);
        Knob* knobs[] = { &reso, &drive, &key, &envAmt };
        for (size_t i = 0; i < 4; ++i)
            knobs[i]->setBounds (cells[i]);
    }

private:
    Toggle on;
    Selector type;
    Knob cutoff, reso, drive, key, envAmt;
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
        display.setBounds (c.removeFromRight (juce::jmax (80, c.getWidth() * 32 / 100)));
        c.removeFromRight (4);
        auto cells = rowCells (c, 4, 3);
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
        auto bottom = c.removeFromBottom (24);
        dest.setBounds (bottom.removeFromLeft (118).withTrimmedTop (2));
        bottom.removeFromLeft (6);
        display.setBounds (bottom);

        auto cells = rowCells (c.withTrimmedBottom (2), 5, 3);
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

//==============================================================================
class MatrixSection : public SectionPanel
{
public:
    explicit MatrixSection (juce::AudioProcessorValueTreeState& apvts)
        : SectionPanel ("MOD MATRIX", theme::accent2)
    {
        for (int i = 0; i < 8; ++i)
        {
            auto& row = rows[(size_t) i];
            row.src  = std::make_unique<Selector> ("", false);
            row.dst  = std::make_unique<Selector> ("", false);
            row.amt  = std::make_unique<juce::Slider>();
            row.bi   = std::make_unique<Toggle> ("BI");

            row.amt->setSliderStyle (juce::Slider::LinearHorizontal);
            row.amt->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            row.amt->setPopupDisplayEnabled (true, true, nullptr);

            addAndMakeVisible (*row.src);
            addAndMakeVisible (*row.dst);
            addAndMakeVisible (*row.amt);
            addAndMakeVisible (*row.bi);

            const int n = i + 1;
            row.src->attach (apvts, ids::slot (n, "Src"));
            row.dst->attach (apvts, ids::slot (n, "Dst"));
            row.amtAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts, ids::slot (n, "Amt"), *row.amt);
            row.amt->setDoubleClickReturnValue (true, 0.0);
            row.amt->setTooltip (getTooltip (ids::slot (n, "Amt")));
            row.bi->attach (apvts, ids::slot (n, "Bipolar"));
        }
    }

    void resized() override
    {
        auto c = content().withTrimmedTop (2);
        const int rowH = c.getHeight() / 8;
        for (int i = 0; i < 8; ++i)
        {
            auto r = c.removeFromTop (rowH).reduced (0, 2);
            auto& row = rows[(size_t) i];
            row.src->setBounds (r.removeFromLeft (100));
            r.removeFromLeft (4);
            row.dst->setBounds (r.removeFromLeft (112));
            r.removeFromLeft (4);
            row.bi->setBounds (r.removeFromRight (38));
            row.amt->setBounds (r);
        }
    }

private:
    struct Row
    {
        std::unique_ptr<Selector> src, dst;
        std::unique_ptr<juce::Slider> amt;
        std::unique_ptr<Toggle> bi;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amtAttachment;
    };
    std::array<Row, 8> rows;
};

} // namespace ydc
