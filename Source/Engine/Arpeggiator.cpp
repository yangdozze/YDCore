#include "Arpeggiator.h"

namespace ydc
{
void Arpeggiator::prepare (double sampleRate)
{
    sr = sampleRate;
    rng.seed (0xA4B3C2D1u);
    reset();
}

void Arpeggiator::reset()
{
    heldCount = 0;
    physicalCount = 0;
    seqLen = seqPos = 0;
    samplesToNextStep = 0.0;
    gateRemaining = -1.0;
    currentArpNote = -1;
    wasOn = false;
}

void Arpeggiator::noteOn (int note, float vel, bool hold)
{
    if (hold && physicalCount == 0)
        heldCount = 0;                       // new chord replaces the latched one
    ++physicalCount;

    // sorted insert (ignore overflow / duplicates)
    for (int i = 0; i < heldCount; ++i)
        if (held[i] == note) { heldVel[i] = vel; return; }
    if (heldCount >= kMaxHeld)
        return;
    int pos = heldCount;
    while (pos > 0 && held[pos - 1] > note)
    {
        held[pos] = held[pos - 1];
        heldVel[pos] = heldVel[pos - 1];
        --pos;
    }
    held[pos] = note;
    heldVel[pos] = vel;
    ++heldCount;
}

void Arpeggiator::noteOff (int note, bool hold)
{
    physicalCount = std::max (0, physicalCount - 1);
    if (hold)
        return;                              // latched: keep in the pattern
    for (int i = 0; i < heldCount; ++i)
        if (held[i] == note)
        {
            std::copy (held + i + 1, held + heldCount, held + i);
            std::copy (heldVel + i + 1, heldVel + heldCount, heldVel + i);
            --heldCount;
            break;
        }
}

void Arpeggiator::buildSequence (ArpMode mode, int octaves)
{
    seqLen = 0;
    if (heldCount == 0)
        return;

    // ascending base sequence across octaves
    int asc[kMaxSeq];
    int ascLen = 0;
    for (int o = 0; o < octaves; ++o)
        for (int i = 0; i < heldCount; ++i)
        {
            const int n = held[i] + 12 * o;
            if (n <= 127 && ascLen < kMaxSeq)
                asc[ascLen++] = i | (o << 8); // store index + octave to keep velocity mapping
        }

    auto push = [this] (int coded) { if (seqLen < kMaxSeq) seq[seqLen++] = coded; };

    switch (mode)
    {
        case ArpMode::Up:
            for (int i = 0; i < ascLen; ++i) push (asc[i]);
            break;
        case ArpMode::Down:
            for (int i = ascLen - 1; i >= 0; --i) push (asc[i]);
            break;
        case ArpMode::UpDown:
            for (int i = 0; i < ascLen; ++i) push (asc[i]);
            for (int i = ascLen - 2; i >= 1; --i) push (asc[i]);
            break;
        case ArpMode::Random:
            for (int i = 0; i < ascLen; ++i) push (asc[i]); // pool; picked randomly per step
            break;
        default: break;
    }
    if (seqPos >= seqLen)
        seqPos = 0;
}

void Arpeggiator::stopCurrent (juce::MidiBuffer& out, int samplePos)
{
    if (currentArpNote >= 0)
    {
        out.addEvent (juce::MidiMessage::noteOff (1, currentArpNote), samplePos);
        currentArpNote = -1;
    }
    gateRemaining = -1.0;
}

void Arpeggiator::process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples,
                           double bpm, bool hostPlaying, double ppqPosition, const ParamRefs& params)
{
    const bool on = params.arpOn->load() > 0.5f;

    if (! on)
    {
        if (wasOn)
        {
            // flush: silence the pattern note and any latched state
            stopCurrent (out, 0);
            out.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            heldCount = 0;
            physicalCount = 0;
            seqLen = 0;
            wasOn = false;
        }
        out.addEvents (in, 0, numSamples, 0);
        return;
    }
    wasOn = true;

    const bool hold     = params.arpHold->load() > 0.5f;
    const auto mode     = (ArpMode) (int) params.arpMode->load();
    const int  octaves  = juce::jlimit (1, 3, (int) params.arpOct->load());
    const float gate    = params.arpGate->load();
    const int  divIdx   = juce::jlimit (0, (int) (sizeof (choices::arpDivBeats) / sizeof (float)) - 1,
                                        (int) params.arpDiv->load());
    const double beats  = (double) choices::arpDivBeats[divIdx];
    const double stepSamples = std::max (1.0, sr * 60.0 / std::max (20.0, bpm) * beats);

    // Phase-lock to the host grid while the transport runs (handles tempo changes & loops)
    if (hostPlaying && ppqPosition >= 0.0)
    {
        const double posInStep = std::fmod (ppqPosition, beats) / beats;   // 0..1
        samplesToNextStep = (1.0 - posInStep) * stepSamples;
        if (samplesToNextStep < 1.0)
            samplesToNextStep = stepSamples;
    }
    else if (samplesToNextStep > stepSamples)
        samplesToNextStep = stepSamples;     // tempo/div change while free-running

    // ---- consume input events; note on/off feed the pattern, everything else passes through
    struct Ev { int pos; bool isOn; int note; float vel; };
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int pos = juce::jlimit (0, std::max (0, numSamples - 1), meta.samplePosition);
        if (m.isNoteOn())
        {
            const bool hadNotes = heldCount > 0;
            noteOn (m.getNoteNumber(), m.getFloatVelocity(), hold);
            buildSequence (mode, octaves);
            if (! hadNotes)
            {
                // first note starts the pattern immediately on the next sample boundary
                if (! (hostPlaying && ppqPosition >= 0.0))
                    samplesToNextStep = (double) (pos - 0) + 1.0;
            }
        }
        else if (m.isNoteOff())
        {
            noteOff (m.getNoteNumber(), hold);
            buildSequence (mode, octaves);
        }
        else
        {
            out.addEvent (m, pos);
        }
    }

    // ---- run the step clock through this block
    double t = 0.0;
    while (t < (double) numSamples)
    {
        double nextEvent = samplesToNextStep;
        if (gateRemaining >= 0.0)
            nextEvent = std::min (nextEvent, gateRemaining);

        if (t + nextEvent >= (double) numSamples)
        {
            const double adv = (double) numSamples - t;
            samplesToNextStep -= adv;
            if (gateRemaining >= 0.0)
                gateRemaining -= adv;
            break;
        }

        t += nextEvent;
        samplesToNextStep -= nextEvent;
        if (gateRemaining >= 0.0)
            gateRemaining -= nextEvent;

        const int pos = juce::jlimit (0, numSamples - 1, (int) t);

        if (gateRemaining >= 0.0 && gateRemaining <= 0.0001)
        {
            stopCurrent (out, pos);
        }

        if (samplesToNextStep <= 0.0001)
        {
            samplesToNextStep = stepSamples;
            stopCurrent (out, pos);          // safety: never overlap pattern notes

            if (seqLen > 0)
            {
                if (mode == ArpMode::Random)
                    seqPos = (int) (rng.next() % (uint32_t) seqLen);

                const int coded = seq[seqPos % seqLen];
                const int idx = coded & 0xFF;
                const int oct = coded >> 8;
                if (idx < heldCount)
                {
                    const int n = juce::jlimit (0, 127, held[idx] + 12 * oct);
                    currentVel = heldVel[idx];
                    currentArpNote = n;
                    out.addEvent (juce::MidiMessage::noteOn (1, n, currentVel), pos);
                    gateRemaining = std::max (16.0, stepSamples * (double) gate) - 1.0;
                }
                if (mode != ArpMode::Random)
                    seqPos = seqLen > 0 ? (seqPos + 1) % seqLen : 0;
            }
        }
    }
}

} // namespace ydc
