#include "Voice.h"

namespace ydc
{
void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& o : osc) o.prepare (sampleRate);
    for (auto& o : oscHq) o.prepare (sampleRate);
    for (auto& o : oscWt) o.prepare (sampleRate);
    sub.prepare (sampleRate);
    noise.prepare (sampleRate);
    filter.prepare (sampleRate);
    filterHq.prepare (sampleRate);
    ampEnv.prepare (sampleRate);
    filEnv.prepare (sampleRate);
    modEnv.prepare (sampleRate);
    for (auto& l : lfo) l.prepare (sampleRate);
    reset();
}

void Voice::reset()
{
    active = false;
    sustained = false;
    note = -1;
    ampEnv.reset();
    filEnv.reset();
    modEnv.reset();
    filter.reset();
    filterHq.reset();
    ampModPrev = 1.0f;
}

void Voice::startNote (const ParamRefs& params, int midiNote, float vel, uint32_t order_,
                       bool legato, float glideFromNote, float glideTimeSec, uint32_t seed)
{
    if (legato && active)
    {
        changeNoteLegato (midiNote, glideTimeSec);
        return;
    }

    rng.seed (seed);
    note      = midiNote;
    velocity  = clampf (vel, 0.0f, 1.0f);
    order     = order_;
    polyAT    = 0.0f;
    randomVal = rng.bi();
    sustained = false;

    targetNote = (float) midiNote;
    if (glideFromNote >= 0.0f && glideTimeSec > 0.001f && std::abs (glideFromNote - targetNote) > 0.01f)
    {
        currentNote = glideFromNote;
        glideRate   = (targetNote - currentNote) / glideTimeSec;
    }
    else
    {
        currentNote = targetNote;
        glideRate   = 0.0f;
    }

    // reset per-note state: oscillator phases, sub, noise, LFO retrigger & fades
    for (int i = 0; i < 2; ++i)
        osc[i].reset (rng,
                      params.osc[(size_t) i].randPhase->load() > 0.5f,
                      params.osc[(size_t) i].phase->load());

    // v1.2 engines use a DERIVED rng so the legacy random sequence (sub/noise/LFO
    // seeds below) stays byte-identical for old presets.
    {
        Rng hqRng;
        hqRng.seed (seed * 2654435761u ^ 0x1F123BB5u);
        for (int i = 0; i < 2; ++i)
        {
            const bool rnd = params.osc[(size_t) i].randPhase->load() > 0.5f;
            const float ph = params.osc[(size_t) i].phase->load();
            oscHq[i].reset (hqRng, rnd, ph);
            oscWt[i].reset (hqRng, rnd, ph);
            wtPosState[i] = clampf (params.osc[(size_t) i].wtPos->load(), 0.0f, 1.0f);
            warpState[i]  = clampf (params.osc[(size_t) i].warpAmt->load(), 0.0f, 1.0f);
        }
    }

    sub.reset (rng);
    noise.reset (rng);

    for (int i = 0; i < 2; ++i)
    {
        const auto& lp = params.lfo[(size_t) i];
        lfo[i].trigger (lp.phase->load(), true, lp.fade->load(), rng.next());
        lfoFadeElapsed[i] = 0.0f;
        lastLfoRateMul[i] = 1.0f;
    }

    active = true;
    ampEnv.noteOn();
    filEnv.noteOn();
    modEnv.noteOn();
    // filter state continues (no reset → no click); envelopes resume from current level
    ampModPrev = 1.0f;
}

void Voice::changeNoteLegato (int midiNote, float glideTimeSec)
{
    note = midiNote;
    const float from = currentNote;
    targetNote = (float) midiNote;
    if (glideTimeSec > 0.001f && std::abs (from - targetNote) > 0.01f)
    {
        currentNote = from;
        glideRate   = (targetNote - currentNote) / glideTimeSec;
    }
    else
    {
        currentNote = targetNote;
        glideRate   = 0.0f;
    }
}

void Voice::stopNote()
{
    ampEnv.noteOff();
    filEnv.noteOff();
    modEnv.noteOff();
}

void Voice::fastSteal()
{
    ampEnv.fastRelease();
    filEnv.fastRelease();
    modEnv.fastRelease();
    sustained = false;
}

void Voice::render (float* L, float* R, int absStart, int numSamples, const RenderContext& ctx)
{
    if (! active)
        return;

    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min (kControlBlock, numSamples - done);
        renderChunk (L + absStart + done, R + absStart + done, absStart + done, n, ctx);
        done += n;
        if (! active)
            break;
    }
}

void Voice::renderChunk (float* L, float* R, int absStart, int n, const RenderContext& ctx)
{
    const auto& P = *ctx.params;

    // ---- envelope parameter refresh (cheap when unchanged)
    ampEnv.setParams (P.amp.a->load(), P.amp.d->load(), P.amp.s->load(), P.amp.r->load());
    filEnv.setParams (P.fil.a->load(), P.fil.d->load(), P.fil.s->load(), P.fil.r->load());
    modEnv.setParams (P.mod.a->load(), P.mod.d->load(), P.mod.s->load(), P.mod.r->load());
    ampEnv.setCurves (P.amp.curveA->load(), P.amp.curveD->load(), P.amp.curveR->load());
    filEnv.setCurves (P.fil.curveA->load(), P.fil.curveD->load(), P.fil.curveR->load());
    modEnv.setCurves (P.mod.curveA->load(), P.mod.curveD->load(), P.mod.curveR->load());

    // ---- glide
    if (! exactEq (glideRate, 0.0f))
    {
        currentNote += glideRate * (float) n / (float) sr;
        if ((glideRate > 0.0f && currentNote >= targetNote) || (glideRate < 0.0f && currentNote <= targetNote))
        {
            currentNote = targetNote;
            glideRate = 0.0f;
        }
    }

    // ---- LFOs
    float lfoVal[2];
    bool  lfoBip[2];
    for (int i = 0; i < 2; ++i)
    {
        const auto& lp = P.lfo[(size_t) i];
        const auto wave   = (LfoWave) (int) lp.wave->load();
        const bool sync   = lp.sync->load() > 0.5f;
        const bool retrig = lp.retrig->load() > 0.5f;
        const bool bip    = lp.bipolar->load() > 0.5f;
        const float fadeT = lp.fade->load();
        const float phOfs = lp.phase->load();
        lfoBip[i] = bip;

        float raw;
        if (retrig)
        {
            const float rateHz = Lfo::effectiveRate (sync, lp.rate->load(), (int) lp.div->load(), ctx.bpm)
                               * lastLfoRateMul[i];
            raw = lfo[i].tick (n, rateHz, wave);
        }
        else
        {
            // free-running: derive value from the engine's master phase (all voices aligned)
            const double pt = ctx.lfoBasePhase[i]
                            + (double) ctx.lfoRateHz[i] * ((double) absStart / sr)
                            + (double) phOfs;
            const double frac = pt - std::floor (pt);
            lfoFadeElapsed[i] += (float) n / (float) sr;
            const float fade = fadeT > 0.001f ? clampf (lfoFadeElapsed[i] / fadeT, 0.0f, 1.0f) : 1.0f;
            raw = Lfo::shape (wave, frac, hashNoise ((uint64_t) (int64_t) std::floor (pt))) * fade;
        }
        lfoVal[i] = Lfo::mapPolarity (raw, bip);
    }

    // ---- modulation sources & matrix
    ModSourcesState st;
    st.velocity   = velocity;
    st.modWheel   = ctx.modWheel;
    st.aftertouch = std::max (ctx.aftertouch, polyAT);
    st.keyTrack   = clampf ((currentNote - 60.0f) / 30.0f, -1.0f, 1.0f);
    st.ampEnv     = ampEnv.getLevel();
    st.filEnv     = filEnv.getLevel();
    st.modEnv     = modEnv.getLevel();
    st.lfo[0]     = lfoVal[0];  st.lfo[1] = lfoVal[1];
    st.lfoBipolar[0] = lfoBip[0]; st.lfoBipolar[1] = lfoBip[1];
    st.random     = randomVal;
    st.pitchBend  = ctx.pitchBendNorm;

    ModValues mv;
    evaluateMatrix (ctx.slots, 8, st, mv);
    for (int i = 0; i < 2; ++i)
        applyLfoQuickAssign ((LfoDest) (int) P.lfo[(size_t) i].dest->load(),
                             P.lfo[(size_t) i].amount->load(), lfoVal[i], mv);
    applyModEnv ((ModEnvDest) (int) P.modEnvDest->load(), P.modEnvAmt->load(), modEnv.getLevel(), mv);
    lastLfoRateMul[0] = mv.lfoRateMul[0];
    lastLfoRateMul[1] = mv.lfoRateMul[1];

    // ---- pitch base
    const float pbSemis = ctx.pitchBendNorm * ctx.pitchBendRange;

    // ---- oscillators into local scratch
    float tmpL[kControlBlock] {};
    float tmpR[kControlBlock] {};

    for (int i = 0; i < 2; ++i)
    {
        const auto& op = P.osc[(size_t) i];
        if (op.on->load() < 0.5f)
            continue;

        OscBlockParams bp;
        bp.wave        = (OscWave) (int) op.wave->load();
        bp.pw          = clampf (op.pw->load() + mv.pwAdd[i], 0.05f, 0.95f);
        bp.detuneCents = op.uniDetune->load();
        bp.spread      = op.uniSpread->load();
        bp.pan         = clampf (op.pan->load() + mv.panAdd[i], -1.0f, 1.0f);
        bp.gain        = clampf (op.level->load() + mv.levelAdd[i], 0.0f, 1.25f);
        bp.uniCount    = (int) op.uniCount->load();
        bp.drift       = op.drift->load();

        const float noteForOsc = currentNote + pbSemis
                               + op.oct->load() * 12.0f + op.semi->load()
                               + op.fine->load() * 0.01f
                               + mv.pitchSemis[i] + mv.fineSemis[i];
        bp.freqHz = noteToHz (clampf (noteForOsc, -12.0f, 135.0f));

        const auto engine = (OscEngine) (int) op.engine->load();
        if (engine == OscEngine::Legacy)
        {
            osc[i].render (tmpL, tmpR, n, bp);   // untouched 1.1 path
        }
        else
        {
            // v1.2 engines respond to the appended modulation destinations
            bp.detuneCents = clampf (op.uniDetune->load() + mv.detuneAdd[i], 0.0f, 100.0f);
            bp.spread      = clampf (op.uniSpread->load() + mv.spreadAdd[i], 0.0f, 1.0f);

            if (engine == OscEngine::BasicHQ)
            {
                if (ctx.hqShapes != nullptr)
                    oscHq[i].render (tmpL, tmpR, n, bp, *ctx.hqShapes);
            }
            else
            {
                HqBlockParams hp;
                hp.bank     = ctx.wtBank[i];
                hp.quality  = ctx.quality;
                hp.warpMode = (WarpMode) (int) op.warpMode->load();
                const float posT  = clampf (op.wtPos->load()   + mv.wtPosAdd[i], 0.0f, 1.0f);
                const float warpT = clampf (op.warpAmt->load() + mv.warpAdd[i],  0.0f, 1.0f);
                hp.posStart  = wtPosState[i];  hp.posEnd  = posT;   wtPosState[i] = posT;
                hp.warpStart = warpState[i];   hp.warpEnd = warpT;  warpState[i]  = warpT;
                oscWt[i].render (tmpL, tmpR, n, bp, hp);
            }
        }
    }

    if (P.subOn->load() > 0.5f)
        sub.render (tmpL, tmpR, n,
                    noteToHz (clampf (currentNote + pbSemis - 12.0f, -12.0f, 135.0f)),
                    P.subWave->load() > 0.5f,
                    P.subLevel->load(),
                    P.subPan->load());

    {
        const float nl = clampf (P.noiseLevel->load() + mv.noiseLevelAdd, 0.0f, 1.0f);
        if (nl > 0.0001f)
            noise.render (tmpL, tmpR, n, P.noiseType->load() > 0.5f, nl, P.noiseTone->load(),
                          P.noisePan->load());
    }

    // ---- filter
    if (P.filterOn->load() > 0.5f)
    {
        const float keyOct = P.keyTrack->load() * (currentNote - 60.0f) / 12.0f;
        const float envOct = P.filterEnvAmt->load() * modScale::cutoffOct * filEnv.getLevel();
        const float fc = P.cutoff->load() * std::exp2 (keyOct + envOct + mv.cutoffOct);
        const auto ftype = (FilterType) (int) P.filterType->load();
        const float res = clampf (P.resonance->load() + mv.resoAdd, 0.0f, 1.0f);

        if (ftype == FilterType::Ladder24 || ftype == FilterType::Ota24 || ftype == FilterType::Sem12)
        {
            // v1.2 models (appended choices — unreachable from legacy presets).
            // Drive responds to the appended Filter Drive mod destination and the
            // nonlinear cores oversample at HIGH/ULTRA quality.
            const bool os = ctx.quality == QualityMode::High || ctx.quality == QualityMode::Ultra;
            filterHq.setParams (ftype, fc,
                                res,
                                clampf (P.filterDrive->load() + mv.driveAdd, 0.0f, 1.0f),
                                os);
            filterHq.process (tmpL, tmpR, n);
        }
        else
        {
            filter.setParams (ftype, fc, res, P.filterDrive->load());
            filter.process (tmpL, tmpR, n);
        }
    }

    // ---- amplitude (per-sample envelope, ramped mod gain)
    const float velGain = 1.0f - P.velAmount->load() * (1.0f - velocity);
    const float ampModTarget = clampf (1.0f + mv.ampAdd, 0.0f, 2.0f);
    const float ampModStep = (ampModTarget - ampModPrev) / (float) n;
    float m = ampModPrev;

    for (int s = 0; s < n; ++s)
    {
        const float ae = ampEnv.process();
        filEnv.process();
        modEnv.process();
        m += ampModStep;
        const float g = ae * velGain * m * 0.35f;   // headroom for chord stacking
        L[s] += tmpL[s] * g;
        R[s] += tmpR[s] * g;
    }
    ampModPrev = ampModTarget;

    if (! ampEnv.isActive())
        active = false;
}

} // namespace ydc
