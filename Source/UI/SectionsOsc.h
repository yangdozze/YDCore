// GLOBUS — oscillator, sub & noise section components (OSC tab).
// v1.2: engine selector (LEGACY / BASIC HQ / WAVETABLE) with contextual
// controls — PW for pulse-capable engines, WT POS + WARP for the wavetable
// engine, live wavetable display with position indicator, and a browser button
// with prev/next navigation. Every visible control is bound to a real
// parameter or an implemented action.
#pragma once
#include "Controls.h"
#include "WaveformDisplay.h"
#include "WavetableDisplay.h"
#include "../Presets/PresetManager.h"
#include "../PluginProcessor.h"
#include "../Engine/WavetableImport.h"    // userWavetablePrefix()

namespace ydc
{
class OscSection : public SectionPanel,
                   private juce::AudioProcessorValueTreeState::Listener,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    OscSection (YDCoreAudioProcessor& p, int oscNum, PresetManager* pmForLock = nullptr)
        : SectionPanel ("OSCILLATOR " + juce::String (oscNum),
                        oscNum == 1 ? theme::accent : theme::accent2),
          proc (p), oscIndex (oscNum - 1),
          on ("ON"), wave ("", false), engine ("", false), warpMode ("", false),
          oct ("OCT"), semi ("SEMI"), fine ("FINE"), level ("LEVEL"), pan ("PAN"), pw ("PW"),
          uni ("UNISON"), det ("DETUNE"), spread ("SPREAD"), drift ("DRIFT"), phase ("PHASE"),
          wtPos ("POS"), warpAmt ("WARP"),
          randPhase ("RND PH"),
          display (p.getApvts(), oscNum, oscNum == 1 ? theme::accent : theme::accent2),
          wtDisplay (p, oscNum, oscNum == 1 ? theme::accent : theme::accent2)
    {
        auto& apvts = p.getApvts();
        const auto accent = oscNum == 1 ? theme::accent : theme::accent2;
        for (Knob* k : { &oct, &semi, &fine, &level, &pan, &pw, &uni, &det, &spread, &drift, &phase,
                         &wtPos, &warpAmt })
        {
            k->setAccent (accent);
            addAndMakeVisible (*k);
        }
        addAndMakeVisible (on);
        addAndMakeVisible (wave);
        addAndMakeVisible (engine);
        addAndMakeVisible (warpMode);
        addAndMakeVisible (randPhase);
        addAndMakeVisible (display);
        addAndMakeVisible (wtDisplay);

        browserButton.setButtonText ("...");
        browserButton.setTooltip ("Open the wavetable browser");
        browserButton.onClick = [this] { if (onOpenBrowser) onOpenBrowser(); };
        addAndMakeVisible (browserButton);
        prevButton.setButtonText (juce::String::fromUTF8 ("\xe2\x97\x80"));
        prevButton.setTooltip ("Previous wavetable");
        prevButton.onClick = [this] { if (onStepWavetable) onStepWavetable (-1); };
        addAndMakeVisible (prevButton);
        nextButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6"));
        nextButton.setTooltip ("Next wavetable");
        nextButton.onClick = [this] { if (onStepWavetable) onStepWavetable (1); };
        addAndMakeVisible (nextButton);

        if (pmForLock != nullptr)
        {
            lock = std::make_unique<LockButton> (
                [pmForLock] { return pmForLock->isSectionLocked (PresetManager::LockOsc); },
                [pmForLock] (bool v) { pmForLock->setSectionLocked (PresetManager::LockOsc, v); },
                "oscillator/sub/noise");
            addAndMakeVisible (*lock);
        }

        const int n = oscNum;
        on.attach (apvts, ids::osc (n, "On"));
        wave.attach (apvts, ids::osc (n, "Wave"));
        engine.attach (apvts, ids::osc (n, "Engine"));
        warpMode.attach (apvts, ids::osc (n, "WarpMode"));
        oct.attach (apvts, ids::osc (n, "Oct"));
        semi.attach (apvts, ids::osc (n, "Semi"));
        fine.attach (apvts, ids::osc (n, "Fine"));
        level.attach (apvts, ids::osc (n, "Level"));
        pan.attach (apvts, ids::osc (n, "Pan"));
        pw.attach (apvts, ids::osc (n, "PW"));
        uni.attach (apvts, ids::osc (n, "UniCount"));
        det.attach (apvts, ids::osc (n, "UniDetune"));
        spread.attach (apvts, ids::osc (n, "UniSpread"));
        drift.attach (apvts, ids::osc (n, "Drift"));
        phase.attach (apvts, ids::osc (n, "Phase"));
        wtPos.attach (apvts, ids::osc (n, "WtPos"));
        warpAmt.attach (apvts, ids::osc (n, "WarpAmt"));
        randPhase.attach (apvts, ids::osc (n, "RandPhase"));

        engineRaw = apvts.getRawParameterValue (ids::osc (n, "Engine"));
        apvts.addParameterListener (ids::osc (n, "Engine"), this);
        proc.wtEvents.addChangeListener (this);
        startTimerHz (10);
        updateEngineVisibility();
    }

    ~OscSection() override
    {
        proc.getApvts().removeParameterListener (ids::osc (oscIndex + 1, "Engine"), this);
        proc.wtEvents.removeChangeListener (this);
    }

    std::function<void()> onOpenBrowser;
    std::function<void (int)> onStepWavetable;

    void resized() override
    {
        auto c = content();
        // header row: ON, engine, wave selector OR browser controls, random phase, lock
        on.setBounds (118, 2, 44, 18);
        engine.setBounds (164, 1, 96, 20);
        wave.setBounds (266, 1, 100, 20);
        prevButton.setBounds (266, 1, 20, 20);
        browserButton.setBounds (288, 1, 130, 20);
        nextButton.setBounds (420, 1, 20, 20);
        randPhase.setBounds (getWidth() - 100, 2, 70, 18);
        if (lock != nullptr)
            lock->setBounds (getWidth() - 26, 2, 20, 18);

        auto body = c.withTrimmedTop (2);
        auto displayArea = body.removeFromRight (150);
        const bool wt = isWavetable();
        if (wt)
        {
            warpMode.setBounds (displayArea.removeFromBottom (22).reduced (2, 1));
            wtDisplay.setBounds (displayArea);
            display.setBounds (displayArea);        // hidden anyway
        }
        else
        {
            display.setBounds (displayArea);
            wtDisplay.setBounds (displayArea);
        }
        body.removeFromRight (5);

        const int rowH = body.getHeight() / 2;
        auto row1 = body.removeFromTop (rowH);
        Knob* top[]    = { &oct, &semi, &fine, &level, &pan, wt ? &wtPos : &pw };
        Knob* bottom[] = { &uni, &det, &spread, &drift, &phase, &warpAmt };
        auto cells1 = rowCells (row1, 6, 3);
        for (size_t i = 0; i < 6; ++i)
            top[i]->setBounds (cells1[i]);
        auto cells2 = rowCells (body, 6, 3);
        const size_t bottomCount = wt ? 6 : 5;
        for (size_t i = 0; i < bottomCount; ++i)
            bottom[i]->setBounds (cells2[i]);
    }

private:
    bool isWavetable() const noexcept
    {
        return engineRaw != nullptr && (int) engineRaw->load() == (int) OscEngine::Wavetable;
    }

    void updateEngineVisibility()
    {
        const bool wt = isWavetable();
        wave.setVisible (! wt);
        pw.setVisible (! wt);
        wtPos.setVisible (wt);
        warpAmt.setVisible (wt);
        warpMode.setVisible (wt);
        display.setVisible (! wt);
        wtDisplay.setVisible (wt);
        browserButton.setVisible (wt);
        prevButton.setVisible (wt);
        nextButton.setVisible (wt);
        refreshBrowserButtonText();
        resized();
    }

    void refreshBrowserButtonText()
    {
        auto name = proc.getOscWavetableName (oscIndex);
        if (name.startsWith (userWavetablePrefix()))
            name = name.fromFirstOccurrenceOf (userWavetablePrefix(), false, false);
        if (proc.isOscWavetableMissing (oscIndex))
            name = "! " + name;
        browserButton.setButtonText (name);
        browserButton.setTooltip (proc.isOscWavetableMissing (oscIndex)
                                  ? "Missing wavetable file - the default bank is playing. Click to choose another."
                                  : "Open the wavetable browser");
    }

    void parameterChanged (const juce::String&, float) override
    {
        engineDirty.store (true, std::memory_order_relaxed);
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        engineDirty.store (true, std::memory_order_relaxed);
    }
    void timerCallback() override
    {
        if (engineDirty.exchange (false, std::memory_order_relaxed))
            updateEngineVisibility();
    }

    YDCoreAudioProcessor& proc;
    int oscIndex;
    Toggle on;
    Selector wave, engine, warpMode;
    Knob oct, semi, fine, level, pan, pw, uni, det, spread, drift, phase, wtPos, warpAmt;
    Toggle randPhase;
    WaveformDisplay display;
    WavetableDisplay wtDisplay;
    juce::TextButton browserButton, prevButton, nextButton;
    std::unique_ptr<LockButton> lock;
    std::atomic<float>* engineRaw = nullptr;
    std::atomic<bool> engineDirty { false };
};

//==============================================================================
class SubSection : public SectionPanel
{
public:
    explicit SubSection (juce::AudioProcessorValueTreeState& apvts)
        : SectionPanel ("SUB"),
          on ("ON"), wave ("", false), level ("LEVEL"), pan ("PAN")
    {
        addAndMakeVisible (on);
        addAndMakeVisible (wave);
        addAndMakeVisible (level);
        addAndMakeVisible (pan);
        on.attach (apvts, ids::subOn);
        wave.attach (apvts, ids::subWave);
        level.attach (apvts, ids::subLevel);
        pan.attach (apvts, ids::subPan);
    }

    void resized() override
    {
        auto c = content();
        on.setBounds (52, 2, 46, 18);
        wave.setBounds (c.getX(), c.getY() + c.getHeight() / 2 - 11, 82, 22);
        auto knobs = c.withTrimmedLeft (88);
        auto cells = rowCells (knobs, 2, 3);
        level.setBounds (cells[0]);
        pan.setBounds (cells[1]);
    }

private:
    Toggle on;
    Selector wave;
    Knob level, pan;
};

//==============================================================================
class NoiseSection : public SectionPanel
{
public:
    explicit NoiseSection (juce::AudioProcessorValueTreeState& apvts)
        : SectionPanel ("NOISE", theme::accent2),
          type ("", false), level ("LEVEL"), tone ("TONE"), pan ("PAN")
    {
        addAndMakeVisible (type);
        for (Knob* k : { &level, &tone, &pan })
        {
            k->setAccent (theme::accent2);
            addAndMakeVisible (*k);
        }
        type.attach (apvts, ids::noiseType);
        level.attach (apvts, ids::noiseLevel);
        tone.attach (apvts, ids::noiseTone);
        pan.attach (apvts, ids::noisePan);
    }

    void resized() override
    {
        auto c = content();
        type.setBounds (c.getX(), c.getY() + c.getHeight() / 2 - 11, 78, 22);
        auto knobs = c.withTrimmedLeft (84);
        auto cells = rowCells (knobs, 3, 3);
        level.setBounds (cells[0]);
        tone.setBounds (cells[1]);
        pan.setBounds (cells[2]);
    }

private:
    Selector type;
    Knob level, tone, pan;
};

} // namespace ydc
