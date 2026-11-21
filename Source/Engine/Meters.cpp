/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "Meters.h"
#include "Session.h"

namespace ez
{
    namespace
    {
        constexpr float fallDbPerSecond = 24.0f;
        constexpr float clipDb = -0.01f;   // a sample at full scale counts as a clip

        juce::uint32 holdMs (MeterBank::Hold h)
        {
            switch (h)
            {
                case MeterBank::Hold::off:          return 0;
                case MeterBank::Hold::short_:       return 1500;
                case MeterBank::Hold::long_:        return 3000;
                case MeterBank::Hold::untilReset:   return std::numeric_limits<juce::uint32>::max();
            }

            return 1500;
        }
    }

    //==============================================================================
    const char* MasterTapPlugin::xmlTypeName = "ezmastertap";

    MasterTapPlugin::MasterTapPlugin (te::PluginCreationInfo info) : te::Plugin (info) {}

    MasterTapPlugin::~MasterTapPlugin()
    {
        notifyListenersOfDeletion();
    }

    void MasterTapPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        // Message thread, while the audio graph isn't running this plugin: safe to allocate
        prepared = false;
        loudness.prepare (info.sampleRate, 2, juce::jmax (64, info.blockSizeSamples));
        sampleRate = info.sampleRate;
        prepared = true;
    }

    float MasterTapPlugin::takePeak (int channel) noexcept
    {
        return juce::isPositiveAndBelow (channel, 2) ? peaks[channel].exchange (0.0f) : 0.0f;
    }

    void MasterTapPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
            return;

        SCOPED_REALTIME_CHECK

        auto& buffer = *fc.destBuffer;
        const auto channels = juce::jmin (2, buffer.getNumChannels());
        const float* pointers[2] = { nullptr, nullptr };

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto peak = buffer.getMagnitude (ch, fc.bufferStartSample, fc.bufferNumSamples);
            auto current = peaks[ch].load();

            while (peak > current && ! peaks[ch].compare_exchange_weak (current, peak)) {}

            pointers[ch] = buffer.getReadPointer (ch, fc.bufferStartSample);
        }

        if (channels == 1)
        {
            pointers[1] = pointers[0];   // mono counts as both sides
            auto current = peaks[1].load();
            const auto peak = peaks[0].load();

            while (peak > current && ! peaks[1].compare_exchange_weak (current, peak)) {}
        }

        if (prepared && channels > 0)
        {
            loudness.process (pointers, 2, fc.bufferNumSamples);

            // For the spectrum: left and right summed. Whatever doesn't fit is dropped (nobody is reading)
            int start1, size1, start2, size2;
            fifo.prepareToWrite (fc.bufferNumSamples, start1, size1, start2, size2);

            for (int i = 0; i < size1 + size2; ++i)
                fifoData[(size_t) (i < size1 ? start1 + i : start2 + i - size1)] = 0.5f * (pointers[0][i] + pointers[1][i]);

            fifo.finishedWrite (size1 + size2);
        }
    }

    int MasterTapPlugin::readSamples (float* dest, int maxSamples) noexcept
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (maxSamples, start1, size1, start2, size2);

        if (size1 > 0) std::copy_n (fifoData.data() + start1, size1, dest);
        if (size2 > 0) std::copy_n (fifoData.data() + start2, size2, dest + size1);

        fifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

    //==============================================================================
    MeterBank::MeterBank (Session& s) : session (s)
    {
        startTimerHz (pollHz);
    }

    MeterBank::~MeterBank()
    {
        releaseSong();
    }

    void MeterBank::unbind (TrackMeter& m)
    {
        if (m.client != nullptr)
            if (auto* meter = dynamic_cast<te::LevelMeterPlugin*> (m.plugin.get()))
                meter->measurer.removeClient (*m.client);

        m.client.reset();
        m.plugin = nullptr;
    }

    void MeterBank::releaseSong()
    {
        for (auto& [id, m] : tracks)
            unbind (*m);

        tracks.clear();
        order.clear();
        main = {};
    }

    void MeterBank::setMode (te::LevelMeasurer::Mode newMode)
    {
        mode = newMode;

        for (auto& [id, m] : tracks)
            if (auto* meter = dynamic_cast<te::LevelMeterPlugin*> (m->plugin.get()))
                meter->measurer.setMode (mode);
    }

    void MeterBank::setHold (Hold h)
    {
        hold = h;

        // Held peaks waiting "until reset" fall again under a shorter hold
        const auto limit = juce::Time::getMillisecondCounter() + holdMs (h);

        for (auto& [id, m] : tracks)
            for (auto& until : m->holdUntil)
                until = h == Hold::untilReset ? until : juce::jmin (until, limit);

        for (auto& until : mainHoldUntil)
            until = h == Hold::untilReset ? until : juce::jmin (until, limit);
    }

    void MeterBank::update (Reading& r, juce::uint32* holdUntil, int channel, float peakDb, float elapsedSeconds, juce::uint32 now)
    {
        peakDb = juce::jmax (floorDb, peakDb);
        auto& level = r.levelDb[channel];
        level = juce::jmax (peakDb, level - fallDbPerSecond * elapsedSeconds);

        // Held peak: stays for the hold time, then falls like the level
        auto& held = r.holdDb[channel];

        if (peakDb >= held)
        {
            held = peakDb;
            const auto ms = holdMs (hold);
            holdUntil[channel] = ms == std::numeric_limits<juce::uint32>::max() ? ms : now + ms;
        }
        else if (now >= holdUntil[channel])
        {
            held = juce::jmax (level, held - fallDbPerSecond * elapsedSeconds);
        }

        r.maxDb = juce::jmax (r.maxDb, peakDb);
        r.clipped = r.clipped || peakDb >= clipDb;
    }

    void MeterBank::poll()
    {
        const auto now = juce::Time::getMillisecondCounter();
        const auto elapsed = lastPollMs == 0 ? 0.0f : juce::jlimit (0.0f, 0.5f, (float) (now - lastPollMs) / 1000.0f);
        lastPollMs = now;

        auto* edit = session.getEdit();

        if (edit == nullptr)
        {
            releaseSong();
            return;
        }

        // Tracks, by ID: a track keeps its meter (and its clip light) when others move or go
        const auto audioTracks = te::getAudioTracks (*edit);
        order.clear();

        for (auto* track : audioTracks)
            order.push_back (track->itemID);

        for (auto it = tracks.begin(); it != tracks.end();)
        {
            if (std::find (order.begin(), order.end(), it->first) == order.end())
            {
                unbind (*it->second);
                it = tracks.erase (it);
            }
            else
            {
                ++it;
            }
        }

        for (auto* track : audioTracks)
        {
            auto& slot = tracks[track->itemID];

            if (slot == nullptr)
                slot = std::make_unique<TrackMeter>();

            auto& m = *slot;
            auto* meterPlugin = track->getLevelMeterPlugin();

            if (m.plugin.get() != meterPlugin)   // first time, or undo recreated the track's plugins
            {
                unbind (m);

                if (meterPlugin != nullptr)
                {
                    m.trackID = track->itemID;
                    m.plugin = meterPlugin;
                    m.client = std::make_unique<te::LevelMeasurer::Client>();
                    m.client->setOverload (7, false);   // sizes its lists here, so the audio thread never has to
                    meterPlugin->measurer.setMode (mode);
                    meterPlugin->measurer.addClient (*m.client);
                }
            }

            if (m.client == nullptr)
                continue;

            // Each channel is read once (reading clears it); a mono track shows the same on both sides
            const auto channels = juce::jlimit (0, 2, m.client->getNumChannelsUsed());
            m.reading.channels = channels;
            const auto left = channels > 0 ? m.client->getAndClearAudioLevel (0).dB : floorDb;
            const auto right = channels > 1 ? m.client->getAndClearAudioLevel (1).dB : left;
            update (m.reading, m.holdUntil, 0, left, elapsed, now);
            update (m.reading, m.holdUntil, 1, right, elapsed, now);
            // (the clip light comes from the level: the client's "overload" flag is a clear request, not an overload)
        }

        // Main output
        MasterTapPlugin* tap = nullptr;

        for (auto* plugin : edit->getMasterPluginList())
            if (auto* t = dynamic_cast<MasterTapPlugin*> (plugin))
                tap = t;

        if (tap != nullptr)
        {
            // The master volume comes after the tap: add it, so the meter shows what leaves the app
            const auto masterDb = edit->getMasterVolumePlugin() != nullptr ? edit->getMasterVolumePlugin()->getVolumeDb() : 0.0f;
            main.channels = 2;

            for (int ch = 0; ch < 2; ++ch)
                update (main, mainHoldUntil, ch, juce::Decibels::gainToDecibels (tap->takePeak (ch), -100.0f) + masterDb, elapsed, now);

            main.loudness = tap->getLoudness();

            const auto read = tap->readSamples (readBuffer.data(), (int) readBuffer.size());
            analyseSamples (readBuffer.data(), read, tap->getSampleRate(), elapsed);

            for (auto* value : { &main.loudness.momentaryLufs, &main.loudness.shortTermLufs, &main.loudness.integratedLufs,
                                 &main.loudness.maxMomentaryLufs, &main.loudness.maxShortTermLufs, &main.loudness.truePeakDb, &main.loudness.samplePeakDb })
                if (*value > te::LoudnessMeter::silenceFloorDb)
                    *value += masterDb;
        }
    }

    MeterBank::Reading MeterBank::getTrack (int trackIndex) const
    {
        if (! juce::isPositiveAndBelow (trackIndex, (int) order.size()))
            return {};

        const auto found = tracks.find (order[(size_t) trackIndex]);
        return found != tracks.end() ? found->second->reading : Reading();
    }

    MeterBank::MainReading MeterBank::getMain() const
    {
        return main;
    }

    void MeterBank::analyseSamples (const float* samples, int numSamples, double sampleRate, float elapsedSeconds)
    {
        if (numSamples <= 0)
        {
            // nothing new (stopped): the bars fall away instead of freezing on the last sound
            for (auto& shown : spectrum)
                shown = juce::jmax (-100.0f, shown - 20.0f * juce::jmax (0.01f, elapsedSeconds));

            return;
        }

        // keep the latest fftSize samples
        if (numSamples >= fftSize)
        {
            std::copy_n (samples + numSamples - fftSize, fftSize, history.begin());
        }
        else if (numSamples > 0)
        {
            std::move (history.begin() + numSamples, history.end(), history.begin());
            std::copy_n (samples, numSamples, history.end() - numSamples);
        }

        std::copy (history.begin(), history.end(), fftData.begin());
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        if (spectrum.empty())
            spectrum.assign ((size_t) spectrumBands, -100.0f);

        // bands evenly spaced in log frequency; each takes the loudest bin it covers
        const auto binHz = sampleRate / fftSize;
        const auto fall = 20.0f * juce::jmax (0.01f, elapsedSeconds);
        const auto scale = 2.0f / (float) fftSize;   // a full-scale sine reads about 0 dB (JUCE normalises the window's gain to 1)

        for (int b = 0; b < spectrumBands; ++b)
        {
            const auto lo = 20.0 * std::pow (1000.0, (double) b / spectrumBands);
            const auto hi = 20.0 * std::pow (1000.0, (double) (b + 1) / spectrumBands);
            const auto first = juce::jlimit (1, fftSize / 2 - 1, (int) std::floor (lo / binHz));
            const auto last = juce::jlimit (first, fftSize / 2 - 1, (int) std::ceil (hi / binHz));
            float magnitude = 0.0f;

            for (int bin = first; bin <= last; ++bin)
                magnitude = juce::jmax (magnitude, fftData[(size_t) bin]);

            const auto db = juce::Decibels::gainToDecibels (magnitude * scale, -100.0f);
            auto& shown = spectrum[(size_t) b];
            shown = db > shown ? db : juce::jmax (db, shown - fall);
        }
    }

    void MeterBank::resetTrack (int trackIndex)
    {
        if (! juce::isPositiveAndBelow (trackIndex, (int) order.size()))
            return;

        if (const auto found = tracks.find (order[(size_t) trackIndex]); found != tracks.end())
        {
            auto& m = *found->second;
            const auto channels = m.reading.channels;
            m.reading = {};
            m.reading.channels = channels;
            m.holdUntil[0] = m.holdUntil[1] = 0;
        }
    }

    void MeterBank::resetMain()
    {
        const auto loud = main.loudness;
        const auto channels = main.channels;
        main = {};
        main.channels = channels;
        main.loudness = loud;
        mainHoldUntil[0] = mainHoldUntil[1] = 0;

        if (auto* edit = session.getEdit())
            for (auto* plugin : edit->getMasterPluginList())
                if (auto* t = dynamic_cast<MasterTapPlugin*> (plugin))
                    t->resetLoudness();   // integrated loudness and the held maxima start again
    }

    void MeterBank::resetAll()
    {
        for (int i = 0; i < (int) order.size(); ++i)
            resetTrack (i);

        resetMain();
    }
}
