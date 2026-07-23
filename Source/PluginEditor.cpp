#include "PluginEditor.h"

YDCoreAudioProcessorEditor::YDCoreAudioProcessorEditor (YDCoreAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      topBar (p),
      oscPage (p),
      fxPage (p),
      matrixPage (p),
      globalPage (p),
      presetsPage (p),
      pages { &oscPage, &fxPage, &matrixPage, &globalPage, &presetsPage }
{
    setLookAndFeel (&lookAndFeel);

    content.addAndMakeVisible (topBar);
    for (auto* page : pages)
        content.addChildComponent (*page);
    addAndMakeVisible (content);

    topBar.onTabSelected  = [this] (int idx) { setActiveTab (idx); };
    topBar.onSaveRequested = [this]
    {
        setActiveTab (4);
        presetsPage.refresh (true);   // jump straight into the Save As field
    };
    presetsPage.onClose = [this] { setActiveTab (0); };

    // fixed reference layout inside `content`
    content.setSize (kRefW, kRefH);
    topBar.setBounds (0, 0, kRefW, kTopBarH);
    for (auto* page : pages)
        page->setBounds (0, kTopBarH, kRefW, kRefH - kTopBarH);

    setActiveTab (0);

    setResizable (true, true);
    setResizeLimits (900, 600, 2400, 1520);
    setSize (kRefW, kRefH);
}

YDCoreAudioProcessorEditor::~YDCoreAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void YDCoreAudioProcessorEditor::setActiveTab (int index)
{
    activeTab = juce::jlimit (0, kNumTabs - 1, index);
    for (int i = 0; i < kNumTabs; ++i)
        pages[i]->setVisible (i == activeTab);
    topBar.setActiveTab (activeTab);
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
