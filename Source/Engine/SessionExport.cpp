/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Export: the song (or each track, each bus, or the notes) to files, in the background.

    Built on Tracktion's RenderSpecification / RenderQueue: one specification per file, checked
    with validateRenderSpecification, turned into a job with createRenderJob, rendered one after
    another on a background thread. Loudness matching is Tracktion's LUFS normalisation (gain to
    the target, held back so the true peak stays under the ceiling); afterwards the full mix is
    measured again from the file, so the number shown is what was written.
*/

#include "SessionExportJobs.h"
#include "AudioEngine.h"
#include "Mastering.h"
#include "../Core/AppPaths.h"

namespace ez
{
    namespace
    {
        const char* exportFolderKey = "ezExportFolder";

        te::RenderFormat toRenderFormat (Session::ExportFormat f)
        {
            switch (f)
            {
                case Session::ExportFormat::flac:   return te::RenderFormat::flac;
                case Session::ExportFormat::ogg:    return te::RenderFormat::ogg;
                case Session::ExportFormat::mp3:    return te::RenderFormat::mp3;
                case Session::ExportFormat::wav:    break;
            }

            return te::RenderFormat::wav;
        }

        juce::AudioFormat* findFormat (te::Engine& engine, Session::ExportFormat f)
        {
            auto& formats = engine.getAudioFileFormatManager();

            switch (f)
            {
                case Session::ExportFormat::flac:   return formats.getFlacFormat();
                case Session::ExportFormat::ogg:    return formats.getOggFormat();
                case Session::ExportFormat::mp3:
                    te::LAMEManager::registerAudioFormat (formats);   // finds lame.exe (or ffmpeg.exe) next to the app, once
                    return formats.getLameFormat();
                case Session::ExportFormat::wav:    break;
            }

            return formats.getWavFormat();
        }

        /** The quality option for a format: 320 kbps MP3, 256 kbps Ogg. */
        int qualityIndex (juce::AudioFormat* format, Session::ExportFormat f)
        {
            if (format == nullptr)
                return 0;

            const auto options = format->getQualityOptions();
            const auto wanted = f == Session::ExportFormat::mp3 ? "320" : "256";

            for (int i = 0; i < options.size(); ++i)
                if (options[i].startsWith (wanted))
                    return i;

            return juce::jmax (0, options.size() - 1);
        }

        int bitDepthFor (const Session::ExportSettings& s)
        {
            switch (s.format)
            {
                case Session::ExportFormat::flac:   return juce::jmin (24, s.bitDepth);   // FLAC has no float
                case Session::ExportFormat::ogg:
                case Session::ExportFormat::mp3:    return 16;                            // the encoders take 16-bit
                case Session::ExportFormat::wav:    break;
            }

            return s.bitDepth == 16 || s.bitDepth == 24 || s.bitDepth == 32 ? s.bitDepth : 24;
        }

        juce::String formatDuration (double seconds)
        {
            const auto total = juce::roundToInt (seconds);
            return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
        }
    }

    //==============================================================================
    juce::File Session::getExportPresetFolder()
    {
        return AppPaths::getRootFolder().getChildFile ("Presets").getChildFile ("Export");
    }

    juce::String Session::describeExportSettings (const ExportSettings& s)
    {
        if (s.what == ExportWhat::midi)
            return "MIDI file per track";

        const char* formats[] = { "WAV", "FLAC", "Ogg", "MP3" };
        juce::String text = formats[juce::jlimit (0, 3, (int) s.format)];

        if (s.format == ExportFormat::wav || s.format == ExportFormat::flac)
            text << " " << (s.bitDepth == 32 ? juce::String ("32-bit float") : juce::String (s.bitDepth) + "-bit");

        if (s.sampleRate != 48000.0)
            text << " " << juce::String (s.sampleRate / 1000.0, s.sampleRate == 44100.0 ? 1 : 0) << " kHz";

        text << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));

        if (s.what == ExportWhat::stems)          text << "one file per track";
        else if (s.what == ExportWhat::busStems)  text << "one file per bus";
        else if (s.matchLoudness)                 text << juce::String (s.targetLufs, 0) << " LUFS";
        else                                      text << "as you hear it";

        return text;
    }

    juce::Array<Session::ExportPreset> Session::getUserExportPresets()
    {
        juce::Array<ExportPreset> list;
        auto files = getExportPresetFolder().findChildFiles (juce::File::findFiles, false, "*.ezexport");
        files.sort();

        for (const auto& f : files)
        {
            auto xml = juce::parseXML (f);

            if (xml == nullptr || ! xml->hasTagName ("EZEXPORT"))
                continue;

            ExportSettings s;
            s.what = (ExportWhat) juce::jlimit (0, 3, xml->getIntAttribute ("what", 0));
            s.range = (ExportRange) juce::jlimit (0, 2, xml->getIntAttribute ("range", 0));
            s.format = (ExportFormat) juce::jlimit (0, 3, xml->getIntAttribute ("format", 0));
            s.sampleRate = juce::jlimit (8000.0, 192000.0, xml->getDoubleAttribute ("rate", 48000.0));
            s.bitDepth = xml->getIntAttribute ("bits", 24) == 16 ? 16 : xml->getIntAttribute ("bits", 24) == 32 ? 32 : 24;
            s.dither = xml->getBoolAttribute ("dither", true);
            s.useMasterChain = xml->getBoolAttribute ("chain", true);
            s.matchLoudness = xml->getBoolAttribute ("match", true);
            s.targetLufs = (float) juce::jlimit (-30.0, -5.0, xml->getDoubleAttribute ("lufs", -14.0));
            s.ceilingDb = (float) juce::jlimit (-6.0, 0.0, xml->getDoubleAttribute ("ceiling", -1.0));
            list.add ({ f.getFileNameWithoutExtension(), describeExportSettings (s), s });
        }

        return list;
    }

    bool Session::saveExportPreset (const juce::String& name, const ExportSettings& s)
    {
        const auto legal = juce::File::createLegalFileName (name.trim());

        if (legal.isEmpty())
            return fail ("Give the preset a name");

        juce::XmlElement xml ("EZEXPORT");
        xml.setAttribute ("what", (int) s.what);
        xml.setAttribute ("range", (int) s.range);
        xml.setAttribute ("format", (int) s.format);
        xml.setAttribute ("rate", s.sampleRate);
        xml.setAttribute ("bits", s.bitDepth);
        xml.setAttribute ("dither", s.dither);
        xml.setAttribute ("chain", s.useMasterChain);
        xml.setAttribute ("match", s.matchLoudness);
        xml.setAttribute ("lufs", (double) s.targetLufs);
        xml.setAttribute ("ceiling", (double) s.ceilingDb);

        const auto folder = getExportPresetFolder();

        if (! folder.createDirectory().wasOk() || ! xml.writeTo (folder.getChildFile (legal + ".ezexport")))
            return fail ("Could not save the preset in " + folder.getFullPathName());

        return true;
    }

    bool Session::deleteExportPreset (const juce::String& name)
    {
        const auto file = getExportPresetFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".ezexport");

        if (! file.existsAsFile() || ! (file.moveToTrash() || file.deleteFile()))
            return fail ("Could not delete the preset \"" + name + "\"");

        return true;
    }

    const juce::Array<Session::ExportPreset>& Session::getExportPresets()
    {
        static const juce::Array<ExportPreset> presets = []
        {
            juce::Array<ExportPreset> list;

            ExportSettings streaming;   // the defaults: WAV 24-bit, 48 kHz, -14 LUFS
            list.add ({ "Streaming release", juce::String (juce::CharPointer_UTF8 ("WAV 24-bit \xc2\xb7 -14 LUFS")), streaming });

            auto friendCopy = streaming;
            friendCopy.format = ExportFormat::mp3;
            friendCopy.sampleRate = 44100.0;
            list.add ({ "Send to a friend", juce::String (juce::CharPointer_UTF8 ("MP3 320 kbps \xc2\xb7 -14 LUFS")), friendCopy });

            auto stems = streaming;
            stems.what = ExportWhat::stems;
            stems.matchLoudness = false;
            list.add ({ "Stems for a collab", juce::String (juce::CharPointer_UTF8 ("WAV 24-bit \xc2\xb7 one file per track")), stems });

            auto club = streaming;
            club.targetLufs = -8.0f;
            club.ceilingDb = -0.3f;
            list.add ({ "Club / DJ set", juce::String (juce::CharPointer_UTF8 ("WAV 24-bit \xc2\xb7 -8 LUFS")), club });

            auto cd = streaming;
            cd.sampleRate = 44100.0;
            cd.bitDepth = 16;
            cd.dither = true;
            cd.matchLoudness = false;
            list.add ({ "CD master", juce::String (juce::CharPointer_UTF8 ("WAV 16-bit 44.1 kHz \xc2\xb7 dither")), cd });

            auto midi = streaming;
            midi.what = ExportWhat::midi;
            list.add ({ "MIDI for hardware", "MIDI file per track (MC-707 and friends)", midi });

            return list;
        }();

        return presets;
    }

    const juce::Array<Session::LoudnessTarget>& Session::getLoudnessTargets()
    {
        static const juce::Array<LoudnessTarget> targets = []
        {
            juce::Array<LoudnessTarget> list;
            list.add ({ "Spotify", -14.0f, -1.0f });
            list.add ({ "Apple Music", -16.0f, -1.0f });
            list.add ({ "YouTube", -14.0f, -1.0f });
            list.add ({ "Club", -8.0f, -0.3f });
            list.add ({ "CD", -9.0f, -0.1f });
            return list;
        }();
        return targets;
    }

    Session::ExportSettings Session::getDefaultExportSettings() const
    {
        ExportSettings s;
        const auto saved = audioEngine.getEngine().getPropertyStorage().getPropertiesFile().getValue (exportFolderKey);
        s.folder = juce::File::isAbsolutePath (saved) ? juce::File (saved) : AppPaths::getRootFolder().getChildFile ("Exports");
        s.fileName = juce::File::createLegalFileName ((edit != nullptr ? getSongName() : juce::String ("Song")) + " - Master");
        const auto& target = getLoudnessTargets().getReference (getMasterTarget());
        s.targetLufs = target.lufs;
        s.ceilingDb = target.ceilingDb;
        s.dither = getMasterDither();
        return s;
    }

    bool Session::isExportFormatAvailable (ExportFormat f) const
    {
        return findFormat (audioEngine.getEngine(), f) != nullptr;
    }

    juce::String Session::getExportExtension (const ExportSettings& s)
    {
        if (s.what == ExportWhat::midi)
            return ".mid";

        switch (s.format)
        {
            case ExportFormat::flac:    return ".flac";
            case ExportFormat::ogg:     return ".ogg";
            case ExportFormat::mp3:     return ".mp3";
            case ExportFormat::wav:     break;
        }

        return ".wav";
    }

    //==============================================================================
    te::TimeRange Session::getExportTimeRange (ExportRange range) const
    {
        if (edit == nullptr)
            return {};

        switch (range)
        {
            case ExportRange::loop:
                return getLoopRange();

            case ExportRange::selection:
            {
                const auto clips = getSelectedClips();

                if (clips.isEmpty())
                    return {};

                auto span = clips.getFirst()->getPosition().time;

                for (auto* c : clips)
                    span = span.getUnionWith (c->getPosition().time);

                return span;
            }

            case ExportRange::wholeSong:
                break;
        }

        return { te::TimePosition(), te::TimePosition() + getSongLength() };
    }

    juce::String Session::describeExportRange (ExportRange range) const
    {
        const auto time = getExportTimeRange (range);

        if (edit == nullptr || time.getLength() <= te::TimeDuration::fromSeconds (0.01))
            return {};

        const auto barBeat = [this] (te::TimePosition t)
        {
            const auto bb = edit->tempoSequence.toBarsAndBeats (t);
            return juce::String (bb.bars + 1) + "." + juce::String (bb.getWholeBeats() + 1);
        };

        return barBeat (time.getStart()) + " to " + barBeat (time.getEnd()) + " (" + formatDuration (time.getLength().inSeconds()) + ")";
    }

    juce::Array<te::AudioTrack*> Session::getExportTracks (ExportWhat what) const
    {
        juce::Array<te::AudioTrack*> tracks;

        if (edit == nullptr || what == ExportWhat::fullMix)
            return tracks;

        for (auto* t : te::getAudioTracks (*edit))
        {
            const auto kind = kindOf (*t);

            if (what == ExportWhat::busStems)
            {
                if (kind != TrackKind::normal)
                    tracks.add (t);

                continue;
            }

            if (kind != TrackKind::normal)
                continue;

            bool wanted = false;

            for (auto* c : t->getClips())
                wanted = wanted || (what == ExportWhat::stems || dynamic_cast<te::MidiClip*> (c) != nullptr);

            if (wanted)
                tracks.add (t);
        }

        return tracks;
    }

    juce::Array<juce::File> Session::getExportFiles (const ExportSettings& s) const
    {
        juce::Array<juce::File> files;
        const auto extension = getExportExtension (s);
        const auto base = juce::File::createLegalFileName (s.fileName.trim().isNotEmpty() ? s.fileName.trim() : juce::String ("Export"));

        if (s.what == ExportWhat::fullMix)
        {
            files.add (s.folder.getChildFile (base + extension));
            return files;
        }

        juce::StringArray used;

        for (auto* t : getExportTracks (s.what))
        {
            auto name = base + " - " + juce::File::createLegalFileName (t->getName());

            for (int n = 2; used.contains (name, true); ++n)
                name = base + " - " + juce::File::createLegalFileName (t->getName()) + " " + juce::String (n);

            used.add (name);
            files.add (s.folder.getChildFile (name + extension));
        }

        return files;
    }

    juce::String Session::estimateExport (const ExportSettings& s) const
    {
        const auto seconds = getExportTimeRange (s.range).getLength().inSeconds();
        const auto count = s.what == ExportWhat::fullMix ? 1 : getExportTracks (s.what).size();

        if (seconds <= 0.01 || count == 0)
            return {};

        const auto length = formatDuration (seconds);
        const auto files = count == 1 ? juce::String() : juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (count) + " files";

        if (s.what == ExportWhat::midi)
            return juce::String (juce::CharPointer_UTF8 ("MIDI \xc2\xb7 ")) + length + files;

        double bytesPerSecond = 0.0;

        switch (s.format)
        {
            case ExportFormat::wav:     bytesPerSecond = s.sampleRate * 2.0 * bitDepthFor (s) / 8.0; break;
            case ExportFormat::flac:    bytesPerSecond = s.sampleRate * 2.0 * bitDepthFor (s) / 8.0 * 0.6; break;   // typical
            case ExportFormat::ogg:     bytesPerSecond = 256000.0 / 8.0; break;
            case ExportFormat::mp3:     bytesPerSecond = 320000.0 / 8.0; break;
        }

        const auto megabytes = bytesPerSecond * seconds * count / (1024.0 * 1024.0);
        const auto size = megabytes < 1.0 ? juce::String ("under 1 MB")
                                          : juce::String (megabytes < 10.0 ? juce::String (megabytes, 1) : juce::String (juce::roundToInt (megabytes))) + " MB";
        return "About " + size + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + length + files;
    }

    //==============================================================================
    void Session::switchMasterChainBackOn (ExportJobs& jobs)
    {
        if (jobs.chainToSwitchBackOn != nullptr)
        {
            jobs.chainToSwitchBackOn->state.setProperty (te::IDs::enabled, true, nullptr);
            jobs.chainToSwitchBackOn = nullptr;
        }
    }

    bool Session::isExportingNow() const noexcept
    {
        return exportStatus.state == ExportStatus::State::running || exportStatus.state == ExportStatus::State::measuring;
    }

    bool Session::startExport (const ExportSettings& s)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("Open a song first");

        if (isExportingNow())
            return fail ("An export is already running");

        if (isRecording())
            return fail ("Stop recording before exporting");

        const auto time = getExportTimeRange (s.range);

        if (time.getLength() <= te::TimeDuration::fromSeconds (0.01))
            return fail (s.range == ExportRange::loop      ? "Set a loop first (drag in the ruler), or export the whole song"
                       : s.range == ExportRange::selection ? "Select some clips first, or export the whole song"
                                                           : "Nothing to export: the song has no clips yet");

        auto& engine = audioEngine.getEngine();
        auto* format = s.what == ExportWhat::midi ? nullptr : findFormat (engine, s.format);

        if (s.what != ExportWhat::midi && format == nullptr)
            return fail (s.format == ExportFormat::mp3 ? "MP3 needs lame.exe (or ffmpeg.exe) in the same folder as EzDAWsIt.exe"
                                                       : "That format isn't available");

        const auto tracks = getExportTracks (s.what);

        if (s.what != ExportWhat::fullMix && tracks.isEmpty())
            return fail (s.what == ExportWhat::busStems ? "There are no buses or FX returns to export"
                       : s.what == ExportWhat::midi     ? "No track has a MIDI clip to export"
                                                        : "No track has clips to export");

        if (! s.folder.isDirectory() && ! s.folder.createDirectory())
            return fail ("Could not make the folder " + s.folder.getFullPathName());

        if (! s.folder.hasWriteAccess())
            return fail ("Can't write to " + s.folder.getFullPathName() + ": pick another folder");

        // What's common to every file
        te::RenderSpecification base;
        base.time = time;
        base.includeTails = s.range == ExportRange::wholeSong;   // reverb and delay ring out; a loop or a selection ends where it ends
        base.sampleRate = s.sampleRate;
        base.bitDepth = bitDepthFor (s);
        base.quality = qualityIndex (format, s.format);
        base.format = s.what == ExportWhat::midi ? te::RenderFormat::midi : toRenderFormat (s.format);
        base.dither = s.dither && base.bitDepth == 16 && (s.format == ExportFormat::wav || s.format == ExportFormat::flac);
        base.channelLayout = "stereo";
        base.useMasterPlugins = s.what == ExportWhat::fullMix;   // the master chain (if on) and the master fader
        base.normaliseByLUFS = s.what == ExportWhat::fullMix && s.matchLoudness;
        base.normaliseToLevelDb = s.targetLufs;
        base.limitTruePeak = true;
        base.truePeakCeilingDb = s.ceilingDb;
        base.usePlugins = s.what != ExportWhat::midi;   // MIDI: the clips' own notes
        base.metadata = te::createTagMetadata (*edit);

        // One specification per file
        std::vector<std::pair<juce::String, te::RenderSpecification>> specs;
        const auto files = getExportFiles (s);

        if (s.what == ExportWhat::fullMix)
        {
            auto spec = base;
            spec.destination = files.getFirst();
            specs.emplace_back ("Full mix", spec);
        }
        else
        {
            for (int i = 0; i < tracks.size(); ++i)
            {
                auto spec = base;
                spec.tracks = { tracks[i]->itemID };
                spec.includeSourceTracks = s.what != ExportWhat::midi;   // a reverb return's sends still feed it, silently
                spec.destination = files[i];
                specs.emplace_back (tracks[i]->getName(), spec);
            }
        }

        // Check them all before starting any
        auto jobs = std::make_unique<ExportJobs>();

        for (auto& [name, spec] : specs)
        {
            const auto valid = te::validateRenderSpecification (*edit, spec);

            if (valid.failed())
            {
                if (s.what == ExportWhat::fullMix)
                    return fail ("Can't export: " + valid.getErrorMessage());

                jobs->problems.add (name + ": " + valid.getErrorMessage());
                continue;
            }

            if (auto planned = te::createRenderJob (*edit, spec))
            {
                planned->name = name;
                jobs->names.add (name);
                jobs->jobs.push_back (jobs->queue.addJob (std::move (*planned)));
            }
            else
            {
                jobs->problems.add (name + ": could not be set up");
            }
        }

        if (jobs->jobs.empty())
            return fail ("Nothing could be exported: " + jobs->problems.joinIntoString ("; "));

        jobs->measureLoudness = s.what == ExportWhat::fullMix;

        // Playback stops (the render takes the plugins); the song comes back when it's done
        if (edit->getTransport().isPlaying())
            edit->getTransport().stop (false, true);

        setReferenceListening (false);   // B never goes into a file (the plugin passes renders anyway)

        // "Use the mastering chain" off: switched off for the render (not an undo step), back on when it's done
        if (s.what == ExportWhat::fullMix && ! s.useMasterChain)
            if (auto* chain = getMasteringChain(); chain != nullptr && chain->isEnabled())
            {
                chain->state.setProperty (te::IDs::enabled, false, nullptr);
                jobs->chainToSwitchBackOn = chain;
            }

        audioEngine.getEngine().getPropertyStorage().getPropertiesFile().setValue (exportFolderKey, s.folder.getFullPathName());

        exportJobs.reset();   // the last export's finished jobs
        exportStatus = {};
        exportStatus.state = ExportStatus::State::running;
        exportStatus.filesTotal = (int) jobs->jobs.size();
        exportStatus.current = jobs->names[0];

        exportJobs = std::move (jobs);
        playbackNeedsRestoring = true;
        auto* running = exportJobs.get();

        running->queue.onJobStarted = [this, running] (te::RenderQueue::Job& job)
        {
            if (exportJobs.get() == running)
                exportStatus.current = job.getName();
        };

        running->queue.onJobFinished = [this, running] (te::RenderQueue::Job& job)
        {
            if (exportJobs.get() != running)
                return;

            if (job.getState() == te::RenderQueue::Job::State::completed)
                exportStatus.files.add (job.getFile());
            else if (job.getState() == te::RenderQueue::Job::State::failed)
                running->problems.add (job.getName() + ": " + job.getError());

            ++exportStatus.filesDone;
        };

        running->queue.onFinished = [this, running]
        {
            if (exportJobs.get() == running)
                finishExport();
        };

        running->queue.start();
        return true;
    }

    void Session::finishExport()
    {
        auto& jobs = *exportJobs;

        if (exportStatus.state != ExportStatus::State::running)
            return;

        switchMasterChainBackOn (jobs);

        exportStatus.progress = 1.0f;
        exportStatus.current = {};

        if (jobs.cancelRequested)
        {
            exportStatus.state = ExportStatus::State::cancelled;
            exportStatus.message = exportStatus.files.isEmpty() ? juce::String ("Export cancelled: nothing was written")
                                                                : "Export cancelled after " + juce::String (exportStatus.files.size()) + " of "
                                                                  + juce::String (exportStatus.filesTotal) + " files";
            return;
        }

        if (exportStatus.files.isEmpty())
        {
            exportStatus.state = ExportStatus::State::failed;
            exportStatus.message = "Export failed: " + (jobs.problems.isEmpty() ? juce::String ("no file was written") : jobs.problems.joinIntoString ("; "));
            lastError = exportStatus.message;   // the export window shows it (no second alert on top)
            return;
        }

        exportStatus.message = jobs.problems.isEmpty() ? juce::String()
                                                       : "Some files were not written: " + jobs.problems.joinIntoString ("; ");

        if (jobs.measureLoudness)
        {
            exportStatus.state = ExportStatus::State::measuring;
            jobs.scan = std::make_unique<LoudnessScan> (audioEngine.getEngine().getAudioFileFormatManager().readFormatManager,
                                                              exportStatus.files.getFirst());
            jobs.scan->startThread();
            return;
        }

        exportStatus.state = ExportStatus::State::done;
    }

    void Session::cancelExport()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (exportJobs == nullptr)
            return;

        if (exportStatus.state == ExportStatus::State::running)
        {
            exportJobs->cancelRequested = true;
            exportJobs->queue.cancelAll();   // finishExport follows when the active job has stopped
        }
        else if (exportStatus.state == ExportStatus::State::measuring)
        {
            exportJobs->scan.reset();        // the files are written: only the number is skipped
            exportStatus.state = ExportStatus::State::done;
        }
    }

    bool Session::isExporting() const noexcept
    {
        return isExportingNow();
    }

    juce::Array<int> Session::getExportSampleRates (ExportFormat f) const
    {
        juce::Array<int> rates;

        if (auto* format = findFormat (audioEngine.getEngine(), f))
            for (auto r : format->getPossibleSampleRates())
                if (r == 44100 || r == 48000 || r == 88200 || r == 96000)
                    rates.add (r);

        return rates;
    }

    void Session::updateExport()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (exportJobs != nullptr)
        {
            if (exportStatus.state == ExportStatus::State::running)
            {
                exportStatus.progress = exportJobs->queue.getTotalProgress();
            }
            else if (exportStatus.state == ExportStatus::State::measuring && exportJobs->scan != nullptr && exportJobs->scan->finished)
            {
                exportStatus.loudnessValid = exportJobs->scan->valid;
                exportStatus.integratedLufs = exportJobs->scan->readings.integratedLufs;
                exportStatus.truePeakDb = exportJobs->scan->readings.truePeakDb;
                exportJobs->scan.reset();
                exportStatus.state = ExportStatus::State::done;
            }
        }

        restorePlaybackAfterExport();
    }

    void Session::restorePlaybackAfterExport()
    {
        // The render freed the playback graph (and doesn't build it again): once nothing is rendering, put it back, once
        if (! playbackNeedsRestoring || exportStatus.state == ExportStatus::State::running)
            return;

        if (edit == nullptr)
        {
            playbackNeedsRestoring = false;
            return;
        }

        if (edit->isRendering())
            return;

        playbackNeedsRestoring = false;

        if (! edit->getTransport().isPlayContextActive())
            edit->getTransport().ensureContextAllocated();
    }

    void Session::clearExportStatus()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (isExportingNow())
            return;

        exportJobs.reset();
        exportStatus = {};
        restorePlaybackAfterExport();
    }

    void Session::abandonExport()
    {
        // Closing the song: stop everything (the jobs hold the song's tracks) before it goes
        if (exportJobs != nullptr)
        {
            exportJobs->cancelRequested = true;
            exportJobs->queue.cancelAll();
            switchMasterChainBackOn (*exportJobs);   // before the song is saved and closed
            exportJobs->queue.onJobStarted = nullptr;
            exportJobs->queue.onJobFinished = nullptr;
            exportJobs->queue.onFinished = nullptr;
            exportJobs.reset();   // cancels, waits for the render thread and restores muted tracks
        }

        exportStatus = {};
        playbackNeedsRestoring = false;   // the song is closing
    }
}
