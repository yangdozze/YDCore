// GLOBUS — main editor: persistent top bar + five tabbed pages
// (OSC / FX / MATRIX / GLOBAL / PRESETS). 1200×760 reference layout,
// scale-to-fit resizing from 900×600 up to 2400×1520.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UI/LookAndFeelYD.h"
#include "UI/TopBar.h"
#include "UI/Pages.h"
#include "UI/PresetBrowser.h"

class YDCoreAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit YDCoreAudioProcessorEditor (YDCoreAudioProcessor&);
    ~YDCoreAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Tab control (also used by the headless screenshot mode). */
    void setActiveTab (int index);
    int  getActiveTab() const noexcept { return activeTab; }
    static constexpr int kNumTabs = 5;

private:
    static constexpr int kRefW = 1200;
    static constexpr int kRefH = 760;
    static constexpr int kTopBarH = 64;

    YDCoreAudioProcessor& processor;
    ydc::LookAndFeelYD lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Component content;

    ydc::TopBar topBar;
    ydc::OscPage oscPage;
    ydc::FxPage fxPage;
    ydc::MatrixPage matrixPage;
    ydc::GlobalPage globalPage;
    ydc::PresetBankPage presetsPage;
    juce::Component* pages[kNumTabs];

    int activeTab = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YDCoreAudioProcessorEditor)
};
