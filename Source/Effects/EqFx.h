// GLOBUS — 3-band output EQ: low shelf 120 Hz, mid peak 1 kHz, high shelf 8 kHz.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Engine/DspUtils.h"

namespace ydc
{
class EqFx
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        lastLow = lastMid = lastHigh = -1000.0f;
        for (auto& f : filters) f.reset();
        update (0.0f, 0.0f, 0.0f);
    }

    void reset()
    {
        for (auto& f : filters) f.reset();
    }

    void process (float* l, float* r, int n, float lowDb, float midDb, float highDb)
    {
        update (lowDb, midDb, highDb);
        filters[0].processSamples (l, n);
        filters[1].processSamples (l, n);
        filters[2].processSamples (l, n);
        filters[3].processSamples (r, n);
        filters[4].processSamples (r, n);
        filters[5].processSamples (r, n);
    }

private:
    void update (float lowDb, float midDb, float highDb)
    {
        if (! juce::exactlyEqual (lowDb, lastLow))
        {
            const auto c = juce::IIRCoefficients::makeLowShelf (sr, 120.0, 0.707, juce::Decibels::decibelsToGain (lowDb));
            filters[0].setCoefficients (c);
            filters[3].setCoefficients (c);
            lastLow = lowDb;
        }
        if (! juce::exactlyEqual (midDb, lastMid))
        {
            const auto c = juce::IIRCoefficients::makePeakFilter (sr, 1000.0, 0.8, juce::Decibels::decibelsToGain (midDb));
            filters[1].setCoefficients (c);
            filters[4].setCoefficients (c);
            lastMid = midDb;
        }
        if (! juce::exactlyEqual (highDb, lastHigh))
        {
            const auto c = juce::IIRCoefficients::makeHighShelf (sr, 8000.0, 0.707, juce::Decibels::decibelsToGain (highDb));
            filters[2].setCoefficients (c);
            filters[5].setCoefficients (c);
            lastHigh = highDb;
        }
    }

    double sr = 44100.0;
    juce::IIRFilter filters[6]; // [L: low, mid, high][R: low, mid, high]
    float lastLow = -1000.0f, lastMid = -1000.0f, lastHigh = -1000.0f;
};

} // namespace ydc
