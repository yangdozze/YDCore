#include "SynthEngine.h"

namespace ydc
{
void SynthEngine::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);
    sr = sampleRate;
    for (auto& v : voices)
        v.prepare (sampleRate);
    wheelSm.prepare ((float) sampleRate, 0.015f, 0.0f);
    atSm.prepare ((float) sampleRate, 0.015f, 0.0f);
    pbSm.prepare ((float) sampleRate, 0.004f, 0.0f);
    seedRng.seed (0xC0FFEEu);
    reset();
}

void SynthEngine::reset()
{
    for (auto& v : voices)
        v.reset();
    monoCount = 0;
    pendingCount = 0;
    pedalDown = false;
    monoSoundingFromPedal = false;
    std::fill (std::begin (physicallyHeld), std::end (physicallyHeld), false);
    masterPhaseTotal[0] = masterPhaseTotal[1] = 0.0;
    wheelSm.snap (wheelTarget);
    atSm.snap (atTarget);
    pbSm.snap (0.0f);
    activeVoiceCount.store (0, std::memory_order_relaxed);
}

//==============================================================================
void SynthEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi,
                           double bpm, const ParamRefs& params)
{
    const int numSamples = out.getNumSamples();
    out.clear();

    // ---- decode matrix slots once per block
    for (int i = 0; i < 8; ++i)
    {
        slots[i].src     = (ModSource) (int) params.slot[(size_t) i].src->load();
        slots[i].dst     = (ModDest)   (int) params.slot[(size_t) i].dst->load();
        slots[i].amount  = params.slot[(size_t) i].amt->load();
        slots[i].bipolar = params.slot[(size_t) i].bipolar->load() > 0.5f;
    }

    // ---- play mode change flush
    const auto mode = (PlayMode) (int) params.playMode->load();
    if (mode != lastMode)
    {
        modeChangeFlush();
        lastMode = mode;
    }

    // ---- smoothed global controllers (block rate)
    const float wheel = wheelSm.processTowards (wheelTarget, numSamples);
    const float at    = atSm.processTowards (atTarget, numSamples);
    const float pb    = pbSm.processTowards (pbTarget, numSamples);

    // ---- master LFO values at block start (for global-destination modulation)
    float masterLfoVal[2];
    bool  masterLfoBip[2];
    float masterRateBase[2];
    for (int i = 0; i < 2; ++i)
    {
        const auto& lp = params.lfo[(size_t) i];
        masterRateBase[i] = Lfo::effectiveRate (lp.sync->load() > 0.5f, lp.rate->load(),
                                                (int) lp.div->load(), bpm);
        const double pt = masterPhaseTotal[i] + (double) lp.phase->load();
        const bool bip = lp.bipolar->load() > 0.5f;
        masterLfoBip[i] = bip;
        masterLfoVal[i] = Lfo::mapPolarity (
            Lfo::shape ((LfoWave) (int) lp.wave->load(), pt - std::floor (pt),
                        hashNoise ((uint64_t) (int64_t) std::floor (pt))),
            bip);
    }

    // ---- global-source modulation (FX mix, stereo width, master LFO rate)
    ModSourcesState gst;
    gst.modWheel   = wheel;
    gst.aftertouch = at;
    gst.pitchBend  = pb;
    gst.keyTrack   = lastPlayedNote >= 0.0f ? clampf ((lastPlayedNote - 60.0f) / 30.0f, -1.0f, 1.0f) : 0.0f;
    gst.lfo[0] = masterLfoVal[0]; gst.lfo[1] = masterLfoVal[1];
    gst.lfoBipolar[0] = masterLfoBip[0]; gst.lfoBipolar[1] = masterLfoBip[1];

    ModValues gmv;
    evaluateMatrix (slots, 8, gst, gmv);
    for (int i = 0; i < 2; ++i)
        applyLfoQuickAssign ((LfoDest) (int) params.lfo[(size_t) i].dest->load(),
                             params.lfo[(size_t) i].amount->load(), masterLfoVal[i], gmv);
    fxMixScale = clampf (1.0f + gmv.fxMixAdd, 0.0f, 2.0f);
    widthMod   = clampf (gmv.widthAdd, -1.0f, 1.0f);

    // ---- render context
    ctx.params         = &params;
    ctx.slots          = slots;
    ctx.sampleRate     = sr;
    ctx.bpm            = bpm;
    ctx.modWheel       = wheel;
    ctx.aftertouch     = at;
    ctx.pitchBendNorm  = pb;
    ctx.pitchBendRange = params.pitchBendRange->load();
    for (int i = 0; i < 2; ++i)
    {
        ctx.lfoBasePhase[i] = masterPhaseTotal[i];
        ctx.lfoRateHz[i]    = masterRateBase[i] * gmv.lfoRateMul[i];
    }

    // ---- event loop: render segments between sample-accurate MIDI events
    int prev = 0;
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (0, std::max (0, numSamples - 1), meta.samplePosition);
        if (pos > prev)
        {
            renderSegment (out, prev, pos - prev);
            prev = pos;
        }
        startPendingNotes (params);
        handleMidiEvent (meta.getMessage(), params);
    }
    if (prev < numSamples)
        renderSegment (out, prev, numSamples - prev);
    startPendingNotes (params);

    // ---- expire pending steals (safety: force-start if a voice never freed)
    for (int i = 0; i < pendingCount;)
    {
        pending[i].ttl -= numSamples;
        if (pending[i].ttl <= 0)
        {
            if (auto* victim = chooseStealVictim())
            {
                victim->reset();
                victim->startNote (params, pending[i].note, pending[i].vel, ++orderCounter,
                                   false, pending[i].glideFrom, params.glideTime->load(), seedRng.next());
            }
            pending[i] = pending[--pendingCount];
        }
        else
            ++i;
    }

    // ---- advance master LFO clocks
    for (int i = 0; i < 2; ++i)
        masterPhaseTotal[i] += (double) ctx.lfoRateHz[i] * numSamples / sr;

    // ---- voice count for UI
    int count = 0;
    for (auto& v : voices)
        if (v.isActive()) ++count;
    activeVoiceCount.store (count, std::memory_order_relaxed);
}

void SynthEngine::renderSegment (juce::AudioBuffer<float>& out, int from, int len)
{
    if (len <= 0)
        return;
    float* L = out.getWritePointer (0);
    float* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : L;
    for (auto& v : voices)
        v.render (L, R, from, len, ctx);
}

//==============================================================================
void SynthEngine::handleMidiEvent (const juce::MidiMessage& m, const ParamRefs& params)
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getFloatVelocity(), params);
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber(), params);
    else if (m.isPitchWheel())
        pbTarget = clampf (((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f, -1.0f, 1.0f);
    else if (m.isChannelPressure())
        atTarget = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch())
    {
        for (auto& v : voices)
            if (v.isActive() && v.getNote() == m.getNoteNumber())
                v.setPolyAftertouch ((float) m.getAfterTouchValue() / 127.0f);
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        if (cc == 1)
            wheelTarget = (float) m.getControllerValue() / 127.0f;
        else if (cc == 64)
            handlePedal (m.getControllerValue() >= 64, params);
        else if (cc == 120)
            allNotesOff (true);
        else if (cc == 123)
            allNotesOff (false);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        allNotesOff (m.isAllSoundOff());
}

//==============================================================================
Voice* SynthEngine::findFreeVoice()
{
    for (auto& v : voices)
        if (! v.isActive())
            return &v;
    return nullptr;
}

Voice* SynthEngine::chooseStealVictim()
{
    // prefer the quietest releasing voice; otherwise the oldest voice
    Voice* best = nullptr;
    float bestLevel = 1.0e9f;
    for (auto& v : voices)
        if (v.isActive() && v.isReleasing())
            if (v.getAmpLevel() < bestLevel) { bestLevel = v.getAmpLevel(); best = &v; }
    if (best != nullptr)
        return best;

    uint32_t oldest = 0xFFFFFFFFu;
    for (auto& v : voices)
        if (v.isActive() && v.getOrder() < oldest) { oldest = v.getOrder(); best = &v; }
    return best;
}

void SynthEngine::startPendingNotes (const ParamRefs& params)
{
    for (int i = 0; i < pendingCount;)
    {
        if (auto* v = findFreeVoice())
        {
            v->startNote (params, pending[i].note, pending[i].vel, ++orderCounter,
                          false, pending[i].glideFrom, params.glideTime->load(), seedRng.next());
            pending[i] = pending[--pendingCount];
        }
        else
            break;
    }
}

//==============================================================================
void SynthEngine::noteOn (int note, float vel, const ParamRefs& params)
{
    if (note < 0 || note > 127)
        return;
    physicallyHeld[note] = true;

    const auto mode = (PlayMode) (int) params.playMode->load();
    const float glideTime = params.glideTime->load();
    // Portamento is a mono/legato behaviour: gliding every poly voice from the
    // previously played note would smear chords, so poly starts at pitch.
    const float glideFrom = (mode != PlayMode::Poly && glideTime > 0.001f && lastPlayedNote >= 0.0f)
                          ? lastPlayedNote : -1.0f;

    if (mode == PlayMode::Poly)
    {
        const int limit = juce::jlimit (1, kMaxVoices, (int) params.polyLimit->load());
        int active = 0;
        for (auto& v : voices)
            if (v.isActive()) ++active;

        Voice* v = active < limit ? findFreeVoice() : nullptr;
        if (v == nullptr)
        {
            if (auto* victim = chooseStealVictim())
                victim->fastSteal();
            if (pendingCount < 8)
                pending[pendingCount++] = { note, vel, (int) (0.02 * sr), glideFrom };
        }
        else
        {
            v->startNote (params, note, vel, ++orderCounter, false, glideFrom, glideTime, seedRng.next());
        }
    }
    else
    {
        // mono / legato: maintain the press-order stack
        for (int i = 0; i < monoCount; ++i)
            if (monoStack[i] == note) { std::copy (monoStack + i + 1, monoStack + monoCount, monoStack + i);
                                        std::copy (monoVelo + i + 1, monoVelo + monoCount, monoVelo + i);
                                        --monoCount; break; }
        if (monoCount < 127)
        {
            monoStack[monoCount] = note;
            monoVelo[monoCount]  = vel;
            ++monoCount;
        }
        monoSoundingFromPedal = false;
        resolveMono (params, true);
    }

    lastPlayedNote = (float) note;
}

void SynthEngine::noteOff (int note, const ParamRefs& params)
{
    if (note < 0 || note > 127)
        return;
    physicallyHeld[note] = false;

    const auto mode = (PlayMode) (int) params.playMode->load();

    if (mode == PlayMode::Poly)
    {
        // Note-off ownership: one note-off releases exactly ONE voice of that
        // pitch (the oldest still-held one). Repeated same-pitch notes each own
        // their voice, so on/off pairs match one-to-one.
        Voice* victim = nullptr;
        for (auto& v : voices)
            if (v.isActive() && v.getNote() == note && ! v.isReleasing() && ! v.isSustained())
                if (victim == nullptr || v.getOrder() < victim->getOrder())
                    victim = &v;
        if (victim != nullptr)
        {
            if (pedalDown)
                victim->setSustained (true);
            else
                victim->stopNote();
        }
    }
    else
    {
        for (int i = 0; i < monoCount; ++i)
            if (monoStack[i] == note)
            {
                std::copy (monoStack + i + 1, monoStack + monoCount, monoStack + i);
                std::copy (monoVelo + i + 1, monoVelo + monoCount, monoVelo + i);
                --monoCount;
                break;
            }

        if (monoCount == 0)
        {
            if (pedalDown)
                monoSoundingFromPedal = true;
            else if (voices[0].isActive())
                voices[0].stopNote();
        }
        else
            resolveMono (params, false);
    }
}

void SynthEngine::resolveMono (const ParamRefs& params, bool freshPress)
{
    if (monoCount == 0)
        return;

    // note priority
    const int priority = (int) params.notePriority->load(); // 0 last, 1 high, 2 low
    int idx = monoCount - 1;
    if (priority == 1)
    {
        for (int i = 0; i < monoCount; ++i)
            if (monoStack[i] > monoStack[idx]) idx = i;
    }
    else if (priority == 2)
    {
        for (int i = 0; i < monoCount; ++i)
            if (monoStack[i] < monoStack[idx]) idx = i;
    }

    const int   target = monoStack[idx];
    const float vel    = monoVelo[idx];
    const auto  mode   = (PlayMode) (int) params.playMode->load();
    const float glideTime = params.glideTime->load();
    auto& v = voices[0];

    if (! v.isActive())
    {
        const float glideFrom = (glideTime > 0.001f && lastPlayedNote >= 0.0f) ? lastPlayedNote : -1.0f;
        v.startNote (params, target, vel, ++orderCounter, false, glideFrom, glideTime, seedRng.next());
        return;
    }

    if (v.getNote() == target)
        return;

    if (mode == PlayMode::Legato || ! freshPress)
        v.changeNoteLegato (target, glideTime);        // no envelope retrigger
    else
        v.startNote (params, target, vel, ++orderCounter, false, v.getCurrentNote(), glideTime, seedRng.next());
}

void SynthEngine::handlePedal (bool down, const ParamRefs& params)
{
    if (down == pedalDown)
        return;
    pedalDown = down;
    if (down)
        return;

    // pedal released: stop everything that is only sounding because of the pedal
    for (auto& v : voices)
        if (v.isActive() && v.isSustained() && v.getNote() >= 0 && ! physicallyHeld[v.getNote()])
        {
            v.setSustained (false);
            v.stopNote();
        }

    const auto mode = (PlayMode) (int) params.playMode->load();
    if (mode != PlayMode::Poly && monoSoundingFromPedal && monoCount == 0 && voices[0].isActive())
    {
        voices[0].stopNote();
        monoSoundingFromPedal = false;
    }
}

void SynthEngine::allNotesOff (bool hard)
{
    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;
        if (hard)
            v.fastSteal();
        else
            v.stopNote();
    }
    monoCount = 0;
    monoSoundingFromPedal = false;
    pendingCount = 0;
    std::fill (std::begin (physicallyHeld), std::end (physicallyHeld), false);
}

void SynthEngine::modeChangeFlush()
{
    allNotesOff (false);
}

} // namespace ydc
