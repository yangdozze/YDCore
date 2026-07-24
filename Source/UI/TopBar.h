// GLOBUS — persistent top bar: brand, tab navigation, preset display &
// transport-style actions, MIDI LED, CPU, voices and a real output meter.
#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace ydc
{
/** Custom-drawn main tab button. */
class TabButton : public juce::Button
{
public:
    explicit TabButton (const juce::String& tabText) : juce::Button (tabText)
    {
        setClickingTogglesState (false);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        const bool active = getToggleState();

        if (active)
        {
            g.setGradientFill (juce::ColourGradient (theme::panelLight.brighter (0.08f), 0, 0,
                                                     theme::panel, 0, b.getBottom(), false));
            g.fillRoundedRectangle (b.reduced (1.0f), 5.0f);
            g.setColour (theme::accent);
            g.fillRoundedRectangle (b.getX() + 8.0f, b.getY() + 2.0f, b.getWidth() - 16.0f, 3.0f, 1.5f);
        }
        else if (highlighted || down)
        {
            g.setColour (theme::panelLight.withAlpha (down ? 0.9f : 0.55f));
            g.fillRoundedRectangle (b.reduced (1.0f), 5.0f);
        }

        g.setColour (active ? theme::text : theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (13.0f, active));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
    }
};

//==============================================================================
/** Compact stereo output meter fed by the processor's lock-free peaks. */
class OutputMeter : public juce::Component,
                    public juce::SettableTooltipClient,
                    private juce::Timer
{
public:
    explicit OutputMeter (YDCoreAudioProcessor& p) : processor (p)
    {
        setTooltip ("Master output level");
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (theme::bg);
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (b, 3.0f, 1.0f);

        auto area = b.reduced (2.5f);
        const float bh = (area.getHeight() - 2.0f) * 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            const auto bar = juce::Rectangle<float> (area.getX(), area.getY() + c * (bh + 2.0f),
                                                     area.getWidth(), bh);
            // dB-ish mapping: -60..0 dB over the bar width
            const float db = juce::Decibels::gainToDecibels (level[c], -60.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            if (frac > 0.001f)
            {
                const auto fill = bar.withWidth (bar.getWidth() * frac);
                const auto col = level[c] > 0.99f ? theme::warn
                                                  : (c == 0 ? theme::accent : theme::accent2);
                g.setColour (col.withAlpha (0.9f));
                g.fillRoundedRectangle (fill, 2.0f);
            }
        }
    }

private:
    void timerCallback() override
    {
        bool changed = false;
        for (int c = 0; c < 2; ++c)
        {
            const float peak = processor.consumeOutputPeak (c);
            const float next = juce::jmax (peak, level[c] * 0.82f);   // fall-back decay
            if (std::abs (next - level[c]) > 0.001f)
            {
                level[c] = next;
                changed = true;
            }
            else
                level[c] = next;
        }
        if (changed)
            repaint();
    }

    YDCoreAudioProcessor& processor;
    float level[2] { 0.0f, 0.0f };
};

//==============================================================================
class TopBar : public juce::Component, private juce::Timer
{
public:
    static constexpr int kNumTabs = 5;

    explicit TopBar (YDCoreAudioProcessor& p)
        : processor (p), meter (p)
    {
        const char* names[kNumTabs] = { "OSC", "FX", "MATRIX", "GLOBAL", "PRESETS" };
        for (int i = 0; i < kNumTabs; ++i)
        {
            tabs[i] = std::make_unique<TabButton> (names[i]);
            tabs[i]->setTooltip (juce::String ("Show the ") + names[i] + " page");
            tabs[i]->onClick = [this, i] { if (onTabSelected) onTabSelected (i); };
            addAndMakeVisible (*tabs[i]);
        }
        tabs[0]->setToggleState (true, juce::dontSendNotification);

        presetButton.setTooltip ("Open the preset bank. A dot marks unsaved changes.");
        presetButton.onClick = [this] { if (onTabSelected) onTabSelected (4); };
        addAndMakeVisible (presetButton);

        prevButton.setButtonText ("<");
        prevButton.setTooltip ("Previous preset");
        prevButton.onClick = [this] { processor.getPresetManager().loadNext (false); refreshPresetName(); };
        addAndMakeVisible (prevButton);

        nextButton.setButtonText (">");
        nextButton.setTooltip ("Next preset");
        nextButton.onClick = [this] { processor.getPresetManager().loadNext (true); refreshPresetName(); };
        addAndMakeVisible (nextButton);

        modeLabel.setJustificationType (juce::Justification::centred);
        modeLabel.setFont (LookAndFeelYD::uiFont (10.5f, true));
        modeLabel.setColour (juce::Label::textColourId, theme::accent);
        modeLabel.setColour (juce::Label::outlineColourId, theme::outline);
        modeLabel.setTooltip ("Current play mode (Poly / Mono / Legato) - change it on the GLOBAL page");
        modeLabel.setText ("POLY", juce::dontSendNotification);
        addAndMakeVisible (modeLabel);

        saveButton.setButtonText ("SAVE");
        saveButton.setTooltip ("Save the current sound as a user preset");
        saveButton.onClick = [this] { if (onSaveRequested) onSaveRequested(); };
        addAndMakeVisible (saveButton);

        initButton.setButtonText ("INIT");
        initButton.setTooltip ("Reset to the init patch");
        initButton.onClick = [this] { processor.getPresetManager().initPatch(); refreshPresetName(); };
        addAndMakeVisible (initButton);

        randButton.setButtonText ("RAND");
        randButton.setTooltip ("Randomize the sound (Normal strength). Use the arrow for Subtle/Wild and section locks.");
        randButton.onClick = [this]
        {
            processor.getPresetManager().randomizePatch (ydc::PresetManager::Strength::Normal);
            refreshPresetName();
        };
        addAndMakeVisible (randButton);

        randMenuButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xbe"));
        randMenuButton.setTooltip ("Randomize options: strength and section locks");
        randMenuButton.onClick = [this] { showRandMenu(); };
        addAndMakeVisible (randMenuButton);

        undoButton.setButtonText (juce::String::fromUTF8 ("\xe2\x86\xa9"));
        undoButton.setTooltip ("Undo the last randomize (restores the exact previous sound)");
        undoButton.onClick = [this]
        {
            processor.getPresetManager().undoRandomize();
            refreshPresetName();
        };
        addAndMakeVisible (undoButton);

        statusLabel.setJustificationType (juce::Justification::centredRight);
        statusLabel.setFont (LookAndFeelYD::uiFont (10.0f));
        statusLabel.setColour (juce::Label::textColourId, theme::textDim);
        statusLabel.setText ("CPU 0%  VOX 0", juce::dontSendNotification);
        addAndMakeVisible (statusLabel);

        addAndMakeVisible (meter);

        startTimerHz (8);
        refreshPresetName();
    }

    std::function<void (int)> onTabSelected;   // wired by the editor
    std::function<void()>     onSaveRequested;

    void setActiveTab (int index)
    {
        for (int i = 0; i < kNumTabs; ++i)
            tabs[i]->setToggleState (i == index, juce::dontSendNotification);
    }

    void refreshPresetName()
    {
        auto& pm = processor.getPresetManager();
        const bool modified = pm.isModified();
        const auto cat = pm.getCurrentCategory();
        presetButton.setButtonText ((modified ? juce::String::fromUTF8 ("\xe2\x97\x8f ") : juce::String())
                                    + (cat.isNotEmpty() && cat != "Init" ? cat + "  |  " : juce::String())
                                    + pm.getCurrentName()
                                    + (modified ? " *" : ""));
        presetButton.setColour (juce::TextButton::textColourOffId, modified ? theme::warn : theme::text);
        undoButton.setEnabled (pm.canUndoRandomize());
    }

    void paint (juce::Graphics& g) override
    {
        g.setGradientFill (juce::ColourGradient (theme::panel.brighter (0.04f), 0, 0,
                                                 theme::panel.darker (0.1f), 0, (float) getHeight(), false));
        g.fillRect (getLocalBounds());
        g.setColour (theme::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        // --- brand: globe glyph (latitude/longitude arcs) + wordmark
        {
            const float cx = 24.0f, cy = getHeight() * 0.5f - 4.0f, r = 12.0f;
            g.setColour (theme::accent);
            g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.6f);
            g.setColour (theme::accent.withAlpha (0.75f));
            g.drawEllipse (cx - r * 0.45f, cy - r, r * 0.9f, r * 2.0f, 1.1f);
            g.setColour (theme::accent2.withAlpha (0.9f));
            g.drawLine (cx - r, cy, cx + r, cy, 1.1f);
            const float pr = r * 0.62f;
            g.setColour (theme::accent2.withAlpha (0.55f));
            g.drawLine (cx - pr, cy - r * 0.52f, cx + pr, cy - r * 0.52f, 0.9f);
            g.drawLine (cx - pr, cy + r * 0.52f, cx + pr, cy + r * 0.52f, 0.9f);

            g.setColour (theme::text);
            g.setFont (LookAndFeelYD::uiFont (19.0f, true));
            g.drawText ("GLOBUS", 42, 8, 92, 24, juce::Justification::centredLeft);
            g.setColour (theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (8.0f));
            g.drawText ("NINTH PARALLEL AUDIO", 43, 34, 120, 11, juce::Justification::centredLeft);
        }

        // --- MIDI LED
        const auto led = midiLedBounds();
        g.setColour (midiLit ? theme::accent : theme::track);
        g.fillEllipse (led);
        if (midiLit)
        {
            g.setColour (theme::accent.withAlpha (0.4f));
            g.fillEllipse (led.expanded (3.0f));
        }
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (8.0f));
        g.drawText ("MIDI", (int) led.getX() - 8, (int) led.getBottom() + 1, 26, 9, juce::Justification::centred);
    }

    void resized() override
    {
        const int h = getHeight();
        const int bh = 26, y = (h - bh) / 2;

        int x = 156;
        for (int i = 0; i < kNumTabs; ++i)
        {
            const int w = i == 4 ? 70 : 58;
            tabs[i]->setBounds (x, 6, w, h - 12);
            x += w + 3;
        }

        prevButton.setBounds (474, y, 24, bh);
        presetButton.setBounds (501, y, 186, bh);
        nextButton.setBounds (690, y, 24, bh);

        modeLabel.setBounds (722, y + 1, 56, bh - 2);

        saveButton.setBounds (786, y, 46, bh);
        initButton.setBounds (835, y, 42, bh);
        randButton.setBounds (880, y, 46, bh);
        randMenuButton.setBounds (927, y, 18, bh);
        undoButton.setBounds (948, y, 28, bh);

        meter.setBounds (getWidth() - 70, (h - 22) / 2, 58, 22);
        statusLabel.setBounds (getWidth() - 216, 0, 112, h);
    }

private:
    juce::Rectangle<float> midiLedBounds() const
    {
        return { (float) getWidth() - 88.0f, getHeight() * 0.5f - 9.0f, 10.0f, 10.0f };
    }

    void showRandMenu()
    {
        auto& pm = processor.getPresetManager();
        juce::PopupMenu m;
        m.addSectionHeader ("Randomize strength");
        m.addItem (1, "Subtle - vary the current sound");
        m.addItem (2, "Normal - new musical patch");
        m.addItem (3, "Wild - experimental (safe)");
        m.addSeparator();
        m.addSectionHeader ("Section locks");
        const char* lockNames[] = { "Lock OSC / SUB / NOISE", "Lock FILTER", "Lock ENVELOPES",
                                    "Lock LFO / MATRIX", "Lock FX" };
        for (int i = 0; i < ydc::PresetManager::NumLocks; ++i)
            m.addItem (10 + i, lockNames[i], true, pm.isSectionLocked ((ydc::PresetManager::Section) i));

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (randMenuButton),
            [this, &pm] (int result)
            {
                if (result >= 1 && result <= 3)
                {
                    using S = ydc::PresetManager::Strength;
                    pm.randomizePatch (result == 1 ? S::Subtle : result == 3 ? S::Wild : S::Normal);
                    refreshPresetName();
                }
                else if (result >= 10 && result < 10 + ydc::PresetManager::NumLocks)
                {
                    const auto s = (ydc::PresetManager::Section) (result - 10);
                    pm.setSectionLocked (s, ! pm.isSectionLocked (s));
                }
            });
    }

    void timerCallback() override
    {
        const bool lit = processor.consumeMidiActivity();
        if (lit != midiLit)
        {
            midiLit = lit;
            repaint (midiLedBounds().expanded (6.0f).toNearestInt());
        }
        const int cpu = juce::roundToInt (processor.getCpuLoad() * 100.0f);
        statusLabel.setText ("CPU " + juce::String (cpu) + "%  VOX "
                             + juce::String (processor.getEngine().getActiveVoiceCount()),
                             juce::dontSendNotification);
        const int mode = (int) processor.getApvts().getRawParameterValue ("playMode")->load();
        modeLabel.setText (mode == 0 ? "POLY" : mode == 1 ? "MONO" : "LEGATO", juce::dontSendNotification);
        refreshPresetName();
    }

    YDCoreAudioProcessor& processor;
    std::unique_ptr<TabButton> tabs[kNumTabs];
    juce::TextButton presetButton, prevButton, nextButton, saveButton, initButton, randButton,
                     randMenuButton, undoButton;
    juce::Label statusLabel, modeLabel;
    OutputMeter meter;
    bool midiLit = false;
};

} // namespace ydc
