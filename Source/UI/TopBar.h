// YD Core — top bar: branding, preset display & navigation, save/init/random,
// MIDI activity LED, CPU and voice meters.
#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace ydc
{
class TopBar : public juce::Component, private juce::Timer
{
public:
    explicit TopBar (YDCoreAudioProcessor& p) : processor (p)
    {
        presetButton.setButtonText ("Init");
        presetButton.setTooltip ("Open the preset browser");
        addAndMakeVisible (presetButton);

        prevButton.setButtonText ("<");
        prevButton.setTooltip ("Previous preset");
        prevButton.onClick = [this] { processor.getPresetManager().loadNext (false); refreshPresetName(); };
        addAndMakeVisible (prevButton);

        nextButton.setButtonText (">");
        nextButton.setTooltip ("Next preset");
        nextButton.onClick = [this] { processor.getPresetManager().loadNext (true); refreshPresetName(); };
        addAndMakeVisible (nextButton);

        saveButton.setButtonText ("SAVE");
        saveButton.setTooltip ("Save the current sound as a user preset");
        addAndMakeVisible (saveButton);

        initButton.setButtonText ("INIT");
        initButton.setTooltip ("Reset to the init patch");
        initButton.onClick = [this] { processor.getPresetManager().initPatch(); refreshPresetName(); };
        addAndMakeVisible (initButton);

        randButton.setButtonText ("RAND");
        randButton.setTooltip ("Generate a random patch");
        randButton.onClick = [this] { processor.getPresetManager().randomizePatch(); refreshPresetName(); };
        addAndMakeVisible (randButton);

        statusLabel.setJustificationType (juce::Justification::centredRight);
        statusLabel.setFont (LookAndFeelYD::uiFont (11.0f));
        statusLabel.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (statusLabel);

        startTimerHz (8);
        refreshPresetName();
    }

    std::function<void()> onOpenBrowser;   // wired by the editor
    std::function<void()> onSave;

    void parentHierarchyChanged() override { refreshPresetName(); }

    void refreshPresetName()
    {
        auto& pm = processor.getPresetManager();
        const auto cat = pm.getCurrentCategory();
        presetButton.setButtonText ((cat.isNotEmpty() && cat != "Init" ? cat + "  |  " : juce::String())
                                    + pm.getCurrentName());
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel);
        g.fillRect (getLocalBounds());
        g.setColour (theme::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        // brand
        g.setFont (LookAndFeelYD::uiFont (22.0f, true));
        g.setColour (theme::text);
        g.drawText ("YD", 14, 0, 34, getHeight(), juce::Justification::centredLeft);
        g.setColour (theme::accent);
        g.drawText ("CORE", 47, 0, 70, getHeight(), juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (9.5f));
        g.drawText ("YANGDOZZE", 15, getHeight() - 15, 100, 12, juce::Justification::topLeft);

        // MIDI LED
        const auto led = midiLedBounds();
        g.setColour (midiLit ? theme::accent : theme::track);
        g.fillEllipse (led);
        if (midiLit)
        {
            g.setColour (theme::accent.withAlpha (0.4f));
            g.fillEllipse (led.expanded (3.0f));
        }
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (9.5f));
        g.drawText ("MIDI", (int) led.getX() - 6, (int) led.getBottom() + 1, 24, 10, juce::Justification::centred);
    }

    void resized() override
    {
        const int h = getHeight();
        const int bh = 26, y = (h - bh) / 2;
        prevButton.setBounds (140, y, 30, bh);
        presetButton.setBounds (174, y, 300, bh);
        nextButton.setBounds (478, y, 30, bh);
        saveButton.setBounds (524, y, 58, bh);
        initButton.setBounds (586, y, 52, bh);
        randButton.setBounds (642, y, 56, bh);
        statusLabel.setBounds (getWidth() - 210, 0, 160, h);
    }

private:
    juce::Rectangle<float> midiLedBounds() const
    {
        return { (float) getWidth() - 36.0f, getHeight() * 0.5f - 9.0f, 11.0f, 11.0f };
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
        statusLabel.setText ("CPU " + juce::String (cpu) + "%   VOICES "
                             + juce::String (processor.getEngine().getActiveVoiceCount()),
                             juce::dontSendNotification);
        refreshPresetName();
    }

    YDCoreAudioProcessor& processor;
    juce::TextButton presetButton, prevButton, nextButton, saveButton, initButton, randButton;
    juce::Label statusLabel;
    bool midiLit = false;

public:
    juce::TextButton& getPresetButton() { return presetButton; }
    juce::TextButton& getSaveButton()   { return saveButton; }
};

} // namespace ydc
