#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/WavetableImport.h"

YDCoreAudioProcessor::YDCoreAudioProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "YDCORE", ydc::createParameterLayout()),
      refs (apvts),
      presetManager (*this, apvts)
{
    // Build (or fetch) the process-wide factory wavetable library here on the
    // message thread — the audio thread must never trigger generation.
    const auto& lib = ydc::WavetableLibrary::get();
    hqShapesCache = &lib.hqShapes();
    refreshWavetablesFromState();
}

//==============================================================================
void YDCoreAudioProcessor::setOscWavetableByName (int oscIndex0, const juce::String& name)
{
    const int i = juce::jlimit (0, 1, oscIndex0);
    const auto& lib = ydc::WavetableLibrary::get();

    const auto* bank = name.startsWith (ydc::userWavetablePrefix()) ? nullptr : lib.byName (name);
    if (bank == nullptr)
        bank = findUserBank (name);

    // user table referenced but not registered: try the asset directory
    // (message-thread load; spec allows preprocessing on the message thread)
    if (bank == nullptr && name.startsWith (ydc::userWavetablePrefix()))
    {
        const auto base = name.fromFirstOccurrenceOf (ydc::userWavetablePrefix(), false, false);
        const auto asset = ydc::userWavetableDirectory().getChildFile (base + ".wav");
        if (asset.existsAsFile())
        {
            auto result = ydc::importWavetableFromFile (asset);
            if (result.ok())
            {
                std::shared_ptr<const ydc::WavetableBank> shared (std::move (result.bank));
                registerUserBank (shared);
                bank = shared.get();
            }
        }
    }

    if (bank != nullptr)
    {
        const auto* old = wtBankPtr[i].load (std::memory_order_acquire);
        wtBankPtr[i].store (bank, std::memory_order_release);
        wtMissing[i].store (false, std::memory_order_relaxed);
        juce::ignoreUnused (old);
    }
    else
    {
        // unknown/missing: keep something audible, preserve the requested name
        wtBankPtr[i].store (lib.defaultBank(), std::memory_order_release);
        wtMissing[i].store (name.isNotEmpty() && name != lib.defaultBank()->name,
                            std::memory_order_relaxed);
    }
    apvts.state.setProperty (wtStateProp (i), name.isNotEmpty() ? name : lib.defaultBank()->name, nullptr);
    wtEvents.sendChangeMessage();
}

void YDCoreAudioProcessor::refreshWavetablesFromState()
{
    for (int i = 0; i < 2; ++i)
        setOscWavetableByName (i, getOscWavetableName (i));
}

const ydc::WavetableBank* YDCoreAudioProcessor::findUserBank (const juce::String& name) const
{
    for (const auto& b : userBanks)
        if (b->name == name)
            return b.get();
    return nullptr;
}

void YDCoreAudioProcessor::registerUserBank (std::shared_ptr<const ydc::WavetableBank> bank)
{
    // replace-by-name: retire the old bank (freed ≥2 block epochs later so any
    // in-flight audio block finishes with a valid pointer)
    for (auto& b : userBanks)
        if (b->name == bank->name)
        {
            retiredBanks.push_back ({ std::move (b), blockEpoch.load (std::memory_order_acquire) });
            b = std::move (bank);
            cleanupRetiredBanks();
            return;
        }
    if ((int) userBanks.size() >= 64)   // bounded registry
    {
        retiredBanks.push_back ({ std::move (userBanks.front()), blockEpoch.load (std::memory_order_acquire) });
        userBanks.erase (userBanks.begin());
    }
    userBanks.push_back (std::move (bank));
    cleanupRetiredBanks();
}

void YDCoreAudioProcessor::cleanupRetiredBanks()
{
    const auto now = blockEpoch.load (std::memory_order_acquire);
    retiredBanks.erase (std::remove_if (retiredBanks.begin(), retiredBanks.end(),
                                        [now] (const RetiredBank& rb)
                                        { return now >= rb.epoch + 2; }),
                        retiredBanks.end());
}

juce::StringArray YDCoreAudioProcessor::getUserWavetableNames() const
{
    juce::StringArray out;
    for (const auto& b : userBanks)
        out.add (b->name);
    return out;
}

void YDCoreAudioProcessor::importWavetableAsync (int oscIndex0, const juce::File& file)
{
    const int osc = juce::jlimit (0, 1, oscIndex0);
    juce::Thread::launch ([this, osc, file]
    {
        auto result = ydc::importWavetableFromFile (file);
        juce::String err = result.error;
        std::shared_ptr<const ydc::WavetableBank> shared;
        if (result.ok())
        {
            const auto asset = ydc::copyToWavetableAssets (file);
            if (asset == juce::File())
                err = "Imported, but the file could not be copied into Documents/GLOBUS/Wavetables.";
            shared = std::shared_ptr<const ydc::WavetableBank> (std::move (result.bank));
        }

        juce::MessageManager::callAsync ([this, osc, shared, err]
        {
            {
                const juce::ScopedLock sl (importLock);
                lastImportError = err;
            }
            if (shared != nullptr)
            {
                registerUserBank (shared);
                setOscWavetableByName (osc, shared->name);
            }
            else
            {
                wtEvents.sendChangeMessage();   // surface the error
            }
        });
    });
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

    // v1.2 render context: immutable bank pointers + quality for this block
    const auto quality = (ydc::QualityMode) (int) refs.qualityMode->load();
    engine.wtBankOsc[0]  = wtBankPtr[0].load (std::memory_order_acquire);
    engine.wtBankOsc[1]  = wtBankPtr[1].load (std::memory_order_acquire);
    engine.hqShapesBank  = hqShapesCache;
    engine.qualityForBlock = quality;
    blockEpoch.fetch_add (1, std::memory_order_acq_rel);

    // oversampled stages introduce a small fixed latency — report it honestly
    const int wantedLatency = quality == ydc::QualityMode::High  ? ydc::Distortion::kLatencyHigh
                            : quality == ydc::QualityMode::Ultra ? ydc::Distortion::kLatencyUltra : 0;
    if (wantedLatency != getLatencySamples())
        setLatencySamples (wantedLatency);

    // MIDI → arp → engine → FX
    arpBuffer.clear();
    arp.process (midiMessages, arpBuffer, numSamples, bpm, playing, ppq, refs);
    engine.process (buffer, arpBuffer, bpm, refs);
    fx.process (buffer, refs, bpm, engine.getFxMixScale(), engine.getStereoWidthMod(), quality);

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
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            // Backward compatibility: replaceState leaves any parameter that is
            // absent from the incoming tree at its CURRENT value. States saved by
            // older versions (or by 1.1) lack the v1.2 parameters, so explicitly
            // reset every parameter the state does not mention to its default —
            // defaults select the LEGACY sound path.
            juce::StringArray present;
            for (auto* child : xml->getChildWithTagNameIterator ("PARAM"))
                present.add (child->getStringAttribute ("id"));
            for (auto* p : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (! present.contains (rp->paramID))
                        rp->setValueNotifyingHost (rp->getDefaultValue());

            // resolve wavetable references (old states have none → default bank)
            refreshWavetablesFromState();
        }
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
