#include "WavetableImport.h"

namespace ydc
{
namespace
{
    constexpr juce::int64 kMaxFileBytes   = 64 * 1024 * 1024;   // hard input cap
    constexpr int         kMaxTotalFrames = kWtMaxFrames;       // 256
    constexpr int         kMaxSingleCycle = 65536;

    /** Catmull-Rom resample of one cycle to kWtFrameSize samples (cyclic). */
    std::vector<float> resampleCycle (const float* src, int srcLen)
    {
        std::vector<float> out ((size_t) kWtFrameSize);
        if (srcLen == kWtFrameSize)
        {
            std::copy (src, src + srcLen, out.begin());
            return out;
        }
        auto at = [src, srcLen] (int i) noexcept
        {
            i %= srcLen;
            if (i < 0) i += srcLen;
            return src[i];
        };
        for (int i = 0; i < kWtFrameSize; ++i)
        {
            const double p = (double) i * srcLen / kWtFrameSize;
            const int i1 = (int) p;
            const float t = (float) (p - i1);
            const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
            const float a = 0.5f * (3.0f * (y1 - y2) + y3 - y0);
            const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c = 0.5f * (y2 - y0);
            out[(size_t) i] = ((a * t + b) * t + c) * t + y1;
        }
        return out;
    }
}

juce::File userWavetableDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
              .getChildFile ("GLOBUS").getChildFile ("Wavetables");
}

juce::File copyToWavetableAssets (const juce::File& source)
{
    auto dir = userWavetableDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory())
        return {};

    const auto base = juce::File::createLegalFileName (source.getFileNameWithoutExtension())
                          .substring (0, 40);
    auto target = dir.getChildFile (base + ".wav");

    if (target.getFullPathName() == source.getFullPathName())
        return target;                                   // importing from the asset dir itself

    if (target.existsAsFile())
    {
        if (target.hasIdenticalContentTo (source))
            return target;                               // identical content already stored
        for (int n = 2; n < 100; ++n)
        {
            auto numbered = dir.getChildFile (base + " " + juce::String (n) + ".wav");
            if (! numbered.existsAsFile())
            {
                target = numbered;
                break;
            }
            if (numbered.hasIdenticalContentTo (source))
                return numbered;
        }
    }
    return source.copyFileTo (target) ? target : juce::File();
}

WavetableImportResult importWavetableFromFile (const juce::File& file, int frameSizeHint)
{
    WavetableImportResult r;

    if (! file.existsAsFile())
    {
        r.error = "File not found: " + file.getFileName();
        return r;
    }
    if (file.getSize() <= 44)                     // smaller than any valid WAV header
    {
        r.error = "File is empty or truncated: " + file.getFileName();
        return r;
    }
    if (file.getSize() > kMaxFileBytes)
    {
        r.error = "File too large (limit 64 MB): " + file.getFileName();
        return r;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (
        wav.createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr)
    {
        r.error = "Not a readable WAV file: " + file.getFileName();
        return r;
    }
    if (reader->numChannels < 1 || reader->numChannels > 2)
    {
        r.error = "Unsupported channel count (" + juce::String ((int) reader->numChannels)
                + ") — mono or stereo only.";
        return r;
    }
    const auto totalLen = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                         (juce::int64) kWtFrameSize * kMaxTotalFrames * 2 + 1);
    if (totalLen < 16)
    {
        r.error = "Audio too short to contain a waveform.";
        return r;
    }
    r.wasStereo = reader->numChannels == 2;

    // read + downmix (documented policy: stereo → average of both channels)
    juce::AudioBuffer<float> buf ((int) reader->numChannels, totalLen);
    if (! reader->read (&buf, 0, totalLen, 0, true, true))
    {
        r.error = "WAV data could not be read (file may be truncated).";
        return r;
    }
    std::vector<float> mono ((size_t) totalLen);
    if (r.wasStereo)
    {
        const float* l = buf.getReadPointer (0);
        const float* rr = buf.getReadPointer (1);
        for (int i = 0; i < totalLen; ++i)
            mono[(size_t) i] = 0.5f * (l[i] + rr[i]);
    }
    else
    {
        const float* l = buf.getReadPointer (0);
        std::copy (l, l + totalLen, mono.begin());
    }
    for (float v : mono)
        if (! std::isfinite (v))
        {
            r.error = "WAV contains non-finite samples.";
            return r;
        }

    // ---- frame segmentation
    int frameSize = 0;
    if (frameSizeHint > 0)
    {
        if (totalLen % frameSizeHint != 0)
        {
            r.error = "Length " + juce::String (totalLen) + " is not divisible by the requested frame size "
                    + juce::String (frameSizeHint) + ".";
            return r;
        }
        frameSize = frameSizeHint;
    }
    else
    {
        for (int candidate : { kWtFrameSize, 4096, 1024, 512, 256 })
            if (totalLen % candidate == 0 && totalLen / candidate <= kMaxTotalFrames)
            {
                frameSize = candidate;
                break;
            }
        if (frameSize == 0)
        {
            if (totalLen <= kMaxSingleCycle)
                frameSize = totalLen;                    // whole file = one cycle
            else
            {
                r.error = "Cannot infer the frame layout: length " + juce::String (totalLen)
                        + " is not a multiple of 2048 (or 4096/1024/512/256) and is too long for a single cycle.";
                return r;
            }
        }
    }

    const int numFrames = totalLen / frameSize;
    if (numFrames > kMaxTotalFrames)
    {
        r.error = "Too many frames (" + juce::String (numFrames) + "; limit " + juce::String (kMaxTotalFrames) + ").";
        return r;
    }
    r.framesDetected = numFrames;
    r.sourceFrameSize = frameSize;

    std::vector<std::vector<float>> frames;
    frames.reserve ((size_t) numFrames);
    for (int f = 0; f < numFrames; ++f)
        frames.push_back (resampleCycle (mono.data() + (size_t) f * (size_t) frameSize, frameSize));

    const auto name = userWavetablePrefix()
                    + juce::File::createLegalFileName (file.getFileNameWithoutExtension()).substring (0, 40);
    r.bank = buildWavetableBank (name, "User",
                                 juce::String (numFrames) + " frame" + (numFrames == 1 ? "" : "s")
                                 + " imported from " + file.getFileName(),
                                 frames, false, WtNormalise::TablePeak);
    r.bank->isFactory = false;
    return r;
}

} // namespace ydc
