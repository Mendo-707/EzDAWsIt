/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "Mastering.h"

namespace ez
{
    namespace
    {
        const juce::StringArray offOn { "Off", "On" };

        std::vector<EffectPlugin::Control> makeControls()
        {
            const auto& first = MasteringChainPlugin::getPresets().front();
            const auto v = [&first] (int c) { return first.values[c]; };
            using M = MasteringChainPlugin;

            return {
                { "low",       "Low",            -6.0f,  6.0f, v (M::lowShelf), 0.0f, "dB", 1 },
                { "lowmids",   "Low mids",       -6.0f,  6.0f, v (M::lowMids),  0.0f, "dB", 1 },
                { "presence",  "Presence",       -6.0f,  6.0f, v (M::presence), 0.0f, "dB", 1 },
                { "air",       "Air",            -6.0f,  6.0f, v (M::air),      0.0f, "dB", 1 },
                { "gluelow",   "Glue low",        0.0f, 100.0f, v (M::glueLow),  0.0f, "%", 0 },
                { "gluemid",   "Glue mid",        0.0f, 100.0f, v (M::glueMid),  0.0f, "%", 0 },
                { "gluehigh",  "Glue high",       0.0f, 100.0f, v (M::glueHigh), 0.0f, "%", 0 },
                { "width",     "Width",           0.0f, 200.0f, v (M::width),    0.0f, "%", 0 },
                { "monobass",  "Mono below 120 Hz", 0.0f, 1.0f, v (M::monoBass), 0.0f, "", 0, offOn },
                { "gain",      "Limiter gain",    0.0f, 18.0f, v (M::gain),     0.0f, "dB", 1 },
                { "ceiling",   "Ceiling",        -3.0f,  0.0f, v (M::ceiling),  0.0f, "dB", 1 },
                { "eqon",      "EQ on",           0.0f, 1.0f, 1.0f, 0.0f, "", 0, offOn },
                { "glueon",    "Multiband on",    0.0f, 1.0f, 1.0f, 0.0f, "", 0, offOn },
                { "stereoon",  "Stereo on",       0.0f, 1.0f, 1.0f, 0.0f, "", 0, offOn },
                { "limiteron", "Limiter on",      0.0f, 1.0f, 1.0f, 0.0f, "", 0, offOn },
            };
        }
    }

    const char* MasteringChainPlugin::xmlTypeName = "ezmastering";

    const std::vector<MasteringChainPlugin::Preset>& MasteringChainPlugin::getPresets()
    {
        //                                                                      Low  LowMid Pres  Air | Glue L  M   H | Width Mono | Gain Ceiling
        static const std::vector<Preset> presets
        {
            { "Gentle (start here)",       "A light touch: a little air and glue",   { 0.0f,  0.0f, 0.0f, 0.5f,  20, 15, 10,  100, 1,  3.0f, -1.0f } },
            { "Streaming, loud & clean",   "Spotify-ready: full, clear, controlled",  { 1.0f, -1.0f, 0.5f, 1.5f,  35, 30, 25,  110, 1,  6.0f, -1.0f } },
            { "Warm & full",               "Rounder low end, softer top",             { 1.5f,  0.0f, -0.5f, -0.5f, 40, 25, 15,  100, 1,  5.0f, -1.0f } },
            { "Bright & wide",             "More sparkle and stereo",                 { 0.0f, -1.0f, 1.5f, 2.5f,  25, 30, 30,  125, 1,  5.0f, -1.0f } },
            { "Club loud",                 "Hits hard on big speakers",               { 1.5f, -1.0f, 0.5f, 1.0f,  50, 40, 35,  105, 1, 10.0f, -0.3f } },
            { "Just a limiter",            "Only louder, nothing else changes",       { 0.0f,  0.0f, 0.0f, 0.0f,   0,  0,  0,  100, 0,  4.0f, -1.0f },
              (1 << eqModule) | (1 << glueModule) | (1 << stereoModule) },
        };

        return presets;
    }

    juce::String MasteringChainPlugin::moduleName (int m)
    {
        static const char* names[] = { "EQ", "Multiband", "Stereo", "Limiter" };
        return juce::isPositiveAndBelow (m, (int) numModules) ? names[m] : "";
    }

    juce::String MasteringChainPlugin::moduleHint (int m)
    {
        static const char* hints[] = { "Tone of the whole song: low end, low mids (mud), presence and air",
                                       "Glues the mix: gentle compression on the lows, mids and highs separately",
                                       "How wide the song is; bass kept in the middle",
                                       "Makes it louder without going over the ceiling" };
        return juce::isPositiveAndBelow (m, (int) numModules) ? hints[m] : "";
    }

    //==============================================================================
    MasteringChainPlugin::MasteringChainPlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, makeControls())
    {
        if (! state.hasProperty ("ezPreset"))
            state.setProperty ("ezPreset", getPresets().front().name, nullptr);

        presetName.referTo (state, "ezPreset", getUndoManager(), getPresets().front().name);
    }

    void MasteringChainPlugin::prepare (double sr, int maxBlock)
    {
        for (auto* f : { &eqLow, &eqLowMids, &eqPresence, &eqAir, &sideHp1, &sideHp2 })
            f->reset();

        crossover.set (sr, lowCrossoverHz, highCrossoverHz);
        crossover.reset();
        sideHp1.set (dsp::Biquad::Type::highPass, sr, monoBelowHz, 0.70710678f);
        sideHp2.set (dsp::Biquad::Type::highPass, sr, monoBelowHz, 0.70710678f);

        for (auto& c : bandComp)
            c.prepare (sr);

        for (auto& b : bands)
            b.setSize (2, juce::jmax (64, maxBlock), false, true, false);

        limiter.releaseMs = 150.0f;   // slower than the track limiter: no pumping on a whole song
        limiter.prepare (sr, maxBlock);

        for (auto& e : lastEq)
            e = -100;

        glueRan = false;
    }

    void MasteringChainPlugin::updateEq()
    {
        const float wanted[4] = { get (lowShelf), get (lowMids), get (presence), get (air) };

        if (std::equal (std::begin (wanted), std::end (wanted), std::begin (lastEq)))
            return;

        std::copy (std::begin (wanted), std::end (wanted), std::begin (lastEq));
        eqLow.set (dsp::Biquad::Type::lowShelf, sampleRate, 100.0f, 0.7f, wanted[0]);
        eqLowMids.set (dsp::Biquad::Type::peak, sampleRate, 300.0f, 1.0f, wanted[1]);
        eqPresence.set (dsp::Biquad::Type::peak, sampleRate, 3000.0f, 0.8f, wanted[2]);
        eqAir.set (dsp::Biquad::Type::highShelf, sampleRate, 12000.0f, 0.7f, wanted[3]);
    }

    void MasteringChainPlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        // EQ
        if (on (eqOn))
        {
            updateEq();

            for (int c = 0; c < numChannels; ++c)
                for (int i = 0; i < numSamples; ++i)
                    ch[c][i] = eqAir.process (eqPresence.process (eqLowMids.process (eqLow.process (ch[c][i], c), c), c), c);
        }

        // Multiband: split, compress each band (stereo linked), add back up
        const bool glue = on (glueOn);

        if (glue)
        {
            if (! glueRan)
            {
                crossover.reset();

                for (auto& c : bandComp)
                    c.prepare (sampleRate);
            }

            const float amounts[3] = { get (glueLow) * 0.01f, get (glueMid) * 0.01f, get (glueHigh) * 0.01f };
            const float attacks[3] = { 25.0f, 12.0f, 6.0f }, releases[3] = { 220.0f, 160.0f, 110.0f };

            for (int b = 0; b < 3; ++b)
            {
                const auto a = juce::jlimit (0.0f, 1.0f, amounts[b]);
                auto& comp = bandComp[b];
                comp.thresholdDb = -10.0f - 20.0f * a;
                comp.ratio = 1.0f + 3.0f * a;   // 0 % = 1:1, so moving off 0 is gentle, not a step
                comp.kneeDb = 10.0f;
                comp.attackMs = attacks[b];
                comp.releaseMs = releases[b];
                comp.makeupDb = 4.0f * a;
            }

            float most[3] = { 0, 0, 0 };
            const auto chunk = bands[0].getNumSamples();

            for (int start = 0; start < numSamples; start += chunk)
            {
                const auto n = juce::jmin (chunk, numSamples - start);

                for (int c = 0; c < numChannels; ++c)
                {
                    auto* lo = bands[0].getWritePointer (c);
                    auto* mi = bands[1].getWritePointer (c);
                    auto* hi = bands[2].getWritePointer (c);

                    for (int i = 0; i < n; ++i)
                        crossover.split (ch[c][start + i], c, lo[i], mi[i], hi[i]);
                }

                for (int b = 0; b < 3; ++b)
                {
                    if (amounts[b] <= 0.0f)
                        continue;   // exactly 0: the band passes as it is

                    float* pointers[2] = { bands[b].getWritePointer (0), bands[b].getWritePointer (1) };
                    bandComp[b].process (pointers, numChannels, n);
                    most[b] = juce::jmin (most[b], bandComp[b].getReductionDb());
                }

                for (int c = 0; c < numChannels; ++c)
                    for (int i = 0; i < n; ++i)
                        ch[c][start + i] = bands[0].getSample (c, i) + bands[1].getSample (c, i) + bands[2].getSample (c, i);
            }

            for (int b = 0; b < 3; ++b)
                bandReduction[b] = most[b];
        }
        else
        {
            for (auto& r : bandReduction)
                r = 0.0f;
        }

        glueRan = glue;

        // Stereo: width on the side signal; the side's lows taken out (bass in the middle)
        if (on (stereoOn) && numChannels > 1)
        {
            const auto w = juce::jlimit (0.0f, 2.0f, get (width) * 0.01f);
            const bool monoLows = on (monoBass);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto mid = (ch[0][i] + ch[1][i]) * 0.5f;
                auto side = (ch[0][i] - ch[1][i]) * 0.5f * w;

                if (monoLows)
                    side = sideHp2.process (sideHp1.process (side, 0), 0);

                ch[0][i] = mid + side;
                ch[1][i] = mid - side;
            }
        }

        // Limiter (always runs: its look-ahead delay never changes; off = no gain, nothing to catch)
        const bool limit = on (limiterOn);
        limiter.inputDb = limit ? get (gain) : 0.0f;
        limiter.ceilingDb = limit ? get (ceiling) - truePeakMarginDb : 24.0f;
        limiter.process (ch, numChannels, numSamples);
        limiterReduction = limit ? limiter.getReductionDb() : 0.0f;
    }

    //==============================================================================
    const char* ReferencePlugin::xmlTypeName = "ezreference";

    namespace { const juce::Identifier referenceFileProp ("ezRefFile"); }

    ReferencePlugin::ReferencePlugin (te::PluginCreationInfo info) : te::Plugin (info)
    {
        const auto saved = state.getProperty (referenceFileProp).toString();

        if (juce::File::isAbsolutePath (saved))
            setFile (juce::File (saved));   // gone since? It just isn't loaded (the path stays)
    }

    ReferencePlugin::~ReferencePlugin()
    {
        notifyListenersOfDeletion();

        {
            const juce::SpinLock::ScopedLockType lock (swap);
            reader.reset();
        }

        readAhead.stopThread (2000);
    }

    void ReferencePlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        const juce::SpinLock::ScopedLockType lock (swap);
        playbackRate = info.sampleRate;
        const auto ratio = fileRate > 0 ? juce::jmax (1.0, fileRate / playbackRate) : 1.0;
        source.setSize (2, (int) std::ceil (juce::jmax (64, info.blockSizeSamples) * ratio) + 8, false, true, false);
    }

    juce::File ReferencePlugin::getFile() const
    {
        const auto path = state.getProperty (referenceFileProp).toString();
        return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }

    bool ReferencePlugin::setFile (const juce::File& file)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        std::unique_ptr<juce::BufferingAudioReader> newReader;
        double newRate = 0;
        juce::int64 newLength = 0;

        if (file != juce::File())
        {
            auto* raw = engine.getAudioFileFormatManager().readFormatManager.createReaderFor (file);

            if (raw == nullptr || raw->lengthInSamples <= 0 || raw->sampleRate <= 0)
            {
                delete raw;
                return false;
            }

            newRate = raw->sampleRate;
            newLength = raw->lengthInSamples;

            if (! readAhead.isThreadRunning())
                readAhead.startThread();

            newReader = std::make_unique<juce::BufferingAudioReader> (raw, readAhead, (int) (newRate * 4.0));
            newReader->setReadTimeout (0);   // the audio thread never waits: not buffered yet = silence
        }

        {
            const juce::SpinLock::ScopedLockType lock (swap);
            std::swap (reader, newReader);
            fileRate = newRate;
            fileLength = newLength;
            const auto ratio = fileRate > 0 ? juce::jmax (1.0, fileRate / playbackRate) : 1.0;
            const auto needed = (int) std::ceil (juce::jmax (512, source.getNumSamples()) * ratio) + 8;

            if (source.getNumSamples() < needed)
                source.setSize (2, needed, false, true, false);
        }

        newReader.reset();   // the old one, outside the lock
        state.setProperty (referenceFileProp, file == juce::File() ? juce::var() : juce::var (file.getFullPathName()), nullptr);
        return true;
    }

    void ReferencePlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (! listening || fc.isRendering || fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
            return;   // A: the mix passes untouched (and an export never hears the reference)

        auto& buffer = *fc.destBuffer;
        const auto start = fc.bufferStartSample, n = fc.bufferNumSamples;
        const auto channels = juce::jmin (2, buffer.getNumChannels());
        const juce::SpinLock::ScopedTryLockType lock (swap);

        if (! fc.isPlaying || ! lock.isLocked() || reader == nullptr || fileLength <= 0)
        {
            for (int c = 0; c < channels; ++c)
                buffer.clear (c, start, n);   // B with nothing to play: silence, not the mix

            return;
        }

        const auto ratio = fileRate / juce::jmax (1.0, playbackRate);
        const auto position = std::fmod (juce::jmax (0.0, fc.editTime.getStart().inSeconds()) * fileRate, (double) fileLength);
        const auto first = (juce::int64) position;
        const auto frac = position - (double) first;
        const auto needed = juce::jmin (source.getNumSamples(), (int) std::ceil (n * ratio + frac) + 2);

        reader->read (&source, 0, needed, first, true, true);   // past the end reads as silence (the loop restarts next block)

        const auto g = gain.load();
        const auto* srcL = source.getReadPointer (0);
        const auto* srcR = source.getReadPointer (reader->numChannels > 1 ? 1 : 0);

        for (int i = 0; i < n; ++i)
        {
            const auto p = frac + i * ratio;
            const auto i0 = juce::jmin (needed - 2, (int) p);
            const auto t = (float) (p - i0);
            const auto l = srcL[i0] + (srcL[i0 + 1] - srcL[i0]) * t;
            const auto r = srcR[i0] + (srcR[i0 + 1] - srcR[i0]) * t;
            buffer.setSample (0, start + i, l * g);

            if (channels > 1)
                buffer.setSample (1, start + i, r * g);
        }
    }
}
