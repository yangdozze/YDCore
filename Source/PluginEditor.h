// YD Core — main editor: 1200×760 reference layout, scale-to-fit resizing
// (min 900×600), dark graphite theme.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UI/LookAndFeelYD.h"
#include "UI/TopBar.h"
#include "UI/SectionsOsc.h"
#include "UI/SectionsMod.h"
#include "UI/SectionsFx.h"
#include "UI/PresetBrowser.h"

class YDCoreAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit YDCoreAudioProcessorEditor (YDCoreAudioProcessor&);
    ~YDCoreAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kRefW = 1200;
    static constexpr int kRefH = 760;

    YDCoreAudioProcessor& processor;
    ydc::LookAndFeelYD lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Component content;

    ydc::TopBar topBar;
    ydc::OscSection osc1, osc2;
    ydc::SubNoiseSection subNoise;
    ydc::LfoSection lfo1, lfo2;
    ydc::FilterSection filter;
    ydc::EnvSection ampEnv, filterEnv;
    ydc::ModEnvSection modEnv;
    ydc::MatrixSection matrix;
    ydc::ArpSection arp;
    ydc::GlobalSection global;
    ydc::FxSection fx;
    juce::MidiKeyboardComponent keyboard;
    ydc::PresetBrowser browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YDCoreAudioProcessorEditor)
};
