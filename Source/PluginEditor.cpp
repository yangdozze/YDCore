#include "PluginEditor.h"

YDCoreAudioProcessorEditor::YDCoreAudioProcessorEditor (YDCoreAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      topBar (p),
      osc1 (p.getApvts(), 1),
      osc2 (p.getApvts(), 2),
      subNoise (p.getApvts()),
      lfo1 (p.getApvts(), 1),
      lfo2 (p.getApvts(), 2),
      filter (p.getApvts()),
      ampEnv (p.getApvts(), "AMP ENV", ydc::ids::ampA, ydc::ids::ampD, ydc::ids::ampS, ydc::ids::ampR, ydc::theme::accent),
      filterEnv (p.getApvts(), "FILTER ENV", ydc::ids::filA, ydc::ids::filD, ydc::ids::filS, ydc::ids::filR, ydc::theme::accent2),
      modEnv (p.getApvts()),
      matrix (p.getApvts()),
      arp (p.getApvts()),
      global (p.getApvts()),
      fx (p.getApvts()),
      keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard),
      browser (p)
{
    setLookAndFeel (&lookAndFeel);

    for (juce::Component* c : { (juce::Component*) &topBar, (juce::Component*) &osc1, (juce::Component*) &osc2,
                                (juce::Component*) &subNoise, (juce::Component*) &lfo1, (juce::Component*) &lfo2,
                                (juce::Component*) &filter, (juce::Component*) &ampEnv, (juce::Component*) &filterEnv,
                                (juce::Component*) &modEnv, (juce::Component*) &matrix, (juce::Component*) &arp,
                                (juce::Component*) &global, (juce::Component*) &fx, (juce::Component*) &keyboard })
        content.addAndMakeVisible (*c);

    content.addChildComponent (browser);   // hidden until opened
    addAndMakeVisible (content);

    topBar.getPresetButton().onClick = [this] { browser.open (false); };
    topBar.getSaveButton().onClick   = [this] { browser.open (true); };

    keyboard.setKeyWidth (26.0f);
    keyboard.setLowestVisibleKey (24);

    // fixed reference layout inside `content`
    content.setSize (kRefW, kRefH);
    topBar.setBounds (0, 0, kRefW, 52);
    osc1.setBounds (8, 56, 590, 130);
    osc2.setBounds (8, 190, 590, 130);
    subNoise.setBounds (8, 324, 590, 80);
    lfo1.setBounds (8, 408, 590, 98);
    lfo2.setBounds (8, 510, 590, 98);
    filter.setBounds (602, 56, 292, 130);
    ampEnv.setBounds (898, 56, 294, 130);
    filterEnv.setBounds (602, 190, 292, 130);
    modEnv.setBounds (898, 190, 294, 130);
    matrix.setBounds (602, 324, 364, 284);
    arp.setBounds (970, 324, 222, 138);
    global.setBounds (970, 466, 222, 142);
    fx.setBounds (8, 612, 1184, 92);
    keyboard.setBounds (8, 708, 1184, 44);
    browser.setBounds (0, 0, kRefW, kRefH);

    setResizable (true, true);
    setResizeLimits (900, 600, 2400, 1520);
    setSize (kRefW, kRefH);
}

YDCoreAudioProcessorEditor::~YDCoreAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void YDCoreAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ydc::theme::bg);
}

void YDCoreAudioProcessorEditor::resized()
{
    // scale the fixed 1200x760 layout to fit, centred (letterboxed when needed)
    const float scale = juce::jmin ((float) getWidth() / (float) kRefW,
                                    (float) getHeight() / (float) kRefH);
    const float ox = ((float) getWidth()  - kRefW * scale) * 0.5f;
    const float oy = ((float) getHeight() - kRefH * scale) * 0.5f;
    content.setTransform (juce::AffineTransform::scale (scale).translated (ox, oy));
    content.setTopLeftPosition (0, 0);
}
