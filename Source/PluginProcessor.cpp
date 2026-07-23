#include "PluginProcessor.h"
#include "PluginEditor.h"

YDCoreAudioProcessor::YDCoreAudioProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "YDCORE", ydc::createParameterLayout()),
      refs (apvts),
      presetManager (*this, apvts)
{
}

//==============================================================================
void YDCoreAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    arp.prepare (sampleRate);
    fx.prepare (sampleRate, samplesPerBlock);
    arpBuffer.ensureSize (8192);
    loadMeasurer.reset (sampleRate, samplesPerBlock);
}

void YDCoreAudioProcessor::releaseResources()
{
    engine.reset();
    arp.reset();
    fx.reset();
}

void YDCoreAudioProcessor::reset()
{
    engine.reset();
    arp.reset();
    fx.reset();
}

bool YDCoreAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void YDCoreAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer loadTimer (loadMeasurer, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();

    // on-screen keyboard events merge into the MIDI stream
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);

    if (! midiMessages.isEmpty())
        midiActivity.store (2, std::memory_order_relaxed);

    // transport info (fallbacks when the host provides none)
    double bpm = 120.0, ppq = -1.0;
    bool playing = false;
    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto pos = hostPlayHead->getPosition())
        {
            if (pos->getBpm())          bpm = std::max (20.0, *pos->getBpm());
            if (pos->getPpqPosition())  ppq = *pos->getPpqPosition();
            playing = pos->getIsPlaying();
        }
    }

    // MIDI → arp → engine → FX
    arpBuffer.clear();
    arp.process (midiMessages, arpBuffer, numSamples, bpm, playing, ppq, refs);
    engine.process (buffer, arpBuffer, bpm, refs);
    fx.process (buffer, refs, bpm, engine.getFxMixScale(), engine.getStereoWidthMod());

    // lock-free output metering for the UI (peak-hold, cleared on UI read)
    for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
    {
        const float mag = buffer.getMagnitude (c, 0, numSamples);
        float prev = outPeak[c].load (std::memory_order_relaxed);
        while (mag > prev && ! outPeak[c].compare_exchange_weak (prev, mag, std::memory_order_relaxed)) {}
    }

    midiMessages.clear();
}

//==============================================================================
void YDCoreAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void YDCoreAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

//==============================================================================
juce::AudioProcessorEditor* YDCoreAudioProcessor::createEditor()
{
    return new YDCoreAudioProcessorEditor (*this);
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new YDCoreAudioProcessor();
}
