/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "SessionExportJobs.h"
#include "Effects.h"
#include "MidiLearn.h"
#include "VocalChain.h"
#include "AudioEngine.h"
#include "../Core/AppPaths.h"

namespace ez
{
    namespace
    {
        juce::String timestamp()
        {
            return juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
        }

        /** e.g. "My Song (autosave 2026-11-08 11-30-00).bak" next to the song. Never overwrites. */
        juce::File makeBackupFile (const juce::File& song, const juce::String& label)
        {
            return song.getParentDirectory().getNonexistentChildFile (song.getFileNameWithoutExtension()
                                                                        + " (" + label + " " + timestamp() + ")",
                                                                      ".bak", false);
        }

        /** A real song: not empty (Tracktion treats an empty file as a new, blank song, which would
            "restore" nothing over real work) and parses as an Edit. */
        bool isReadableSong (te::Engine& engine, const juce::File& file)
        {
            return file.getSize() > 0
                && te::loadEditFromFile (engine, file, te::ProjectItemID {}).hasType (te::IDs::EDIT);
        }

        juce::String formatTime (double seconds)
        {
            const auto totalMs = juce::roundToInt (juce::jmax (0.0, seconds) * 1000.0);

            return juce::String (totalMs / 60000) + ":" + juce::String ((totalMs / 1000) % 60).paddedLeft ('0', 2)
                 + "." + juce::String (totalMs % 1000).paddedLeft ('0', 3);
        }
    }

    //==============================================================================
    int Session::readSongFormatVersion (te::Engine& engine, const juce::File& file)
    {
        if (file.getSize() <= 0)
            return 0;

        return (int) te::loadEditFromFile (engine, file, te::ProjectItemID {})[formatVersionProp];
    }

    //==============================================================================
    Session::Session (AudioEngine& engineToUse)
        : audioEngine (engineToUse),
          selectionManager (engineToUse.getEngine())
    {
        meters = std::make_unique<MeterBank> (*this);
        midiLearn = std::make_unique<MidiLearn> (audioEngine, *this);
    }

    Session::~Session()
    {
        closeSong();
        midiLearn.reset();
        meters.reset();
    }

    //==============================================================================
    bool Session::createSong (const juce::String& name)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto file = AppPaths::createSongFile (name);

        if (file == juce::File())
            return fail ("Could not create a folder for the song in " + AppPaths::getProjectsFolder().getFullPathName());

        return adoptEdit (te::createEmptyEdit (audioEngine.getEngine(), file), file, true);
    }

    bool Session::openSong (const juce::File& file, bool useAutosave)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! file.existsAsFile())
            return fail ("Song not found: " + file.getFullPathName());

        // Re-opening the open song: save it first (keeping it open if that fails), then close it
        // so its save can't overwrite what we load
        if (edit != nullptr && file == songFile)
        {
            if (isPlaying() || isRecording())
                edit->getTransport().stop (false, false);

            if (! writeSongNow (true))
                return fail ("Could not save the open song before re-opening it, so it was left open");

            closeSong();
        }

        auto& engine = audioEngine.getEngine();
        const auto autosave = getAutosaveFile (file);
        const bool hasAutosave = autosave.existsAsFile();

        if (useAutosave)
        {
            if (! hasAutosave)
                return fail ("There is no autosave to restore for " + file.getFileName());

            if (! isReadableSong (engine, autosave))
                return fail ("The autosave for " + file.getFileNameWithoutExtension()
                             + " is damaged, so it can't be restored. Your saved song was not changed.");

            if (readSongFormatVersion (engine, autosave) > songFormatVersion)
                return fail ("The autosave for " + file.getFileNameWithoutExtension()
                             + " was made by a newer EzDAWsIt. Update EzDAWsIt to restore it. Nothing was changed.");

            const auto backup = makeBackupFile (file, "before restore");

            if (! file.copyFileTo (backup) || ! autosave.copyFileTo (file))
                return fail ("Could not restore the autosave for " + file.getFileName());

            log ("Restored autosave for " + file.getFullPathName() + " (previous version: " + backup.getFileName() + ")");
        }

        // Check the file before handing it to the engine; nothing on disk has been touched if this fails
        if (! isReadableSong (engine, file))
            return fail ("This song file is damaged and could not be opened:\n" + file.getFullPathName()
                         + (hasAutosave && ! useAutosave ? "\n\nAn autosave exists: open it again and choose Restore." : ""));

        if (readSongFormatVersion (engine, file) > songFormatVersion)
        {
            const auto savedWith = te::loadEditFromFile (engine, file, te::ProjectItemID {})[savedWithProp].toString();
            return fail ("\"" + file.getFileNameWithoutExtension() + "\" was saved by a newer EzDAWsIt"
                         + (savedWith.isNotEmpty() ? " (" + savedWith + ")" : juce::String())
                         + ".\n\nUpdate EzDAWsIt (Help > Check for updates) to open it. The song was not changed.");
        }

        // Move the old autosave aside (never delete it) before the engine writes its own next to the song
        if (hasAutosave)
        {
            const auto aside = makeBackupFile (file, "autosave");

            if (! autosave.moveFileTo (aside))
                return fail ("Could not move the old autosave aside: " + autosave.getFullPathName());

            log ("Old autosave kept as " + aside.getFileName());
        }

        return adoptEdit (te::loadEditFromFile (engine, file), file, false);
    }

    bool Session::saveSong()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (isPlaying() || isRecording())
        {
            saveWhenStopped = true;   // writing during playback can cause dropouts
            log ("Save requested during playback: will save when stopped");
            return true;
        }

        return writeSongNow();
    }

    bool Session::closeSong()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return true;

        stopTimer();
        abandonExport();   // its jobs hold the song's tracks
        referenceScan.reset();
        referenceLufsValid = false;

        auto& transport = edit->getTransport();
        transport.removeChangeListener (this);

        if (transport.isPlaying() || transport.isRecording())
            transport.stop (false, true);   // false = keep any take that was being recorded

        saveWhenStopped = false;
        endAllParameterGestures();   // a fader held while the song closes
        edit->getAutomationRecordManager().punchOut (false);   // what Write was recording goes in before saving
        const bool saved = writeSongNow (true);

        listeners.call ([] (Listener& l) { l.sessionSongClosing(); });

        if (meters != nullptr)
            meters->releaseSong();   // it holds the tracks' level meters: let go before they are destroyed

        edit->state.removeListener (this);
        selectionManager.deselectAll();
        clipboard.reset();   // clip state refers to files relative to this song: it can't be pasted into another
        fileOps.reset();
        edit.reset();
        log ("Closed " + songFile.getFileName());
        songFile = juce::File();

        listeners.call ([] (Listener& l) { l.sessionSongChanged(); });
        return saved;
    }

    void Session::autosaveNow()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        lastAutosave = juce::Time::getCurrentTime();

        if (edit == nullptr || isRecording())
            return;

        // Compare content rather than Tracktion's "changed" flag, which only starts tracking after
        // the first message loop turn and is updated asynchronously (see docs/LESSONS.md)
        const auto fingerprint = getContentFingerprint();

        if (fingerprint == lastWrittenFingerprint)
            return;

        if (edit->isSaveInhibited())
            return;   // the song is mid-operation; try again next time

        // Writes the crash-recovery copy (".tmp_<song>") next to the song, never the song itself
        if (isPlaying())
        {
            // Background write (fileOps keeps the writer thread alive): safe for the audio thread,
            // but its success can't be checked
            fileOps->saveTempVersion (false);
            lastWrittenFingerprint = fingerprint;
            return;
        }

        if (fileOps->saveTempVersion (true))
            lastWrittenFingerprint = fingerprint;
        else
            log ("Warning: autosave failed for " + songFile.getFullPathName());
    }

    juce::String Session::getSongName() const    { return songFile.getFileNameWithoutExtension(); }
    juce::File Session::getSongFile() const      { return songFile; }

    juce::File Session::getAutosaveFile (const juce::File& file)
    {
        return te::EditFileOperations::getTempVersionOfEditFile (file);
    }

    bool Session::hasNewerAutosave (const juce::File& file)
    {
        const auto autosave = getAutosaveFile (file);

        return file.existsAsFile() && autosave.existsAsFile()
                && autosave.getLastModificationTime() > file.getLastModificationTime();
    }

    //==============================================================================
    bool Session::adoptEdit (std::unique_ptr<te::Edit> newEdit, const juce::File& file, bool isNewSong)
    {
        if (newEdit == nullptr)
            return fail ("The audio engine could not load " + file.getFileName());

        closeSong();   // reports its own problems; the new song still opens

        edit = std::move (newEdit);
        edit->state.addListener (this);
        fileOps = std::make_unique<te::EditFileOperations> (*edit);
        songFile = file;
        saveWhenStopped = false;
        wasRecording = false;
        lastWrittenFingerprint = 0;
        lastSavedQuickPrint = 0;
        unsavedCache = false;

        edit->playInStopEnabled = true;

        if (isNewSong)
        {
            edit->ensureNumberOfAudioTracks (defaultTrackCount);
            colourNewTracks (0);
        }
        else
        {
            colourUncolouredTracks();   // songs from before track colours: the design's colour bars everywhere
        }

        ensureMasterTap();
        ensureMasterPlugins();   // the mastering chain (off) before the meter, the reference after it
        syncBusRouting();
        edit->getAutomationRecordManager().setWritingAutomation (false);   // saved with the song: never start a song recording

        connectInputs (isNewSong);
        edit->getTransport().addChangeListener (this);

        AppPaths::rememberLastSongFile (songFile);
        writeSongNow();   // proves the folder is writable now, not after an hour of work

        lastAutosave = juce::Time::getCurrentTime();
        startTimer (autosaveCheckIntervalMs);
        log ((isNewSong ? "Created " : "Opened ") + songFile.getFullPathName());

        listeners.call ([] (Listener& l) { l.sessionSongChanged(); });
        notifyStateChanged();
        return true;
    }

    void Session::ensureMasterTap()
    {
        // The main meter measures the end of the mastering chain. Every song gets one; ensureMasterPlugins
        // then puts the chain before it and the reference after it.
        auto& masters = edit->getMasterPluginList();

        for (auto* plugin : masters)
            if (dynamic_cast<MasterTapPlugin*> (plugin) != nullptr)
                return;

        if (auto tap = edit->getPluginCache().createNewPlugin (MasterTapPlugin::xmlTypeName, {}))
        {
            masters.insertPlugin (tap, -1, nullptr);
            edit->getUndoManager().clearUndoHistory();   // not something you can undo: it's part of the app
        }
        else
        {
            log ("Warning: could not add the main meter to the song");
        }
    }

    bool Session::writeSongNow (bool mustWriteNow)
    {
        jassert (edit != nullptr && ! isRecording());

        if (edit->isSaveInhibited())
        {
            if (! mustWriteNow)
            {
                // Mid-operation: the Edit is temporarily changed and must not be written. Try again shortly.
                saveWhenStopped = true;
                log ("Save postponed: song is mid-operation");
                return true;
            }

            // Closing while busy: the song file is left untouched, but the work is not lost
            const auto savedCopy = writeEmergencyCopy();
            reportProblem ("\"" + getSongName() + "\" was busy and couldn't be saved normally.\n\n"
                           + (savedCopy ? "A copy of your work was saved; see the log for where."
                                        : "An emergency copy could not be saved either."));
            return false;
        }

        // Stamp the format (undo-free: it isn't an edit the user made)
        if ((int) edit->state[formatVersionProp] != songFormatVersion)
            edit->state.setProperty (formatVersionProp, songFormatVersion, nullptr);

       #ifdef JUCE_APPLICATION_VERSION_STRING
        if (edit->state[savedWithProp].toString() != "EzDAWsIt " JUCE_APPLICATION_VERSION_STRING)
            edit->state.setProperty (savedWithProp, "EzDAWsIt " JUCE_APPLICATION_VERSION_STRING, nullptr);
       #endif

        const auto fingerprint = getContentFingerprint();
        bool saved = false;
        fileOps->save (false, true, false, [&saved] (bool ok) { saved = ok; });   // synchronous

        if (saved)
        {
            lastWrittenFingerprint = fingerprint;
            lastSavedQuickPrint = getQuickFingerprint();
            unsavedCache = false;
            maybeEdited = false;
            return true;
        }

        const auto savedCopy = writeEmergencyCopy();
        reportProblem ("Could not save \"" + getSongName() + "\" to\n" + songFile.getFullPathName() + "\n\n"
                       + (savedCopy ? "A copy of your work was saved; see the log for where."
                                    : "An emergency copy could not be saved either. Please use Save on a different drive."));
        return false;
    }

    juce::int64 Session::getQuickFingerprint() const
    {
        // As getContentFingerprint, without asking plugins for their settings (that can be slow, or glitch)
        auto state = edit->state.createCopy();
        state.removeChild (state.getChildWithName (te::IDs::TRANSPORT), nullptr);
        return state.toXmlString().hashCode64();
    }

    void Session::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
    {
        if (! tree.hasType (te::IDs::TRANSPORT))   // the playhead moving isn't an edit
            maybeEdited = true;
    }

    bool Session::hasUnsavedChanges()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return false;

        if (maybeEdited.exchange (false))
            unsavedCache = getQuickFingerprint() != lastSavedQuickPrint;   // an undo back to the saved song: saved again

        return unsavedCache;
    }

    bool Session::saveSongAs (const juce::String& newName)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (refuseWhileRecording ("saving as a new song"))
            return false;

        const auto oldFolder = songFile.getParentDirectory();
        const auto target = AppPaths::createSongFile (newName);

        if (target == juce::File())
            return fail ("Could not create a folder for the song in " + AppPaths::getProjectsFolder().getFullPathName());

        // The recordings and anything else in the song's folder go along (the song finds them next to it)
        for (const auto& entry : juce::RangedDirectoryIterator (oldFolder, true, "*", juce::File::findFiles))
        {
            const auto f = entry.getFile();
            const auto name = f.getFileName();

            if (f.hasFileExtension (AppPaths::songFileExtension) || name.startsWith (".tmp_") || name.endsWithIgnoreCase (".bak"))
                continue;

            const auto copy = target.getParentDirectory().getChildFile (f.getRelativePathFrom (oldFolder));
            copy.getParentDirectory().createDirectory();

            if (! f.copyFileTo (copy))
                return fail ("Could not copy " + f.getFileName() + " to the new song's folder");
        }

        edit->getTransport().stop (false, false);
        edit->flushState();

        if (! fileOps->writeToFile (target, false))
            return fail ("Could not write " + target.getFullPathName());

        log ("Saved as " + target.getFullPathName());
        return openSong (target);
    }

    juce::int64 Session::getContentFingerprint() const
    {
        // Everything that defines the song, minus the playhead (moving it isn't unsaved work)
        edit->flushState();
        auto state = edit->state.createCopy();
        state.removeChild (state.getChildWithName (te::IDs::TRANSPORT), nullptr);
        return state.toXmlString().hashCode64();
    }

    bool Session::writeEmergencyCopy()
    {
        const auto name = getSongName() + " (unsaved copy " + timestamp() + ")";
        const juce::File candidates[] =
        {
            songFile.getParentDirectory().getNonexistentChildFile (name, AppPaths::songFileExtension, false),
            AppPaths::getRootFolder().getChildFile ("Recovered").getNonexistentChildFile (name, AppPaths::songFileExtension, false)
        };

        for (auto& target : candidates)
        {
            target.getParentDirectory().createDirectory();

            if (fileOps->writeToFile (target, false))
            {
                log ("Emergency copy written to " + target.getFullPathName());
                return true;
            }
        }

        log ("Error: emergency copy failed");
        return false;
    }

    void Session::connectInputs (bool armFirstInputOnTrackOne)
    {
        audioEngine.enableAllInputs();
        edit->getTransport().ensureContextAllocated();

        if (armFirstInputOnTrackOne)
        {
            auto tracks = te::getAudioTracks (*edit);

            for (auto* input : edit->getAllInputDevices())
            {
                if (input->getInputDevice().getDeviceType() != te::InputDevice::waveDevice || tracks.isEmpty())
                    continue;

                auto* firstTrack = tracks.getFirst();

                if (auto result = input->setTarget (firstTrack->itemID, true, &edit->getUndoManager(), 0); ! result)
                    log ("Could not connect input: " + result.error());
                else
                    input->setRecordingEnabled (firstTrack->itemID, true);

                break;
            }
        }

        edit->restartPlayback();
    }

    bool Session::fail (const juce::String& reason)
    {
        lastError = reason;
        log ("Error: " + reason);
        return false;
    }

    void Session::reportProblem (const juce::String& message)
    {
        fail (message);
        listeners.call ([&message] (Listener& l) { l.sessionProblem (message); });
    }

    void Session::notifyStateChanged()
    {
        listeners.call ([] (Listener& l) { l.sessionStateChanged(); });
    }

    //==============================================================================
    void Session::togglePlay()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return;

        auto& transport = edit->getTransport();

        if (transport.isPlaying())
            transport.stop (false, false);
        else
            transport.play (false);
    }

    void Session::toggleRecord()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return;

        auto& transport = edit->getTransport();

        if (transport.isRecording())
            transport.stop (false, false);
        else
            transport.record (false);
    }

    void Session::returnToStart()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        // If recording, this ends the take; changeListenerCallback() then saves it
        if (edit != nullptr)
            edit->getTransport().setPosition (te::TimePosition());
    }

    bool Session::isPlaying() const     { return edit != nullptr && edit->getTransport().isPlaying(); }
    bool Session::isRecording() const   { return edit != nullptr && edit->getTransport().isRecording(); }

    juce::String Session::getPositionText() const
    {
        return edit != nullptr ? getBarsBeatsText() + "   " + getTimeText() : juce::String();
    }

    juce::String Session::getBarsBeatsText() const
    {
        if (edit == nullptr)
            return {};

        const auto barsBeats = edit->tempoSequence.toBarsAndBeats (edit->getTransport().getPosition());
        const auto sixteenth = juce::jlimit (0, 3, (int) (barsBeats.getFractionalBeats().inBeats() * 4.0));

        return juce::String (barsBeats.bars + 1) + "." + juce::String (barsBeats.getWholeBeats() + 1)
             + "." + juce::String (sixteenth + 1);
    }

    juce::String Session::getTimeText() const
    {
        return edit != nullptr ? formatTime (edit->getTransport().getPosition().inSeconds()) : juce::String();
    }

    //==============================================================================
    double Session::getBpm() const
    {
        if (edit != nullptr)
            if (auto* tempo = edit->tempoSequence.getTempo (0))
                return tempo->getBpm();

        return 120.0;
    }

    void Session::setBpm (double bpm)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return;

        if (auto* tempo = edit->tempoSequence.getTempo (0))
        {
            tempo->setBpm (juce::jlimit (minBpm, maxBpm, bpm));
            notifyStateChanged();
        }
    }

    //==============================================================================
    // Tempo, time signature, key
    namespace
    {
        const juce::Identifier keySetID ("ezKey");   // on a pitch setting: the user chose this key (Tracktion's default C major isn't a choice)

        /** Keeps clips on their beats across a change to the tempo map. */
        struct KeepClipsOnBeats
        {
            explicit KeepClipsOnBeats (te::Edit& e) : edit (e)  { snapshot.savePreChangeState (edit); }
            ~KeepClipsOnBeats()                                  { snapshot.remapEdit (edit); }

            te::Edit& edit;
            te::EditTimecodeRemapperSnapshot snapshot;
        };
    }

    std::vector<Session::TempoMarker> Session::getTempoMarkers() const
    {
        std::vector<TempoMarker> markers;

        if (edit != nullptr)
            for (auto* t : edit->tempoSequence.getTempos())
                markers.push_back ({ t->getStartBeat().inBeats(), t->getBpm(), t->getCurve() > -1.0f && t->getCurve() < 1.0f });

        return markers;
    }

    double Session::getBpmAt (te::TimePosition t) const
    {
        return edit != nullptr ? edit->tempoSequence.getBpmAt (t) : 120.0;
    }

    bool Session::setTempoChangeBpm (int index, double bpm, bool continuesGesture)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        auto* tempo = edit->tempoSequence.getTempo (index);

        if (tempo == nullptr)
            return fail ("There is no such tempo change");

        if (refuseWhileRecording ("changing the tempo"))
            return false;

        if (! continuesGesture)
            beginUndoStep();

        tempo->set (tempo->getStartBeat(), juce::jlimit (minBpm, maxBpm, bpm), tempo->getCurve(), true);
        notifyStateChanged();
        return true;
    }

    bool Session::setBpmAt (te::TimePosition t, double bpm, bool continuesGesture)
    {
        if (edit == nullptr)
            return fail ("No song is open");

        return setTempoChangeBpm (edit->tempoSequence.indexOfTempoAt (t), bpm, continuesGesture);
    }

    bool Session::addTempoChange (double beat, double bpm)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (beat <= 1.0e-6)
            return fail ("The song's first tempo is already there: change it instead");

        for (auto& m : getTempoMarkers())
            if (std::abs (m.beat - beat) < 1.0e-6)
                return fail ("There is already a tempo change there");

        if (refuseWhileRecording ("changing the tempo"))
            return false;

        beginUndoStep();
        auto& ts = edit->tempoSequence;

        {
            // Inside a glide, a new marker reshapes the glide: keep every clip on its beat throughout
            KeepClipsOnBeats keep (*edit);
            const auto before = ts.getBpmAt (ts.toTime (te::BeatPosition::fromBeats (beat)));
            auto added = ts.insertTempo (te::BeatPosition::fromBeats (beat), before, 1.0f);

            if (added == nullptr)
                return fail ("Could not add the tempo change");

            added->set (added->getStartBeat(), juce::jlimit (minBpm, maxBpm, bpm), 1.0f, false);
        }
        notifyStateChanged();
        return true;
    }

    bool Session::moveTempoChange (int index, double newBeat)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        auto& ts = edit->tempoSequence;
        auto* tempo = ts.getTempo (index);

        if (tempo == nullptr || index == 0)
            return fail (index == 0 ? "The song's first tempo stays at the start" : "There is no such tempo change");

        const auto previous = ts.getTempo (index - 1)->getStartBeat().inBeats();
        const auto next = index + 1 < ts.getNumTempos() ? ts.getTempo (index + 1)->getStartBeat().inBeats() : 1.0e12;

        if (newBeat <= previous + 1.0e-6 || newBeat >= next - 1.0e-6)
            return fail ("A tempo change can't move past its neighbours");

        if (refuseWhileRecording ("changing the tempo"))
            return false;

        beginUndoStep();
        tempo->set (te::BeatPosition::fromBeats (newBeat), tempo->getBpm(), tempo->getCurve(), true);
        notifyStateChanged();
        return true;
    }

    bool Session::setTempoRamp (int index, bool ramp)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        auto* tempo = edit->tempoSequence.getTempo (index);

        if (tempo == nullptr)
            return fail ("There is no such tempo change");

        if (refuseWhileRecording ("changing the tempo"))
            return false;

        beginUndoStep();
        tempo->set (tempo->getStartBeat(), tempo->getBpm(), ramp ? 0.0f : 1.0f, true);   // 0 = straight glide, 1 = jump
        notifyStateChanged();
        return true;
    }

    bool Session::deleteTempoChange (int index)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (index == 0 || edit->tempoSequence.getTempo (index) == nullptr)
            return fail (index == 0 ? "The song's first tempo can't be deleted" : "There is no such tempo change");

        if (refuseWhileRecording ("changing the tempo"))
            return false;

        beginUndoStep();
        edit->tempoSequence.removeTempo (index, true);
        notifyStateChanged();
        return true;
    }

    //==============================================================================
    bool Session::isValidTimeSig (int numerator, int denominator)
    {
        // Quarter-note bottoms only for now: in Tracktion an x/8 bar has eighth-note beats, so switching to /8
        // would double the speed of everything kept on its beats (see LESSONS.md)
        return numerator >= 1 && numerator <= 32 && denominator == 4;
    }

    std::vector<Session::TimeSigMarker> Session::getTimeSigMarkers() const
    {
        std::vector<TimeSigMarker> markers;

        if (edit != nullptr)
            for (auto* t : edit->tempoSequence.getTimeSigs())
                markers.push_back ({ t->startBeatNumber.get().inBeats(), getBarAt (t->startBeatNumber.get().inBeats()).first,
                                     t->numerator.get(), t->denominator.get() });

        return markers;
    }

    std::pair<int, int> Session::getTimeSigAt (te::TimePosition t) const
    {
        if (edit == nullptr)
            return { 4, 4 };

        auto& sig = edit->tempoSequence.getTimeSigAt (t);
        return { sig.numerator.get(), sig.denominator.get() };
    }

    double Session::getBeatsPerBarAt (te::TimePosition t) const
    {
        const auto [n, d] = getTimeSigAt (t);
        return n * 4.0 / d;   // in quarter-note beats
    }

    double Session::getBarStartBeat (int bar) const
    {
        if (edit == nullptr)
            return bar * 4.0;

        return edit->tempoSequence.toBeats (te::tempo::BarsAndBeats { juce::jmax (0, bar), te::BeatDuration() }).inBeats();
    }

    std::pair<int, double> Session::getBarAt (double beat) const
    {
        if (edit == nullptr)
            return { (int) std::floor (beat / 4.0), std::fmod (beat, 4.0) };

        auto& ts = edit->tempoSequence;
        const auto bb = ts.toBarsAndBeats (ts.toTime (te::BeatPosition::fromBeats (juce::jmax (0.0, beat))));
        return { bb.bars, bb.beats.inBeats() };
    }

    std::vector<std::pair<double, bool>> Session::getGridLines (double fromBeat, double toBeat, double gridBeats) const
    {
        std::vector<std::pair<double, bool>> lines;
        fromBeat = juce::jmax (0.0, fromBeat);

        if (gridBeats <= 0.0 || toBeat <= fromBeat)
            return lines;

        auto bar = getBarAt (fromBeat).first;
        const auto barLength = getBarStartBeat (bar + 1) - getBarStartBeat (bar);

        if (gridBeats >= barLength - 1.0e-9)
        {
            // Whole bars, every n-th one
            const auto every = juce::jmax (1, juce::roundToInt (gridBeats / juce::jmax (1.0, barLength)));
            bar = (bar / every) * every;

            for (int guard = 0; guard < 10000; ++guard, bar += every)
            {
                const auto beat = getBarStartBeat (bar);

                if (beat > toBeat)
                    break;

                if (beat >= fromBeat - gridBeats)
                    lines.push_back ({ beat, true });
            }

            return lines;
        }

        // Lines restart at every bar line, so bars are always marked whatever the grid and the meter
        for (int guard = 0; guard < 10000; ++guard, ++bar)
        {
            const auto barStart = getBarStartBeat (bar), nextBar = getBarStartBeat (bar + 1);

            if (barStart > toBeat)
                break;

            for (auto beat = barStart; beat < nextBar - 1.0e-9 && beat <= toBeat; beat += gridBeats)
                if (beat >= fromBeat - gridBeats)
                    lines.push_back ({ beat, beat == barStart });
        }

        return lines;
    }

    te::TimeDuration Session::getBarLengthAt (te::TimePosition t) const
    {
        if (edit == nullptr)
            return te::TimeDuration::fromSeconds (2.0);

        auto& ts = edit->tempoSequence;
        const auto bar = getBarAt (ts.toBeats (t).inBeats()).first;
        return ts.toTime (te::BeatPosition::fromBeats (getBarStartBeat (bar + 1))) - ts.toTime (te::BeatPosition::fromBeats (getBarStartBeat (bar)));
    }

    void Session::keepTimeSigsOnBars (const std::vector<int>& bars, int skipIndex)
    {
        // After a change to the bars, later time signature changes go back onto the bars they were on
        auto& ts = edit->tempoSequence;

        for (int i = 1; i < ts.getNumTimeSigs() && i <= (int) bars.size(); ++i)
        {
            if (i == skipIndex)
                continue;

            auto* prev = ts.getTimeSig (i - 1);
            const auto prevBar = getBarAt (prev->startBeatNumber.get().inBeats()).first;
            const auto beat = prev->startBeatNumber.get().inBeats() + (bars[(size_t) i - 1] - prevBar) * prev->numerator.get() * 4.0 / prev->denominator.get();
            ts.getTimeSig (i)->startBeatNumber = te::BeatPosition::fromBeats (beat);
        }
    }

    std::vector<int> Session::getTimeSigBars() const
    {
        std::vector<int> bars;   // bars of the changes after the first, in order

        for (auto& m : getTimeSigMarkers())
            if (m.beat > 1.0e-6)
                bars.push_back (m.bar);

        return bars;
    }

    bool Session::setTimeSigAt (te::TimePosition t, int numerator, int denominator)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (! isValidTimeSig (numerator, denominator))
            return fail ("That isn't a time signature this app can use yet (top 1-32, bottom 4)");

        auto& sig = edit->tempoSequence.getTimeSigAt (t);

        if (sig.numerator.get() == numerator && sig.denominator.get() == denominator)
            return true;

        if (refuseWhileRecording ("changing the time signature"))
            return false;

        beginUndoStep();

        {
            const auto bars = getTimeSigBars();
            KeepClipsOnBeats keep (*edit);
            sig.numerator = numerator;
            sig.denominator = denominator;
            keepTimeSigsOnBars (bars);
        }

        notifyStateChanged();
        return true;
    }

    bool Session::addTimeSigChange (int bar, int numerator, int denominator)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (bar <= 0)
            return fail ("The song's first time signature is already there: change it instead");

        if (! isValidTimeSig (numerator, denominator))
            return fail ("That isn't a time signature this app can use yet (top 1-32, bottom 4)");

        const auto beat = getBarStartBeat (bar);

        for (auto& m : getTimeSigMarkers())
            if (std::abs (m.beat - beat) < 1.0e-6)
                return fail ("There is already a time signature change there");

        if (refuseWhileRecording ("changing the time signature"))
            return false;

        beginUndoStep();

        {
            // Later changes keep their bars
            auto bars = getTimeSigBars();
            bars.insert (std::lower_bound (bars.begin(), bars.end(), bar), bar);

            KeepClipsOnBeats keep (*edit);
            auto added = edit->tempoSequence.insertTimeSig (te::BeatPosition::fromBeats (beat));

            if (added == nullptr)
                return fail ("Could not add the time signature change");

            added->numerator = numerator;
            added->denominator = denominator;
            keepTimeSigsOnBars (bars);
        }

        notifyStateChanged();
        return true;
    }

    bool Session::moveTimeSigChange (int index, int newBar)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        auto& ts = edit->tempoSequence;
        auto* sig = ts.getTimeSig (index);

        if (sig == nullptr || index == 0)
            return fail (index == 0 ? "The song's first time signature stays at the start" : "There is no such time signature change");

        // Bars counted as they will be after the move: the previous signature runs up to it
        auto* prev = ts.getTimeSig (index - 1);
        const auto previous = prev->startBeatNumber.get().inBeats();
        const auto previousBar = getBarAt (previous).first;
        const auto beat = previous + (newBar - previousBar) * prev->numerator.get() * 4.0 / prev->denominator.get();
        // The next change keeps its bar, so compare bars (its beat will move with the new layout)
        const auto laterBars = getTimeSigBars();
        const auto nextBar = juce::isPositiveAndBelow (index, (int) laterBars.size()) ? laterBars[(size_t) index] : std::numeric_limits<int>::max();

        if (newBar <= previousBar || newBar >= nextBar)
            return fail ("A time signature change can't move past its neighbours");

        if (refuseWhileRecording ("changing the time signature"))
            return false;

        beginUndoStep();

        {
            auto bars = getTimeSigBars();
            KeepClipsOnBeats keep (*edit);
            sig->startBeatNumber = te::BeatPosition::fromBeats (beat);

            if (juce::isPositiveAndBelow (index - 1, (int) bars.size()))
                bars[(size_t) index - 1] = newBar;

            keepTimeSigsOnBars (bars, index);
        }

        notifyStateChanged();
        return true;
    }

    bool Session::deleteTimeSigChange (int index)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (index == 0 || edit->tempoSequence.getTimeSig (index) == nullptr)
            return fail (index == 0 ? "The song's first time signature can't be deleted" : "There is no such time signature change");

        if (refuseWhileRecording ("changing the time signature"))
            return false;

        beginUndoStep();

        {
            auto bars = getTimeSigBars();

            if (juce::isPositiveAndBelow (index - 1, (int) bars.size()))
                bars.erase (bars.begin() + (index - 1));

            KeepClipsOnBeats keep (*edit);
            edit->tempoSequence.removeTimeSig (index);
            keepTimeSigsOnBars (bars);
        }

        notifyStateChanged();
        return true;
    }

    //==============================================================================
    juce::String Session::describeKey (int root, bool minor)
    {
        static const char* const names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return juce::isPositiveAndBelow (root, 12) ? juce::String (names[root]) + (minor ? " minor" : " major") : juce::String ("No key");
    }

    std::vector<Session::KeyMarker> Session::getKeyMarkers() const
    {
        std::vector<KeyMarker> markers;

        if (edit != nullptr)
            for (auto* p : edit->pitchSequence.getPitches())
            {
                const bool chosen = p->state.getProperty (keySetID, false);
                markers.push_back ({ p->getStartBeatNumber().inBeats(), chosen ? ((p->getPitch() % 12) + 12) % 12 : -1,
                                     chosen && p->getScale() == te::Scale::minor });
            }

        return markers;
    }

    Session::KeyMarker Session::getKeyAt (te::TimePosition t) const
    {
        if (edit == nullptr || edit->pitchSequence.getNumPitches() == 0)
            return {};

        const auto beat = edit->tempoSequence.toBeats (t).inBeats();
        KeyMarker result;

        for (auto& m : getKeyMarkers())
            if (m.beat <= beat + 1.0e-9)
                result = m;

        return result;
    }

    bool Session::setKeyAt (te::TimePosition t, int root, bool minor)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr || edit->pitchSequence.getNumPitches() == 0)
            return fail ("No song is open");

        if (root < -1 || root > 11)
            return fail ("There is no such key");

        if (refuseWhileRecording ("changing the key"))
            return false;

        auto& p = edit->pitchSequence.getPitchAt (t);
        auto* um = &edit->getUndoManager();
        beginUndoStep();
        p.state.setProperty (keySetID, root >= 0, um);

        if (root >= 0)
        {
            p.setPitch (60 + root);
            p.setScaleID (minor ? te::Scale::minor : te::Scale::major);
        }

        notifyStateChanged();
        return true;
    }

    bool Session::addKeyChange (double beat, int root, bool minor)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (beat <= 1.0e-6)
            return fail ("The song's first key is already there: change it instead");

        if (root < -1 || root > 11)
            return fail ("There is no such key");

        for (auto& m : getKeyMarkers())
            if (std::abs (m.beat - beat) < 1.0e-6)
                return fail ("There is already a key change there");

        if (refuseWhileRecording ("changing the key"))
            return false;

        beginUndoStep();
        auto added = edit->pitchSequence.insertPitch (te::BeatPosition::fromBeats (beat), 60 + juce::jmax (0, root));

        if (added == nullptr)
            return fail ("Could not add the key change");

        added->setScaleID (minor ? te::Scale::minor : te::Scale::major);
        added->state.setProperty (keySetID, root >= 0, &edit->getUndoManager());
        notifyStateChanged();
        return true;
    }

    bool Session::moveKeyChange (int index, double newBeat)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        auto& ps = edit->pitchSequence;
        auto* p = ps.getPitch (index);

        if (p == nullptr || index == 0)
            return fail (index == 0 ? "The song's first key stays at the start" : "There is no such key change");

        const auto previous = ps.getPitch (index - 1)->getStartBeatNumber().inBeats();
        const auto next = index + 1 < ps.getNumPitches() ? ps.getPitch (index + 1)->getStartBeatNumber().inBeats() : 1.0e12;

        if (newBeat <= previous + 1.0e-6 || newBeat >= next - 1.0e-6)
            return fail ("A key change can't move past its neighbours");

        if (refuseWhileRecording ("changing the key"))
            return false;

        beginUndoStep();
        p->setStartBeat (te::BeatPosition::fromBeats (newBeat));
        notifyStateChanged();
        return true;
    }

    bool Session::deleteKeyChange (int index)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        auto* p = edit->pitchSequence.getPitch (index);

        if (index == 0 || p == nullptr)
            return fail (index == 0 ? "The song's first key can't be deleted" : "There is no such key change");

        if (refuseWhileRecording ("changing the key"))
            return false;

        beginUndoStep();
        p->removeFromEdit();
        notifyStateChanged();
        return true;
    }

    //==============================================================================
    // Sections
    te::TimePosition Session::beatToTime (double beat) const
    {
        return edit != nullptr ? edit->tempoSequence.toTime (te::BeatPosition::fromBeats (juce::jmax (0.0, beat))) : te::TimePosition();
    }

    te::MarkerClip* Session::findSectionClip (te::EditItemID id) const
    {
        if (edit == nullptr || ! id.isValid())
            return nullptr;

        if (auto* markers = edit->getMarkerTrack())
            for (auto* clip : markers->getClips())
                if (clip->itemID == id)
                    return dynamic_cast<te::MarkerClip*> (clip);

        return nullptr;
    }

    std::vector<Session::Section> Session::getSections() const
    {
        std::vector<Section> sections;

        if (edit == nullptr)
            return sections;

        if (auto* markers = edit->getMarkerTrack())
            for (auto* clip : markers->getClips())
                if (auto* marker = dynamic_cast<te::MarkerClip*> (clip))
                {
                    const auto range = edit->tempoSequence.toBeats (marker->getPosition().time);
                    sections.push_back ({ marker->itemID, marker->getName(), marker->getColour(),
                                          range.getStart().inBeats(), range.getEnd().inBeats() });
                }

        std::sort (sections.begin(), sections.end(), [] (const Section& a, const Section& b) { return a.startBeat < b.startBeat; });
        return sections;
    }

    std::optional<Session::Section> Session::getSection (te::EditItemID id) const
    {
        for (auto& s : getSections())
            if (s.id == id)
                return s;

        return std::nullopt;
    }

    bool Session::addSection (double startBeat, double endBeat, const juce::String& name)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        startBeat = juce::jmax (0.0, startBeat);

        for (auto& s : getSections())
        {
            if (startBeat >= s.startBeat - 1.0e-6 && startBeat < s.endBeat - 1.0e-6)
                return fail ("There is already a section there (" + s.name + ")");

            if (s.startBeat > startBeat)
                endBeat = juce::jmin (endBeat, s.startBeat);   // up to the next one
        }

        if (endBeat - startBeat < minSectionBeats - 1.0e-6)
            return fail ("A section must be at least a beat long");

        if (refuseWhileRecording ("adding sections"))
            return false;

        beginUndoStep();
        edit->ensureMarkerTrack();
        auto* markers = edit->getMarkerTrack();
        const auto trimmed = name.trim().substring (0, 40);
        auto* clip = markers != nullptr ? markers->insertNewClip (te::TrackItem::Type::marker, trimmed.isNotEmpty() ? trimmed : juce::String ("Section"),
                                                                  { beatToTime (startBeat), beatToTime (endBeat) }, nullptr)
                                        : nullptr;

        if (clip == nullptr)
            return fail ("Could not add the section");

        // A colour per kind of section, so the song's shape shows at a glance
        const auto lower = clip->getName().toLowerCase();
        const auto colour = lower.contains ("hook") || lower.contains ("chorus") ? juce::Colour (0xff8a6a3a)
                          : lower.contains ("verse")                             ? juce::Colour (0xff4b6a8a)
                          : lower.contains ("bridge") || lower.contains ("break") ? juce::Colour (0xff6a4b8a)
                          : lower.contains ("drop")                              ? juce::Colour (0xff8a3a4b)
                                                                                 : juce::Colour (0xff5a6170);
        clip->setColour (colour);
        notifyStateChanged();
        return true;
    }

    bool Session::renameSection (te::EditItemID id, const juce::String& name)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* clip = findSectionClip (id);
        const auto trimmed = name.trim().substring (0, 40);

        if (clip == nullptr)
            return fail ("That section has been deleted");

        if (trimmed.isEmpty())
            return fail ("A section needs a name");

        if (refuseWhileRecording ("renaming sections"))
            return false;

        beginUndoStep();
        clip->setName (trimmed);
        notifyStateChanged();
        return true;
    }

    bool Session::setSectionColour (te::EditItemID id, juce::Colour colour)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* clip = findSectionClip (id);

        if (clip == nullptr)
            return fail ("That section has been deleted");

        if (refuseWhileRecording ("changing sections"))
            return false;

        beginUndoStep();
        clip->setColour (colour);
        notifyStateChanged();
        return true;
    }

    bool Session::resizeSection (te::EditItemID id, double newStartBeat, double newEndBeat)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* clip = findSectionClip (id);

        if (clip == nullptr)
            return fail ("That section has been deleted");

        if (newStartBeat < -1.0e-9 || newEndBeat - newStartBeat < minSectionBeats - 1.0e-6)
            return fail ("A section must be at least a beat long");

        for (auto& s : getSections())
            if (s.id != id && newStartBeat < s.endBeat - 1.0e-6 && newEndBeat > s.startBeat + 1.0e-6)
                return fail ("Sections can't overlap (" + s.name + " is there)");

        if (refuseWhileRecording ("changing sections"))
            return false;

        beginUndoStep();
        clip->setPosition ({ { beatToTime (newStartBeat), beatToTime (newEndBeat) }, te::TimeDuration() });
        notifyStateChanged();
        return true;
    }

    bool Session::moveSection (te::EditItemID id, double newStartBeat)
    {
        const auto section = getSection (id);

        if (! section.has_value())
            return fail ("That section has been deleted");

        return resizeSection (id, newStartBeat, newStartBeat + section->endBeat - section->startBeat);
    }

    bool Session::deleteSection (te::EditItemID id)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* clip = findSectionClip (id);

        if (clip == nullptr)
            return fail ("That section has been deleted");

        if (refuseWhileRecording ("deleting sections"))
            return false;

        beginUndoStep();
        clip->removeFromParent();
        notifyStateChanged();
        return true;
    }

    bool Session::repeatSection (te::EditItemID id, int times)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        const auto section = getSection (id);

        if (! section.has_value())
            return fail ("That section has been deleted");

        if (times < 1 || times > 16)
            return fail ("A section can be repeated 1 to 16 times at once");

        if (refuseWhileRecording ("repeating sections"))
            return false;

        // A tempo or meter change inside the section can't be repeated faithfully (the copies would land off the grid)
        for (auto& m : getTempoMarkers())
            if (m.beat > section->startBeat + 1.0e-6 && m.beat < section->endBeat - 1.0e-6)
                return fail ("This section has a tempo change inside it: repeating it isn't supported yet");

        for (auto& m : getTimeSigMarkers())
        {
            if (m.beat > section->startBeat + 1.0e-6 && m.beat < section->endBeat - 1.0e-6)
                return fail ("This section has a time signature change inside it: repeating it isn't supported yet");

            if (m.beat >= section->endBeat - 1.0e-6)
            {
                const auto [bar, inBar] = getBarAt (section->endBeat);
                const auto [startBar, startInBar] = getBarAt (section->startBeat);

                if (inBar > 1.0e-6 || startInBar > 1.0e-6)
                    return fail ("Make the section whole bars first: a later time signature change would end up mid-bar");

                juce::ignoreUnused (bar, startBar);
            }
        }

        const auto start = beatToTime (section->startBeat), end = beatToTime (section->endBeat);
        const auto length = end - start;
        const auto amount = length * (double) times;

        beginUndoStep();

        // Make room after the section. Our own loop: Tracktion's ClipTrack::insertSpace stops early when its
        // clip list isn't sorted yet (it sorts asynchronously), which would leave later sections behind.
        const bool tempoFirst = ! edit->getTimecodeFormat().isBarsBeats();

        if (tempoFirst)
        {
            edit->pitchSequence.insertSpaceIntoSequence (end, amount, false);
            edit->tempoSequence.insertSpaceIntoSequence (end, amount, false);
        }

        for (auto* clipTrack : te::getTracksOfType<te::ClipTrack> (*edit, true))
        {
            te::split (*clipTrack, end);
            juce::Array<te::Clip*> later;

            for (auto* clip : clipTrack->getClips())
                if (clip->getPosition().getStart() >= end - te::TimeDuration::fromSeconds (1.0e-6))
                    later.add (clip);

            for (auto* clip : later)
                clip->setStart (clip->getPosition().getStart() + amount, false, true);
        }

        for (auto* item : te::getAllAutomatableEditItems (*edit))
            for (auto* param : item->getAutomatableParameters())
                param->getCurve().insertSpace (*param, end, amount);

        if (! tempoFirst)
        {
            edit->pitchSequence.insertSpaceIntoSequence (end, amount, false);
            edit->tempoSequence.insertSpaceIntoSequence (end, amount, false);
        }

        // The section's own clips (after the insert they are exactly the ones starting inside it)
        for (auto* track : te::getAudioTracks (*edit))
        {
            juce::Array<te::Clip*> toCopy;

            for (auto* clip : track->getClips())
                if (clip->getPosition().getStart() >= start - te::TimeDuration::fromSeconds (1.0e-6) && clip->getPosition().getStart() < end - te::TimeDuration::fromSeconds (1.0e-6))
                    toCopy.add (clip);

            for (int k = 1; k <= times; ++k)
                for (auto* clip : toCopy)
                    placeClipCopy (*clip, clip->getPosition().getStart() + length * (double) k, *track);
        }

        if (auto* markers = edit->getMarkerTrack())
            for (int k = 1; k <= times; ++k)
                if (auto* copy = markers->insertNewClip (te::TrackItem::Type::marker, section->name, { start + length * (double) k, end + length * (double) k }, nullptr))
                    copy->setColour (section->colour);

        notifyStateChanged();
        return true;
    }

    bool Session::deleteSectionAndMusic (te::EditItemID id)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        const auto section = getSection (id);

        if (! section.has_value())
            return fail ("That section has been deleted");

        if (refuseWhileRecording ("deleting sections"))
            return false;

        // What the music after the section plays at: it must keep playing at that (the delete removes
        // tempo, meter and key changes inside the range, including one at the section's start)
        const auto endTime = beatToTime (section->endBeat);
        const auto bpmAfter = edit->tempoSequence.getTempoAt (endTime).getBpm();
        const auto sigAfter = getTimeSigAt (endTime);
        const auto keyAfter = getKeyAt (endTime);

        beginUndoStep();
        selectionManager.deselectAll();   // nothing selected may point at a clip that goes
        te::deleteRegionOfTracks (*edit, { beatToTime (section->startBeat), endTime }, false, te::CloseGap::yes, nullptr);

        {
            auto& ts = edit->tempoSequence;
            const auto startBeat = te::BeatPosition::fromBeats (section->startBeat);
            const auto startTime = beatToTime (section->startBeat);

            // No remap: the music now at the start was recorded against these values
            if (std::abs (ts.getTempoAt (startTime).getBpm() - bpmAfter) > 1.0e-6)
                ts.insertTempo (startBeat, bpmAfter, 1.0f);

            if (getTimeSigAt (startTime) != sigAfter)
                if (auto sig = ts.insertTimeSig (startBeat))
                {
                    sig->numerator = sigAfter.first;
                    sig->denominator = sigAfter.second;
                }

            const auto keyNow = getKeyAt (startTime);

            if (keyNow.root != keyAfter.root || keyNow.minor != keyAfter.minor)
                if (auto p = edit->pitchSequence.insertPitch (startBeat, 60 + juce::jmax (0, keyAfter.root)))
                {
                    p->setScaleID (keyAfter.minor ? te::Scale::minor : te::Scale::major);
                    p->state.setProperty (keySetID, keyAfter.root >= 0, &edit->getUndoManager());
                }
        }

        if (auto* clip = findSectionClip (id))   // a marker exactly the length of the range may survive as a sliver
            clip->removeFromParent();

        notifyStateChanged();
        return true;
    }

    bool Session::selectSectionClips (te::EditItemID id)
    {
        const auto section = getSection (id);

        if (! section.has_value() || edit == nullptr)
            return fail ("That section has been deleted");

        const auto start = beatToTime (section->startBeat), end = beatToTime (section->endBeat);
        juce::Array<te::Clip*> clips;

        for (auto* track : te::getAudioTracks (*edit))
            for (auto* clip : track->getClips())
                if (clip->getPosition().getStart() >= start - te::TimeDuration::fromSeconds (1.0e-6) && clip->getPosition().getStart() < end - te::TimeDuration::fromSeconds (1.0e-6))
                    clips.add (clip);

        selectClips (clips);
        return true;
    }

    bool Session::isClickEnabled() const        { return edit != nullptr && edit->clickTrackEnabled.get(); }

    void Session::setClickEnabled (bool shouldBeEnabled)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return;

        edit->clickTrackEnabled = shouldBeEnabled;
        notifyStateChanged();
    }

    bool Session::isCountInEnabled() const
    {
        return edit != nullptr && edit->getCountInMode() != te::Edit::CountIn::none;
    }

    void Session::setCountInEnabled (bool shouldBeEnabled)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return;

        edit->setCountInMode (shouldBeEnabled ? te::Edit::CountIn::oneBar : te::Edit::CountIn::none);
        notifyStateChanged();
    }

    //==============================================================================
    void Session::addAudioTrack()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return;

        beginUndoStep();
        const auto before = getNumAudioTracks();
        edit->ensureNumberOfAudioTracks (before + 1);
        colourNewTracks (before);
        notifyStateChanged();
    }

    int Session::getNumAudioTracks() const
    {
        return edit != nullptr ? te::getAudioTracks (*edit).size() : 0;
    }

    bool Session::importAudioFile (const juce::File& file)
    {
        if (edit == nullptr)
            return fail ("No song is open");

        int trackIndex = 0;

        if (auto* selected = getSelectedClip())
            trackIndex = juce::jmax (0, getTrackIndex (selected->getClipTrack()));

        juce::StringArray problems;
        return importAudioFilesAt ({ file }, trackIndex, getPosition(), problems);
    }

    bool Session::importAudioFileAt (const juce::File& file, int trackIndex, te::TimePosition start)
    {
        if (getTrack (trackIndex) == nullptr)
            return fail ("No such track");

        juce::StringArray problems;
        return importAudioFilesAt ({ file }, trackIndex, start, problems);
    }

    bool Session::importAudioFilesAt (const juce::Array<juce::File>& files, int trackIndex,
                                      te::TimePosition start, juce::StringArray& problems)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (refuseWhileRecording ("importing audio"))
            return false;

        if (trackIndex < 0 || trackIndex > getNumAudioTracks())
            return fail ("No such track");

        // Check the files before changing anything, so a bad drop leaves no empty track behind
        juce::Array<juce::File> readable;

        for (const auto& file : files)
        {
            if (te::AudioFile (audioEngine.getEngine(), file).isValid())
                readable.add (file);
            else
                problems.add ("Not a readable audio file: " + file.getFileName());
        }

        if (readable.isEmpty())
            return fail (problems.isEmpty() ? juce::String ("Nothing to import") : problems.joinIntoString ("\n"));

        beginUndoStep();   // the new track (if any) and every file: one Ctrl+Z
        const bool makesNewTrack = trackIndex == getNumAudioTracks();

        if (makesNewTrack)
        {
            edit->ensureNumberOfAudioTracks (trackIndex + 1);
            colourNewTracks (trackIndex);
        }

        auto* track = getTrack (trackIndex);
        start = juce::jmax (te::TimePosition(), start);
        bool anyImported = false;

        for (const auto& file : readable)
        {
            if (auto length = insertAudioFile (file, *track, start); length.has_value())
            {
                start = start + *length;   // several files play one after another
                anyImported = true;
            }
            else
            {
                problems.add (lastError);
            }
        }

        if (! anyImported && makesNewTrack)   // e.g. the disk was full: don't leave an empty track behind
            edit->deleteTrack (track);

        notifyStateChanged();
        return anyImported || fail (problems.joinIntoString ("\n"));
    }

    std::optional<te::TimeDuration> Session::insertAudioFile (const juce::File& file, te::AudioTrack& track, te::TimePosition start)
    {
        // Keep a copy inside the song folder so moving/deleting the original can't break the song
        auto source = file;
        const auto songFolder = songFile.getParentDirectory();

        if (! file.isAChildOf (songFolder))
        {
            const auto importFolder = songFolder.getChildFile (importedAudioFolderName);
            source = importFolder.getNonexistentChildFile (file.getFileNameWithoutExtension(), file.getFileExtension(), false);

            if (! importFolder.createDirectory().wasOk() || ! file.copyFileTo (source))
            {
                fail ("Could not copy " + file.getFileName() + " into the song folder");
                return std::nullopt;
            }
        }

        const auto length = te::TimeDuration::fromSeconds (te::AudioFile (audioEngine.getEngine(), source).getLength());

        if (track.insertWaveClip (source.getFileNameWithoutExtension(), source, { { start, length }, {} }, false) == nullptr)
        {
            fail ("Could not add " + file.getFileName() + " to the song");
            return std::nullopt;
        }

        return length;
    }

    //==============================================================================
    // Tracks
    bool Session::deleteTrack (int trackIndex)
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return fail ("No such track");

        if (refuseWhileRecording ("deleting tracks"))
            return false;

        beginUndoStep();
        selectionManager.deselectAll();

        // A bus's tracks go where the bus went; a return takes its sends with it (all in this undo step)
        const auto kind = kindOf (*track);

        if (kind == TrackKind::bus)
            rerouteTracksInto (*track);
        else if (kind == TrackKind::fxReturn)
            for (const auto& r : getReturns())
                if (r.trackIndex == trackIndex)
                    removeSendsTo (r.bus);

        edit->deleteTrack (track);
        syncBusRouting();   // the tracks after it moved up a number
        notifyStateChanged();
        return true;
    }

    bool Session::duplicateTrack (int trackIndex)
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return fail ("No such track");

        if (refuseWhileRecording ("duplicating tracks"))
            return false;

        // A copied return needs a bus of its own (or both would play the same sends twice)
        const bool isReturn = kindOf (*track) == TrackKind::fxReturn;
        const auto newBus = isReturn ? findFreeBus() : -1;

        if (isReturn && newBus < 0)
            return fail ("A song can have up to " + juce::String (maxReturns) + " FX returns");

        beginUndoStep();
        auto copy = edit->copyTrack (track, te::TrackInsertPoint (*track, false));
        auto* copiedTrack = dynamic_cast<te::AudioTrack*> (copy.get());

        if (copiedTrack == nullptr)
            return fail ("Could not duplicate the track");

        copiedTrack->setName (makeUniqueTrackName (track->getName() + " copy"));

        if (isReturn)
        {
            for (auto* plugin : copiedTrack->pluginList)
                if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (plugin))
                    r->busNumber = newBus;
        }

        syncBusRouting();   // the tracks after it moved down a number
        notifyStateChanged();
        return true;
    }

    bool Session::moveTrack (int trackIndex, int newIndex)
    {
        auto* track = getTrack (trackIndex);
        auto* destination = getTrack (newIndex);

        if (track == nullptr || destination == nullptr)
            return fail ("No such track");

        if (trackIndex == newIndex)
            return true;

        if (refuseWhileRecording ("moving tracks"))
            return false;

        beginUndoStep();
        // Moving down: go after the destination; moving up: go before it
        edit->moveTrack (track, te::TrackInsertPoint (*destination, newIndex < trackIndex));
        syncBusRouting();
        notifyStateChanged();
        return getTrackIndex (track) == newIndex || fail ("Could not move the track");
    }

    bool Session::setTrackColour (int trackIndex, juce::Colour colour)
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return fail ("No such track");

        beginUndoStep();
        track->setColour (colour);
        return true;
    }

    const std::vector<Session::NamedColour>& Session::getTrackPalette()
    {
        static const std::vector<NamedColour> colours {
            { "Orange", juce::Colour (0xffe58a3a) }, { "Purple", juce::Colour (0xffb65bd6) },
            { "Green",  juce::Colour (0xff3fa36b) }, { "Blue",   juce::Colour (0xff4aa3ff) },
            { "Teal",   juce::Colour (0xff2fb8b0) }, { "Red",    juce::Colour (0xffd9534f) },
            { "Yellow", juce::Colour (0xffd8c03a) }, { "Pink",   juce::Colour (0xffd16ba5) } };
        return colours;
    }

    void Session::colourNewTracks (int firstIndex)
    {
        if (edit == nullptr)
            return;

        const auto& palette = getTrackPalette();
        const auto tracks = te::getAudioTracks (*edit);

        for (int i = juce::jmax (0, firstIndex); i < tracks.size(); ++i)
            if (tracks[i]->getColour().isTransparent())
                tracks[i]->setColour (palette[(size_t) i % palette.size()].colour);
    }

    void Session::colourUncolouredTracks()
    {
        if (edit == nullptr)
            return;

        // Not an edit the user made, so not an undo step (Ctrl+Z after opening shouldn't take the colours away)
        const auto& palette = getTrackPalette();
        const auto tracks = te::getAudioTracks (*edit);

        for (int i = 0; i < tracks.size(); ++i)
            if (tracks[i]->getColour().isTransparent())
                tracks[i]->state.setProperty (te::IDs::colour,
                                              (kindOf (*tracks[i]) == TrackKind::bus ? getBusColour() : palette[(size_t) i % palette.size()].colour).toString(),
                                              nullptr);
    }

    juce::Colour Session::getTrackColour (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);
        return track != nullptr ? track->getColour() : juce::Colour();
    }

    //==============================================================================
    juce::Array<Session::InstrumentChoice> Session::getAvailableInstruments() const
    {
        juce::Array<InstrumentChoice> choices;
        choices.add ({ "Synth (4OSC)", te::FourOscPlugin::xmlTypeName, {} });
        choices.add ({ "Sampler",      te::SamplerPlugin::xmlTypeName, {} });
        choices.add ({ "Drum Machine", DrumMachinePlugin::xmlTypeName, {} });

        for (const auto& description : audioEngine.getEngine().getPluginManager().knownPluginList.getTypes())
            if (description.isInstrument)
                choices.add ({ description.name, te::ExternalPlugin::xmlTypeName, description });

        return choices;
    }

    bool Session::addInstrumentTrack (const InstrumentChoice& choice, bool openInstrumentWindow, int drumKitIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (isRecording())
            return fail ("Stop recording before adding an instrument");

        edit->getUndoManager().beginNewTransaction();   // one Ctrl+Z removes the whole thing

        // Load the instrument first: if it fails, the song is left exactly as it was
        auto plugin = edit->getPluginCache().createNewPlugin (choice.pluginType, choice.description);

        if (plugin == nullptr)
            return fail ("Could not create the instrument \"" + choice.name + "\"");

        if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
            if (external->getAudioPluginInstance() == nullptr)
                return fail ("Could not load \"" + choice.name + "\": " + external->getLoadError());

        const auto tracksBefore = getNumAudioTracks();
        edit->ensureNumberOfAudioTracks (tracksBefore + 1);
        colourNewTracks (tracksBefore);
        auto* track = te::getAudioTracks (*edit).getLast();

        if (track == nullptr)
            return fail ("Could not add a track");

        auto* drums = dynamic_cast<DrumMachinePlugin*> (plugin.get());
        track->setName (drums != nullptr ? juce::String (drumTrackName) : choice.name);
        track->pluginList.insertPlugin (plugin, 0, nullptr);
        routeMidiInputsTo (*track);

        if (drums != nullptr && ! applyDrumKit (*drums, drumKitIndex))   // part of the same undo step
            log ("Drum Machine added without a kit: " + lastError);

        if (openInstrumentWindow)
            plugin->showWindowExplicitly();

        log ("Added instrument track: " + choice.name);
        notifyStateChanged();   // the mixer adds its strip
        return true;
    }

    juce::StringArray Session::getPluginNamesOnTrack (int audioTrackIndex) const
    {
        juce::StringArray names;

        if (edit != nullptr)
            if (auto* track = te::getAudioTracks (*edit)[audioTrackIndex])
                for (auto* plugin : track->pluginList)
                    names.add (plugin->getName());

        return names;
    }

    //==============================================================================
    // Effects
    te::AudioTrack* Session::getTrack (int trackIndex) const
    {
        return edit != nullptr ? te::getAudioTracks (*edit)[trackIndex] : nullptr;
    }

    juce::Array<te::Plugin*> Session::getEffects (int trackIndex) const
    {
        juce::Array<te::Plugin*> effects;

        if (auto* track = getTrack (trackIndex))
            for (auto* plugin : track->pluginList)
                if (dynamic_cast<te::VolumeAndPanPlugin*> (plugin) == nullptr
                     && dynamic_cast<te::LevelMeterPlugin*> (plugin) == nullptr
                     && dynamic_cast<te::AuxSendPlugin*> (plugin) == nullptr     // sends and returns have their own controls
                     && dynamic_cast<te::AuxReturnPlugin*> (plugin) == nullptr
                     && ! plugin->isSynth())
                    effects.add (plugin);

        return effects;
    }

    juce::Array<Session::PluginChoice> Session::getAvailableEffects() const
    {
        // The first seven keep their places (older code and tests pick them by index); ours follow
        juce::Array<PluginChoice> choices;
        const auto add = [&choices] (const char* name, const char* type) { choices.add ({ name, type, {}, Effects::categoryOf (type) }); };
        add ("EQ",           te::EqualiserPlugin::xmlTypeName);
        add ("Compressor",   te::CompressorPlugin::xmlTypeName);
        add ("Reverb",       te::ReverbPlugin::xmlTypeName);
        add ("Delay",        te::DelayPlugin::xmlTypeName);
        add ("Chorus",       te::ChorusPlugin::xmlTypeName);
        add ("Phaser",       te::PhaserPlugin::xmlTypeName);
        add ("Filter",       te::LowPassPlugin::xmlTypeName);
        add ("Gate",         GatePlugin::xmlTypeName);
        add ("De-esser",     DeEsserPlugin::xmlTypeName);
        add ("Limiter",      LimiterPlugin::xmlTypeName);
        add ("Low/high cut", CutFilterPlugin::xmlTypeName);
        add ("Saturator",    SaturatorPlugin::xmlTypeName);
        add ("Tune",         TunePlugin::xmlTypeName);
        add ("Tremolo",      TremoloPlugin::xmlTypeName);
        add ("Utility",      UtilityPlugin::xmlTypeName);
        add ("Vocal Chain",  VocalChainPlugin::xmlTypeName);
        add ("Amp",          AmpPlugin::xmlTypeName);
        add ("Tuner",        TunerPlugin::xmlTypeName);

        for (const auto& description : audioEngine.getEngine().getPluginManager().knownPluginList.getTypes())
            if (! description.isInstrument)
                choices.add ({ description.name, te::ExternalPlugin::xmlTypeName, description });

        return choices;
    }

    juce::ChangeBroadcaster& Session::getPluginListBroadcaster()
    {
        return audioEngine.getEngine().getPluginManager().knownPluginList;
    }

    bool Session::setMainOutput (const juce::String& outputID)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (isRecording())
            return fail ("Stop recording before changing the main output");

        if (! audioEngine.setMainOutput (outputID))
            return fail ("Could not switch the main output");

        notifyStateChanged();
        return true;
    }

    juce::StringArray Session::getEffectNames (int trackIndex) const
    {
        juce::StringArray names;

        for (auto* plugin : getEffects (trackIndex))
            names.add (plugin->getName());

        return names;
    }

    bool Session::addEffect (int trackIndex, const PluginChoice& choice, bool openWindow)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return fail ("No such track");

        auto plugin = edit->getPluginCache().createNewPlugin (choice.pluginType, choice.description);

        if (plugin == nullptr)
            return fail ("Could not create the effect \"" + choice.name + "\"");

        if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
            if (external->getAudioPluginInstance() == nullptr)
                return fail ("Could not load \"" + choice.name + "\": " + external->getLoadError());

        edit->getUndoManager().beginNewTransaction();

        // Tune starts in the song's key (where the playhead is), so it pulls to the right notes at once
        if (const auto key = getKeyAt (getPosition()); key.root >= 0)
        {
            if (auto* tune = dynamic_cast<TunePlugin*> (plugin.get()))
            {
                tune->set (TunePlugin::root, (float) key.root);
                tune->set (TunePlugin::scale, key.minor ? 2.0f : 1.0f);
            }
            else if (auto* chain = dynamic_cast<VocalChainPlugin*> (plugin.get()))
            {
                chain->set (VocalChainPlugin::root, (float) key.root);
                chain->set (VocalChainPlugin::scale, key.minor ? 2.0f : 1.0f);
            }
        }

        // Effects go after the instrument/other effects, but before pre-fader sends and the track's fader
        int index = -1;

        for (int i = 0; i < track->pluginList.size(); ++i)
        {
            auto* p = track->pluginList[i];

            if (dynamic_cast<te::AuxSendPlugin*> (p) != nullptr || p == track->getVolumePlugin())
            {
                index = i;
                break;
            }
        }

        const auto before = track->pluginList.size();
        track->pluginList.insertPlugin (plugin, index, nullptr);

        // Tracktion drops a plugin silently when the track is full: say so instead of pretending
        if (track->pluginList.size() != before + 1)
            return fail ("This track is full (" + juce::String (edit->engine.getEngineBehaviour().getEditLimits().maxPluginsOnTrack)
                         + " devices, counting its fader, meter, instrument and sends): remove an effect first");

        if (openWindow)
            plugin->showWindowExplicitly();

        notifyStateChanged();
        return true;
    }

    bool Session::removeEffect (int trackIndex, int effectIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto effects = getEffects (trackIndex);

        if (! juce::isPositiveAndBelow (effectIndex, effects.size()))
            return fail ("No such effect");

        edit->getUndoManager().beginNewTransaction();
        effects[effectIndex]->deleteFromParent();
        notifyStateChanged();
        return true;
    }

    bool Session::openEffectWindow (int trackIndex, int effectIndex)
    {
        auto effects = getEffects (trackIndex);

        if (! juce::isPositiveAndBelow (effectIndex, effects.size()))
            return fail ("No such effect");

        effects[effectIndex]->showWindowExplicitly();
        return true;
    }

    bool Session::openInstrumentWindow (int trackIndex)
    {
        if (auto* track = getTrack (trackIndex))
            for (auto* plugin : track->pluginList)
                if (plugin->isSynth())
                {
                    plugin->showWindowExplicitly();
                    return true;
                }

        return fail ("This track has no instrument");
    }

    namespace
    {
        Session::DeviceInfo describePlugin (te::Plugin& plugin)
        {
            const bool isExternal = dynamic_cast<te::ExternalPlugin*> (&plugin) != nullptr;
            return { plugin.getName(), isExternal ? plugin.getVendor() : juce::String ("Built-in"), ! isExternal, plugin.isEnabled(),
                     plugin.itemID.toString(), plugin.getPluginType() };
        }
    }

    juce::Array<Session::DeviceInfo> Session::getEffectInfo (int trackIndex) const
    {
        juce::Array<DeviceInfo> info;

        for (auto* plugin : getEffects (trackIndex))
            info.add (describePlugin (*plugin));

        return info;
    }

    te::Plugin* Session::findEffect (int trackIndex, const juce::String& id) const
    {
        for (auto* plugin : getEffects (trackIndex))
            if (plugin->itemID.toString() == id)
                return plugin;

        return nullptr;
    }

    std::optional<Session::DeviceInfo> Session::getInstrumentInfo (int trackIndex) const
    {
        if (auto* track = getTrack (trackIndex))
            for (auto* plugin : track->pluginList)
                if (plugin->isSynth())
                    return describePlugin (*plugin);

        return std::nullopt;
    }

    bool Session::setEffectEnabled (int trackIndex, int effectIndex, bool shouldBeEnabled)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto effects = getEffects (trackIndex);

        if (! juce::isPositiveAndBelow (effectIndex, effects.size()))
            return fail ("No such effect");

        beginUndoStep();
        effects[effectIndex]->setEnabled (shouldBeEnabled);
        notifyStateChanged();
        return true;
    }

    bool Session::moveEffect (int trackIndex, int fromIndex, int toIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto effects = getEffects (trackIndex);

        if (! juce::isPositiveAndBelow (fromIndex, effects.size()) || ! juce::isPositiveAndBelow (toIndex, effects.size()))
            return fail ("No such effect");

        if (fromIndex == toIndex)
            return true;

        if (refuseWhileRecording ("reordering effects"))
            return false;

        // Effects are children of the track's plugin list, among the volume fader and meter:
        // move in that list, to where the target effect sits now
        auto parent = effects[fromIndex]->state.getParent();
        const auto from = parent.indexOf (effects[fromIndex]->state);
        const auto to = parent.indexOf (effects[toIndex]->state);

        if (from < 0 || to < 0)
            return fail ("Could not move the effect");

        beginUndoStep();
        parent.moveChild (from, to, &edit->getUndoManager());
        notifyStateChanged();
        return true;
    }

    //==============================================================================
    // Notes
    Session::NoteList Session::notesInClip (te::MidiClip& clip, const NoteList& wanted) const
    {
        NoteList valid;

        if (clip.getClipTrack() != nullptr)   // not deleted
        {
            const auto& all = clip.getSequence().getNotes();

            for (auto* n : wanted)
                if (all.contains (n))
                    valid.addIfNotAlreadyThere (n);
        }

        return valid;
    }

    double Session::getContentStartInSongBeats (const te::MidiClip& clip) const
    {
        return clip.edit.tempoSequence.toBeats (clip.getTimeOfContentBeat (te::BeatPosition())).inBeats();
    }

    te::MidiNote* Session::addMidiNote (te::MidiClip& clip, int pitch, double startBeat, double lengthBeats, int velocity)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (clip.getClipTrack() == nullptr || edit == nullptr)
        {
            fail ("That clip has been deleted");
            return nullptr;
        }

        if (pitch < 0 || pitch > 127 || startBeat < 0.0)
        {
            fail ("A note can't go there");
            return nullptr;
        }

        if (refuseWhileRecording ("adding notes"))
            return nullptr;

        beginUndoStep();
        auto* note = clip.getSequence().addNote (pitch, te::BeatPosition::fromBeats (startBeat),
                                                 te::BeatDuration::fromBeats (juce::jmax (minNoteBeats, lengthBeats)),
                                                 juce::jlimit (1, 127, velocity), 0, &edit->getUndoManager());

        if (note == nullptr)
            fail ("Could not add the note");

        return note;
    }

    bool Session::deleteMidiNotes (te::MidiClip& clip, const NoteList& wanted)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto notes = notesInClip (clip, wanted);

        if (notes.isEmpty())
            return fail ("No notes selected");

        if (refuseWhileRecording ("deleting notes"))
            return false;

        beginUndoStep();

        for (auto* n : notes)
            clip.getSequence().removeNote (*n, &edit->getUndoManager());

        return true;
    }

    bool Session::moveMidiNotes (te::MidiClip& clip, const NoteList& wanted, double deltaBeats, int deltaPitch)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto notes = notesInClip (clip, wanted);

        if (notes.isEmpty())
            return fail ("No notes selected");

        for (auto* n : notes)   // all or nothing
        {
            if (! juce::isPositiveAndBelow (n->getNoteNumber() + deltaPitch, 128))
                return fail ("Notes can't go higher or lower than that");

            if (n->getStartBeat().inBeats() + deltaBeats < 0.0)
                return fail ("Notes can't start before the clip's content");
        }

        if (refuseWhileRecording ("moving notes"))
            return false;

        beginUndoStep();
        auto* um = &edit->getUndoManager();

        for (auto* n : notes)
        {
            n->setStartAndLength (n->getStartBeat() + te::BeatDuration::fromBeats (deltaBeats), n->getLengthBeats(), um);
            n->setNoteNumber (n->getNoteNumber() + deltaPitch, um);
        }

        return true;
    }

    bool Session::resizeMidiNotes (te::MidiClip& clip, const NoteList& wanted, double deltaLengthBeats)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto notes = notesInClip (clip, wanted);

        if (notes.isEmpty())
            return fail ("No notes selected");

        if (refuseWhileRecording ("changing notes"))
            return false;

        beginUndoStep();

        for (auto* n : notes)
            n->setStartAndLength (n->getStartBeat(),
                                  te::BeatDuration::fromBeats (juce::jmax (minNoteBeats, n->getLengthBeats().inBeats() + deltaLengthBeats)),
                                  &edit->getUndoManager());

        return true;
    }

    bool Session::setMidiNoteVelocity (te::MidiClip& clip, const NoteList& wanted, int velocity, bool continuesGesture)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto notes = notesInClip (clip, wanted);

        if (notes.isEmpty())
            return fail ("No notes selected");

        if (refuseWhileRecording ("changing notes"))
            return false;

        if (! continuesGesture)
            beginUndoStep();

        for (auto* n : notes)
            n->setVelocity (juce::jlimit (1, 127, velocity), &edit->getUndoManager());

        return true;
    }

    bool Session::quantiseMidiNotes (te::MidiClip& clip, const NoteList& wanted, double gridBeats, double strength, double swing)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (clip.getClipTrack() == nullptr || edit == nullptr)
            return fail ("That clip has been deleted");

        if (gridBeats <= 0.0)
            return fail ("Pick a grid size first");

        auto notes = notesInClip (clip, wanted);

        if (wanted.isEmpty())
            notes = clip.getSequence().getNotes();

        if (notes.isEmpty())
            return fail ("There are no notes to quantise");

        if (refuseWhileRecording ("quantising"))
            return false;

        // Snap to the song's grid, which may not line up with the clip's own beat 0
        const auto contentStart = getContentStartInSongBeats (clip);
        strength = juce::jlimit (0.0, 1.0, strength);
        swing = juce::jlimit (0.0, 1.0, swing);

        // Where line k is, in content beats: every second line is delayed by the swing
        const auto lineAt = [&] (double k)
        {
            const bool odd = ((long long) k & 1) != 0;
            return k * gridBeats + (odd ? swing * 0.5 * gridBeats : 0.0) - contentStart;
        };

        beginUndoStep();

        for (auto* n : notes)
        {
            const auto was = n->getStartBeat().inBeats();
            auto k = std::round ((was + contentStart) / gridBeats);

            // With swing the nearest line can be the one before or after the straight one. Exactly
            // half way goes to the earlier line: that is where a fully swung hit came from.
            if (std::abs (lineAt (k - 1.0) - was) <= std::abs (lineAt (k) - was) + 1.0e-9)
                k -= 1.0;
            else if (std::abs (lineAt (k + 1.0) - was) < std::abs (lineAt (k) - was) - 1.0e-9)
                k += 1.0;

            while (lineAt (k) < -1.0e-9)   // the nearest line is before the content: take the next one, still on the grid
                k += 1.0;

            const auto target = juce::jmax (0.0, lineAt (k));
            n->setStartAndLength (te::BeatPosition::fromBeats (was + (target - was) * strength), n->getLengthBeats(), &edit->getUndoManager());
        }

        return true;
    }

    bool Session::resizeMidiNotesFromStart (te::MidiClip& clip, const NoteList& wanted, double deltaBeats)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto notes = notesInClip (clip, wanted);

        if (notes.isEmpty())
            return fail ("No notes selected");

        // One amount for all of them, limited by whichever note hits a limit first
        auto lowest = -1.0e12, highest = 1.0e12;

        for (auto* n : notes)
        {
            lowest = juce::jmax (lowest, -n->getStartBeat().inBeats());
            highest = juce::jmin (highest, n->getLengthBeats().inBeats() - minNoteBeats);
        }

        deltaBeats = juce::jlimit (lowest, juce::jmax (lowest, highest), deltaBeats);

        if (std::abs (deltaBeats) < 1.0e-9)
            return true;   // nothing to change: no undo step either

        if (refuseWhileRecording ("changing notes"))
            return false;

        beginUndoStep();

        for (auto* n : notes)
            n->setStartAndLength (n->getStartBeat() + te::BeatDuration::fromBeats (deltaBeats),
                                  te::BeatDuration::fromBeats (n->getLengthBeats().inBeats() - deltaBeats),
                                  &edit->getUndoManager());

        return true;
    }

    bool Session::setMidiNoteLength (te::MidiClip& clip, const NoteList& wanted, double lengthBeats, bool continuesGesture)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto notes = notesInClip (clip, wanted);

        if (notes.isEmpty())
            return fail ("No notes selected");

        if (refuseWhileRecording ("changing notes"))
            return false;

        if (! continuesGesture)
            beginUndoStep();

        for (auto* n : notes)
            n->setStartAndLength (n->getStartBeat(), te::BeatDuration::fromBeats (juce::jmax (minNoteBeats, lengthBeats)), &edit->getUndoManager());

        return true;
    }

    bool Session::legatoMidiNotes (te::MidiClip& clip, const NoteList& wanted)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (clip.getClipTrack() == nullptr || edit == nullptr)
            return fail ("That clip has been deleted");

        auto notes = notesInClip (clip, wanted);

        if (wanted.isEmpty())
            notes = clip.getSequence().getNotes();

        if (notes.isEmpty())
            return fail ("There are no notes to join up");

        // What each note will become, worked out before anything changes
        std::vector<std::pair<te::MidiNote*, double>> changes;

        for (auto* n : notes)
        {
            const auto start = n->getStartBeat().inBeats();
            auto next = 1.0e12;

            for (auto* other : notes)
                if (other->getStartBeat().inBeats() > start + 1.0e-6)
                    next = juce::jmin (next, other->getStartBeat().inBeats());

            const auto newLength = next - start;

            if (next < 1.0e11 && newLength >= minNoteBeats && std::abs (newLength - n->getLengthBeats().inBeats()) > 1.0e-6)
                changes.emplace_back (n, newLength);
        }

        if (changes.empty())
            return true;   // already joined up: no undo step

        if (refuseWhileRecording ("changing notes"))
            return false;

        beginUndoStep();

        for (auto& [note, length] : changes)
            note->setStartAndLength (note->getStartBeat(), te::BeatDuration::fromBeats (length), &edit->getUndoManager());

        return true;
    }

    bool Session::copyMidiNotes (te::MidiClip& clip, const NoteList& wanted)
    {
        const auto notes = notesInClip (clip, wanted);

        if (notes.isEmpty())
            return fail ("No notes selected");

        auto earliest = 1.0e12;

        for (auto* n : notes)
            earliest = juce::jmin (earliest, n->getStartBeat().inBeats());

        noteClipboard.clear();

        for (auto* n : notes)
            noteClipboard.push_back ({ n->getNoteNumber(), n->getStartBeat().inBeats() - earliest,
                                       n->getLengthBeats().inBeats(), n->getVelocity() });

        return true;
    }

    bool Session::cutMidiNotes (te::MidiClip& clip, const NoteList& wanted)
    {
        const auto before = noteClipboard;

        if (! copyMidiNotes (clip, wanted))
            return false;

        if (deleteMidiNotes (clip, wanted))
            return true;

        noteClipboard = before;   // nothing was cut, so nothing was taken
        return false;
    }

    Session::NoteList Session::pasteMidiNotes (te::MidiClip& clip, double atBeat)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (clip.getClipTrack() == nullptr || edit == nullptr)
        {
            fail ("That clip has been deleted");
            return {};
        }

        if (noteClipboard.empty())
        {
            fail ("There are no copied notes to paste");
            return {};
        }

        if (refuseWhileRecording ("pasting notes"))
            return {};

        atBeat = juce::jmax (0.0, atBeat);
        beginUndoStep();
        NoteList added;

        for (auto& c : noteClipboard)
            if (auto* n = clip.getSequence().addNote (c.pitch, te::BeatPosition::fromBeats (atBeat + c.startBeat),
                                                      te::BeatDuration::fromBeats (c.lengthBeats), c.velocity, 0, &edit->getUndoManager()))
                added.add (n);

        return added;
    }

    Session::NoteList Session::duplicateMidiNotes (te::MidiClip& clip, const NoteList& wanted)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto notes = notesInClip (clip, wanted);

        if (notes.isEmpty())
        {
            fail ("No notes selected");
            return {};
        }

        if (refuseWhileRecording ("duplicating notes"))
            return {};

        auto earliest = 1.0e12, latest = 0.0;

        for (auto* n : notes)
        {
            earliest = juce::jmin (earliest, n->getStartBeat().inBeats());
            latest = juce::jmax (latest, n->getEndBeat().inBeats());
        }

        const auto span = latest - earliest;
        beginUndoStep();
        NoteList added;

        for (auto* n : notes)
            if (auto* copy = clip.getSequence().addNote (n->getNoteNumber(), n->getStartBeat() + te::BeatDuration::fromBeats (span),
                                                         n->getLengthBeats(), n->getVelocity(), 0, &edit->getUndoManager()))
                added.add (copy);

        return added;
    }

    Session::NoteList Session::copyMoveMidiNotes (te::MidiClip& clip, const NoteList& wanted, double deltaBeats, int deltaPitch)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto notes = notesInClip (clip, wanted);

        if (notes.isEmpty())
        {
            fail ("No notes selected");
            return {};
        }

        for (auto* n : notes)   // all or nothing, like moving
        {
            if (! juce::isPositiveAndBelow (n->getNoteNumber() + deltaPitch, 128))
            {
                fail ("Notes can't go higher or lower than that");
                return {};
            }

            if (n->getStartBeat().inBeats() + deltaBeats < 0.0)
            {
                fail ("Notes can't start before the clip's content");
                return {};
            }
        }

        if (refuseWhileRecording ("copying notes"))
            return {};

        beginUndoStep();
        NoteList added;

        for (auto* n : notes)
            if (auto* copy = clip.getSequence().addNote (n->getNoteNumber() + deltaPitch, n->getStartBeat() + te::BeatDuration::fromBeats (deltaBeats),
                                                         n->getLengthBeats(), n->getVelocity(), 0, &edit->getUndoManager()))
                added.add (copy);

        return added;
    }

    bool Session::isClipLooping (const te::MidiClip& clip) const
    {
        return clip.isLooping();
    }

    bool Session::setClipLooping (te::MidiClip& clip, bool shouldLoop)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (clip.getClipTrack() == nullptr || edit == nullptr)
            return fail ("That clip has been deleted");

        if (clip.isLooping() == shouldLoop)
            return true;

        if (refuseWhileRecording ("changing the clip"))
            return false;

        beginUndoStep();

        if (shouldLoop)   // repeat what the clip shows now
            clip.setLoopRangeBeats ({ clip.getContentBeatAtTime (clip.getPosition().getStart()),
                                      clip.getContentBeatAtTime (clip.getPosition().getEnd()) });
        else
            clip.disableLooping();

        return true;
    }

    //==============================================================================
    // Drum machine
    DrumMachinePlugin* Session::getDrumMachine (int trackIndex) const
    {
        if (auto* track = getTrack (trackIndex))
            for (auto* plugin : track->pluginList)
                if (auto* drums = dynamic_cast<DrumMachinePlugin*> (plugin))
                    return drums;

        return nullptr;
    }

    DrumMachinePlugin* Session::getDrumMachineFor (const te::Clip& clip) const
    {
        if (auto* track = dynamic_cast<te::AudioTrack*> (clip.getClipTrack()))
            for (auto* plugin : track->pluginList)
                if (auto* drums = dynamic_cast<DrumMachinePlugin*> (plugin))
                    return drums;

        return nullptr;
    }

    bool Session::addDrumMachineTrack (int kitIndex)
    {
        if (! juce::isPositiveAndBelow (kitIndex, DrumKits::getNumKits()))
            return fail ("There is no such drum kit");

        for (int pad = 0; pad < DrumMachinePlugin::numPads; ++pad)   // the sounds first: a failure changes nothing
        {
            juce::String error;

            if (DrumKits::getSampleFile (kitIndex, pad, error) == juce::File())
                return fail ("Could not make the kit's sounds: " + error);
        }

        if (! addInstrumentTrack ({ "Drum Machine", DrumMachinePlugin::xmlTypeName, {} }, false, kitIndex))
            return false;

        return getDrumMachine (getNumAudioTracks() - 1) != nullptr || fail ("Could not make the Drum Machine");
    }

    bool Session::checkDrumPad (DrumMachinePlugin& drums, int pad)
    {
        if (edit == nullptr || drums.getOwnerTrack() == nullptr)
            return fail ("That Drum Machine has been deleted");

        if (! juce::isPositiveAndBelow (pad, DrumMachinePlugin::numPads) || ! drums.getPadState (pad).isValid())
            return fail ("There is no such pad");

        return ! refuseWhileRecording ("changing the drum machine");
    }

    bool Session::applyDrumKit (DrumMachinePlugin& drums, int kitIndex)
    {
        if (! juce::isPositiveAndBelow (kitIndex, DrumKits::getNumKits()))
            return fail ("There is no such drum kit");

        // Make sure every sound exists before anything changes
        for (int pad = 0; pad < DrumMachinePlugin::numPads; ++pad)
        {
            juce::String error;

            if (DrumKits::getSampleFile (kitIndex, pad, error) == juce::File())
                return fail ("Could not make the kit's sounds: " + error);
        }

        auto* um = &edit->getUndoManager();
        drums.state.setProperty (DrumMachinePlugin::kitID, DrumKits::getKit (kitIndex).name, um);

        for (int pad = 0; pad < DrumMachinePlugin::numPads; ++pad)
        {
            auto p = drums.getPadState (pad);
            p.setProperty (DrumMachinePlugin::fileID, DrumMachinePlugin::makeKitSource (kitIndex, pad), um);
            p.setProperty (DrumMachinePlugin::nameID, DrumKits::getPadName (pad), um);

            for (auto v : { DrumMachinePlugin::Value::tune, DrumMachinePlugin::Value::decay, DrumMachinePlugin::Value::start,
                            DrumMachinePlugin::Value::pan, DrumMachinePlugin::Value::level })
                p.setProperty (DrumMachinePlugin::idFor (v), DrumMachinePlugin::defaultFor (v), um);

            p.setProperty (DrumMachinePlugin::chokeID, DrumKits::getDefaultChokeGroup (pad), um);
            p.setProperty (DrumMachinePlugin::reverseID, false, um);
        }

        log ("Loaded drum kit: " + juce::String (DrumKits::getKit (kitIndex).name));
        return true;
    }

    bool Session::loadDrumKit (DrumMachinePlugin& drums, int kitIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumPad (drums, 0))
            return false;

        beginUndoStep();
        return applyDrumKit (drums, kitIndex);
    }

    bool Session::setDrumPadProperty (DrumMachinePlugin& drums, int pad, const juce::Identifier& id, const juce::var& value, bool continuesGesture)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumPad (drums, pad))
            return false;

        auto p = drums.getPadState (pad);

        // Always start the step (an empty one is never kept): a drag whose first move changes nothing
        // must still not join whatever was done before it
        if (! continuesGesture)
            beginUndoStep();

        if (p.getProperty (id) == value)
            return true;

        p.setProperty (id, value, &edit->getUndoManager());
        return true;
    }

    bool Session::setDrumPadValue (DrumMachinePlugin& drums, int pad, DrumMachinePlugin::Value which, double value, bool continuesGesture)
    {
        return setDrumPadProperty (drums, pad, DrumMachinePlugin::idFor (which), DrumMachinePlugin::clampValue (which, value), continuesGesture);
    }

    bool Session::setDrumPadChoke (DrumMachinePlugin& drums, int pad, int group)
    {
        return setDrumPadProperty (drums, pad, DrumMachinePlugin::chokeID, juce::jlimit (0, DrumMachinePlugin::maxChokeGroup, group));
    }

    bool Session::setDrumPadMuted (DrumMachinePlugin& drums, int pad, bool b)      { return setDrumPadProperty (drums, pad, DrumMachinePlugin::muteID, b); }
    bool Session::setDrumPadSoloed (DrumMachinePlugin& drums, int pad, bool b)     { return setDrumPadProperty (drums, pad, DrumMachinePlugin::soloID, b); }
    bool Session::setDrumPadReversed (DrumMachinePlugin& drums, int pad, bool b)   { return setDrumPadProperty (drums, pad, DrumMachinePlugin::reverseID, b); }

    bool Session::renameDrumPad (DrumMachinePlugin& drums, int pad, const juce::String& name)
    {
        const auto trimmed = name.trim().substring (0, 40);

        if (trimmed.isEmpty())
            return fail ("A pad needs a name");

        return setDrumPadProperty (drums, pad, DrumMachinePlugin::nameID, trimmed);
    }

    bool Session::loadDrumPadSample (DrumMachinePlugin& drums, int pad, const juce::File& file)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumPad (drums, pad))
            return false;

        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

            if (reader == nullptr || reader->lengthInSamples <= 0)
                return fail (file.getFileName() + " isn't an audio file this app can read");
        }

        // A copy in the song's folder, stored relative to it, so the song can be moved
        const auto songFolder = songFile.getParentDirectory();
        auto source = file;

        if (! file.isAChildOf (songFolder))
        {
            const auto importFolder = songFolder.getChildFile (importedAudioFolderName);
            source = importFolder.getNonexistentChildFile (file.getFileNameWithoutExtension(), file.getFileExtension(), false);

            if (! importFolder.createDirectory().wasOk() || ! file.copyFileTo (source))
                return fail ("Could not copy " + file.getFileName() + " into the song folder");
        }

        beginUndoStep();
        auto p = drums.getPadState (pad);
        auto* um = &edit->getUndoManager();
        p.setProperty (DrumMachinePlugin::fileID, source.getRelativePathFrom (songFolder).replaceCharacter ('\\', '/'), um);
        p.setProperty (DrumMachinePlugin::nameID, file.getFileNameWithoutExtension().substring (0, 40), um);
        p.setProperty (DrumMachinePlugin::startID, 0.0, um);
        p.setProperty (DrumMachinePlugin::reverseID, false, um);
        return true;
    }

    bool Session::resetDrumPad (DrumMachinePlugin& drums, int pad)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumPad (drums, pad))
            return false;

        const auto kit = juce::jmax (0, DrumKits::findKit (drums.getKitName()));
        juce::String error;

        if (DrumKits::getSampleFile (kit, pad, error) == juce::File())
            return fail ("Could not make the kit's sound: " + error);

        beginUndoStep();
        auto p = drums.getPadState (pad);
        auto* um = &edit->getUndoManager();
        p.setProperty (DrumMachinePlugin::fileID, DrumMachinePlugin::makeKitSource (kit, pad), um);
        p.setProperty (DrumMachinePlugin::nameID, DrumKits::getPadName (pad), um);

        for (auto v : { DrumMachinePlugin::Value::tune, DrumMachinePlugin::Value::decay, DrumMachinePlugin::Value::start,
                        DrumMachinePlugin::Value::pan, DrumMachinePlugin::Value::level })
            p.setProperty (DrumMachinePlugin::idFor (v), DrumMachinePlugin::defaultFor (v), um);

        p.setProperty (DrumMachinePlugin::chokeID, DrumKits::getDefaultChokeGroup (pad), um);
        p.setProperty (DrumMachinePlugin::reverseID, false, um);
        return true;
    }

    bool Session::copyDrumPad (DrumMachinePlugin& drums, int from, int to)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumPad (drums, from) || ! checkDrumPad (drums, to))
            return false;

        if (from == to)
            return true;

        beginUndoStep();
        auto source = drums.getPadState (from);
        auto dest = drums.getPadState (to);

        for (const auto& id : { DrumMachinePlugin::nameID, DrumMachinePlugin::fileID, DrumMachinePlugin::tuneID, DrumMachinePlugin::decayID,
                                DrumMachinePlugin::startID, DrumMachinePlugin::panID, DrumMachinePlugin::levelID, DrumMachinePlugin::chokeID,
                                DrumMachinePlugin::reverseID })
            dest.setProperty (id, source.getProperty (id), &edit->getUndoManager());

        return true;
    }

    void Session::auditionDrumPad (DrumMachinePlugin& drums, int pad, int velocity)
    {
        if (auto* track = dynamic_cast<te::AudioTrack*> (drums.getOwnerTrack()))
            if (! isRecording() && juce::isPositiveAndBelow (pad, DrumMachinePlugin::numPads))
                track->playGuideNote (DrumMachinePlugin::noteForPad (pad), te::MidiChannel (1), juce::jlimit (1, 127, velocity), false, true, true);
    }

    //==============================================================================
    bool Session::checkDrumClip (te::MidiClip& clip)
    {
        if (clip.getClipTrack() == nullptr || edit == nullptr)
            return fail ("That clip has been deleted");

        return ! refuseWhileRecording ("changing the pattern");
    }

    double Session::getClipContentEndBeat (const te::MidiClip& clip) const
    {
        return clip.getContentBeatAtTime (clip.getPosition().getEnd()).inBeats();
    }

    Session::NoteList Session::getNotesInRange (te::MidiClip& clip, int pitch, double fromBeat, double toBeat) const
    {
        NoteList found;
        constexpr double tolerance = 1.0e-6;   // a note a hair before the step still counts

        if (clip.getClipTrack() != nullptr)
            for (auto* n : clip.getSequence().getNotes())
            {
                const auto start = n->getStartBeat().inBeats();

                if ((pitch < 0 || n->getNoteNumber() == pitch) && start >= fromBeat - tolerance && start < toBeat - tolerance)
                    found.add (n);
            }

        return found;
    }

    bool Session::setDrumStep (te::MidiClip& clip, int pitch, double stepStart, double stepBeats, bool on, int velocity,
                               bool continuesGesture, bool* changed, std::optional<double> placeAt)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (changed != nullptr)
            *changed = false;

        if (! checkDrumClip (clip))
            return false;

        if (! juce::isPositiveAndBelow (pitch, 128) || stepStart < -1.0e-9 || stepBeats <= 0.0)
            return fail ("A step can't go there");

        if (on && stepStart >= getClipContentEndBeat (clip) - 1.0e-9)
            return fail ("That step is past the end of the clip. Make the clip longer first.");

        const auto existing = getNotesInRange (clip, pitch, stepStart, stepStart + stepBeats);

        if (! continuesGesture)
            beginUndoStep();   // even with no change (an empty step is never kept): see setDrumPadProperty

        if (on == ! existing.isEmpty())
            return true;

        auto* um = &edit->getUndoManager();

        if (on)
            clip.getSequence().addNote (pitch, te::BeatPosition::fromBeats (juce::jmax (0.0, juce::jlimit (stepStart, stepStart + stepBeats * 0.99, placeAt.value_or (stepStart)))),
                                        te::BeatDuration::fromBeats (juce::jmax (minNoteBeats, stepBeats)),
                                        juce::jlimit (1, 127, velocity), 0, um);
        else
            for (auto* n : existing)
                clip.getSequence().removeNote (*n, um);

        if (changed != nullptr)
            *changed = true;

        return true;
    }

    bool Session::isPunchOn() const
    {
        return edit != nullptr && edit->recordingPunchInOut.get();
    }

    bool Session::setPunch (bool on)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (isRecording())
            return fail ("Stop recording before switching punch on or off");

        if (on)
        {
            const auto loop = getLoopRange();

            if (loop.getLength().inSeconds() < 0.05)
                return fail ("Set a loop region first (drag in the ruler): punch records only inside it");
        }

        edit->recordingPunchInOut = on;
        notifyStateChanged();
        return true;
    }

    int Session::getNumTakes (te::Clip& clip) const
    {
        return clip.hasAnyTakes() ? clip.getNumTakes (false) : 0;
    }

    int Session::getCurrentTake (te::Clip& clip) const
    {
        return clip.hasAnyTakes() ? clip.getCurrentTake() : 0;
    }

    juce::StringArray Session::getTakeNames (te::Clip& clip) const
    {
        juce::StringArray names;
        const auto n = getNumTakes (clip);

        for (int i = 0; i < n; ++i)
            names.add ("Take " + juce::String (i + 1));

        return names;
    }

    bool Session::setClipTake (te::Clip& clip, int takeIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (isRecording())
            return fail ("Stop recording first");

        const auto n = getNumTakes (clip);

        if (n == 0)
            return fail ("This clip has only one take (record over a loop to make takes)");

        if (! juce::isPositiveAndBelow (takeIndex, n))
            return fail ("No such take");

        if (takeIndex == clip.getCurrentTake())
            return true;

        beginUndoStep();
        clip.setCurrentTake (takeIndex);
        notifyStateChanged();
        return true;
    }

    bool Session::stepSelectedTakes (int delta)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        int changed = 0, lastTake = 0, lastCount = 0;
        bool began = false;

        for (auto* clip : getSelectedClips())
        {
            const auto n = clip != nullptr ? getNumTakes (*clip) : 0;

            if (n < 2)
                continue;

            if (! began)
            {
                beginUndoStep();
                began = true;
            }

            lastTake = (clip->getCurrentTake() + delta + n) % n;
            lastCount = n;
            clip->setCurrentTake (lastTake);
            ++changed;
        }

        if (changed == 0)
            return fail ("Select a clip with takes (record over a loop to make them)");

        showHint ("Take " + juce::String (lastTake + 1) + " of " + juce::String (lastCount)
                  + (changed > 1 ? " (" + juce::String (changed) + " clips)" : juce::String()) + "  (T: next, Shift+T: back)");
        notifyStateChanged();
        return true;
    }

    bool Session::setDrumRoll (te::MidiClip& clip, int pitch, double stepStart, double stepBeats, int hits, int velocity)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumClip (clip))
            return false;

        if (! juce::isPositiveAndBelow (pitch, 128) || stepStart < -1.0e-9 || stepBeats <= 0.0 || hits < 1 || hits > 8)
            return fail ("A roll can't go there");

        if (stepStart >= getClipContentEndBeat (clip) - 1.0e-9)
            return fail ("That step is past the end of the clip. Make the clip longer first.");

        const auto existing = getNotesInRange (clip, pitch, stepStart, stepStart + stepBeats);
        auto v = velocity;

        if (! existing.isEmpty())
        {
            v = 0;

            for (auto* note : existing)
                v = juce::jmax (v, note->getVelocity());
        }

        // Where the first hit was (swing moves it), if there was one
        const auto first = existing.isEmpty() ? stepStart : existing.getFirst()->getStartBeat().inBeats();
        const auto start = juce::jlimit (stepStart, stepStart + stepBeats * 0.5, first);
        const auto each = (stepStart + stepBeats - start) / hits;

        beginUndoStep();
        auto* um = &edit->getUndoManager();

        for (auto* note : existing)
            clip.getSequence().removeNote (*note, um);

        for (int i = 0; i < hits; ++i)
            clip.getSequence().addNote (pitch, te::BeatPosition::fromBeats (start + i * each),
                                        te::BeatDuration::fromBeats (juce::jmax (minNoteBeats, each)),
                                        juce::jlimit (1, 127, v), 0, um);

        notifyStateChanged();
        return true;
    }

    bool Session::clearDrumRow (te::MidiClip& clip, int pitch, double fromBeat, double toBeat)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumClip (clip))
            return false;

        const auto notes = getNotesInRange (clip, pitch, fromBeat, toBeat);

        if (notes.isEmpty())
            return true;

        beginUndoStep();

        for (auto* n : notes)
            clip.getSequence().removeNote (*n, &edit->getUndoManager());

        return true;
    }

    bool Session::fillDrumRow (te::MidiClip& clip, int pitch, double fromBeat, double toBeat, double stepBeats, int every, int velocity)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumClip (clip))
            return false;

        if (! juce::isPositiveAndBelow (pitch, 128) || stepBeats <= 0.0 || every < 1 || fromBeat < 0.0 || toBeat <= fromBeat)
            return fail ("Can't fill that");

        bool started = false;

        for (int step = 0; ; step += every)
        {
            const auto start = fromBeat + step * stepBeats;

            if (start >= toBeat - 1.0e-9)
                break;

            if (! getNotesInRange (clip, pitch, start, start + stepBeats).isEmpty())
                continue;

            if (! std::exchange (started, true))
                beginUndoStep();

            clip.getSequence().addNote (pitch, te::BeatPosition::fromBeats (start), te::BeatDuration::fromBeats (juce::jmax (minNoteBeats, stepBeats)),
                                        juce::jlimit (1, 127, velocity), 0, &edit->getUndoManager());
        }

        return true;
    }

    bool Session::shiftDrumRow (te::MidiClip& clip, int pitch, double fromBeat, double toBeat, double deltaBeats)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumClip (clip))
            return false;

        const auto span = toBeat - fromBeat;

        if (span <= 0.0 || fromBeat < 0.0)
            return fail ("Can't shift that");

        const auto notes = getNotesInRange (clip, pitch, fromBeat, toBeat);

        if (notes.isEmpty() || std::abs (deltaBeats) < 1.0e-9)
            return true;

        beginUndoStep();

        for (auto* n : notes)
        {
            auto start = std::fmod (n->getStartBeat().inBeats() - fromBeat + deltaBeats, span);

            if (start < 0.0)
                start += span;

            if (start >= span - 1.0e-9)
                start = 0.0;

            n->setStartAndLength (te::BeatPosition::fromBeats (fromBeat + start), n->getLengthBeats(), &edit->getUndoManager());
        }

        return true;
    }

    std::vector<Session::DrumHit> Session::getDrumHits (te::MidiClip& clip, double fromBeat, double toBeat) const
    {
        std::vector<DrumHit> hits;

        for (auto* n : getNotesInRange (clip, -1, fromBeat, toBeat))
            hits.push_back ({ n->getNoteNumber(), n->getStartBeat().inBeats() - fromBeat, n->getLengthBeats().inBeats(), n->getVelocity() });

        return hits;
    }

    bool Session::replaceDrumRange (te::MidiClip& clip, double fromBeat, double toBeat, const std::vector<DrumHit>& hits)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumClip (clip))
            return false;

        if (fromBeat < 0.0 || toBeat <= fromBeat)
            return fail ("Can't put a pattern there");

        const auto replaced = getNotesInRange (clip, -1, fromBeat, toBeat);

        if (hits.empty() && replaced.isEmpty())
            return true;

        beginUndoStep();
        auto* um = &edit->getUndoManager();

        for (auto* n : replaced)
            clip.getSequence().removeNote (*n, um);

        for (auto& h : hits)
            if (h.offset >= -1.0e-9 && fromBeat + h.offset < toBeat - 1.0e-9 && juce::isPositiveAndBelow (h.pitch, 128))
                clip.getSequence().addNote (h.pitch, te::BeatPosition::fromBeats (fromBeat + juce::jmax (0.0, h.offset)),
                                            te::BeatDuration::fromBeats (juce::jmax (minNoteBeats, h.length)), juce::jlimit (1, 127, h.velocity), 0, um);

        return true;
    }

    bool Session::copyDrumPattern (te::MidiClip& clip, double fromBeat, double toBeat, double destBeat)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumClip (clip))
            return false;

        const auto span = toBeat - fromBeat;

        if (span <= 0.0 || fromBeat < 0.0 || destBeat < 0.0)
            return fail ("Can't copy that");

        if (std::abs (destBeat - fromBeat) < 1.0e-9)
            return true;

        if (destBeat < toBeat - 1.0e-9 && destBeat + span > fromBeat + 1.0e-9)
            return fail ("A pattern can't be copied over itself");

        return replaceDrumRange (clip, destBeat, destBeat + span, getDrumHits (clip, fromBeat, toBeat));
    }

    bool Session::repeatDrumPatternToEnd (te::MidiClip& clip, double fromBeat, double toBeat)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! checkDrumClip (clip))
            return false;

        const auto span = toBeat - fromBeat;
        const auto end = getClipContentEndBeat (clip);

        if (span <= 0.0 || fromBeat < 0.0)
            return fail ("Can't repeat that");

        if (toBeat >= end - 1.0e-9)
            return fail ("The pattern already reaches the end of the clip. Make the clip longer first.");

        const auto hits = getDrumHits (clip, fromBeat, toBeat);
        beginUndoStep();
        auto* um = &edit->getUndoManager();

        for (auto* n : getNotesInRange (clip, -1, toBeat, end))
            clip.getSequence().removeNote (*n, um);

        for (auto at = toBeat; at < end - 1.0e-9; at += span)
            for (auto& h : hits)
                if (at + h.offset < end - 1.0e-9)
                    clip.getSequence().addNote (h.pitch, te::BeatPosition::fromBeats (at + h.offset), te::BeatDuration::fromBeats (h.length), h.velocity, 0, um);

        return true;
    }

    void Session::auditionNote (te::MidiClip& clip, int pitch, int velocity, bool autoRelease)
    {
        if (auto* track = dynamic_cast<te::AudioTrack*> (clip.getClipTrack()))
            if (! isRecording())
                track->playGuideNote (juce::jlimit (0, 127, pitch), te::MidiChannel (1), juce::jlimit (1, 127, velocity), true, true, autoRelease);
    }

    void Session::stopAudition (te::MidiClip& clip)
    {
        if (auto* track = dynamic_cast<te::AudioTrack*> (clip.getClipTrack()))
            track->turnOffGuideNotes();
    }

    void Session::valueTreeChildOrderChanged (juce::ValueTree& parent, int oldIndex, int newIndex)
    {
        if (edit == nullptr || ! parent.hasType (te::IDs::TRACK))
            return;

        const auto moved = parent.getChild (newIndex), other = parent.getChild (oldIndex);

        if (moved.hasType (te::IDs::PLUGIN) || other.hasType (te::IDs::PLUGIN))
        {
            ++playbackRebuildsForReorder;
            edit->restartPlayback();   // rebuild the audio graph in the new order
        }
    }

    //==============================================================================
    // Track controls
    void Session::beginUndoStep()
    {
        if (edit != nullptr)
            edit->getUndoManager().beginNewTransaction();
    }

    bool Session::renameTrack (int trackIndex, const juce::String& newName)
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr || newName.trim().isEmpty())
            return fail ("A track needs a name");

        edit->getUndoManager().beginNewTransaction();
        track->setName (newName.trim());

        syncBusRouting();   // a return's name is also what Tracktion calls its sends
        return true;
    }

    bool Session::isTrackMuted (int i) const        { auto* t = getTrack (i); return t != nullptr && t->isMuted (false); }
    bool Session::isTrackSoloed (int i) const       { auto* t = getTrack (i); return t != nullptr && t->isSolo (false); }

    void Session::setTrackMuted (int i, bool shouldBeMuted)
    {
        if (auto* t = getTrack (i))
        {
            // Tracktion's setMute() bypasses undo, so Ctrl+Z after muting would undo the PREVIOUS
            // edit instead. Set it through the undo manager so Undo does what the user expects.
            beginUndoStep();
            t->state.setProperty (te::IDs::mute, shouldBeMuted, &edit->getUndoManager());
            notifyStateChanged();
        }
    }

    void Session::setTrackSoloed (int i, bool shouldBeSoloed)
    {
        if (auto* t = getTrack (i))
        {
            beginUndoStep();
            t->state.setProperty (te::IDs::solo, shouldBeSoloed, &edit->getUndoManager());   // undoable, as mute
            notifyStateChanged();
        }
    }

    bool Session::isTrackArmed (int i) const
    {
        if (auto* t = getTrack (i))
            for (auto* input : edit->getAllInputDevices())
                if (input->getTargets().contains (t->itemID) && input->isRecordingEnabled (t->itemID))
                    return true;

        return false;
    }

    bool Session::isTrackFrozen (int i) const
    {
        auto* t = getTrack (i);
        return t != nullptr && t->isFrozen (te::Track::individualFreeze);
    }

    bool Session::setTrackFrozen (int i, bool shouldBeFrozen)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* t = getTrack (i);

        if (t == nullptr)
            return fail ("No such track");

        if (isRecording())
            return fail ("Stop recording before freezing or unfreezing a track");

        if (shouldBeFrozen == t->isFrozen (te::Track::individualFreeze))
            return true;

        if (shouldBeFrozen && kindOf (*t) != TrackKind::normal)
            return fail ("Buses and FX returns can't be frozen: freeze the tracks that play into them");

        if (shouldBeFrozen && t->getClips().isEmpty())
            return fail ("\"" + t->getName() + "\" has nothing to freeze yet: it has no clips");

        edit->getTransport().stop (false, false);   // the render needs the engine to itself
        beginUndoStep();   // keeps the freeze point Tracktion inserts out of the previous action's undo step

        if (const auto result = t->setFrozen (shouldBeFrozen, te::Track::individualFreeze); result.failed())
            return fail (result.getErrorMessage());

        ez::log (juce::String (shouldBeFrozen ? "Froze" : "Unfroze") + " track \"" + t->getName() + "\"");
        notifyStateChanged();
        return true;
    }

    bool Session::setTrackArmed (int i, bool shouldBeArmed)
    {
        auto* t = getTrack (i);

        if (t == nullptr)
            return fail ("No such track");

        if (isRecording())
            return fail ("Stop recording before arming or disarming tracks");

        if (shouldBeArmed && kindOf (*t) != TrackKind::normal)
            return fail ("Buses and FX returns don't record: they play what other tracks send them");

        bool hasInput = false;

        for (auto* input : edit->getAllInputDevices())
        {
            if (input->getTargets().contains (t->itemID))
            {
                input->setRecordingEnabled (t->itemID, shouldBeArmed);
                hasInput = true;
            }
        }

        if (! hasInput && shouldBeArmed)
            return fail ("Pick an input for \"" + t->getName() + "\" first (in Routing)");

        notifyStateChanged();
        return true;
    }

    // Read volume and pan from the song itself, not from the plugin's parameter: after an undo the
    // parameter only catches up asynchronously, and a view (or test) reading it at once sees the old value
    float Session::getTrackVolumeDb (int i) const
    {
        if (auto* t = getTrack (i))
            if (auto* volume = t->getVolumePlugin())
            {
                // Automated: what the lane says now. Otherwise the stored value (the parameter catches up later)
                if (volume->volParam->hasAutomationPoints() && isAutomationReadOn())
                    return te::volumeFaderPositionToDB (volume->volParam->getCurrentValue());

                return te::volumeFaderPositionToDB (volume->volume.get());
            }

        return 0.0f;
    }

    bool Session::setTrackVolumeDb (int i, float db)
    {
        if (auto* t = getTrack (i))
            if (auto* volume = t->getVolumePlugin())
            {
                if (refuseIfAutomated (volume->volParam.get(), "Volume"))
                    return false;

                volume->setVolumeDb (juce::jlimit (minVolumeDb, maxVolumeDb, db));
                return true;
            }

        return false;
    }

    float Session::getTrackPan (int i) const
    {
        if (auto* t = getTrack (i))
            if (auto* volume = t->getVolumePlugin())
                return volume->panParam->hasAutomationPoints() && isAutomationReadOn() ? volume->panParam->getCurrentValue() : volume->pan.get();

        return 0.0f;
    }

    bool Session::setTrackPan (int i, float pan)
    {
        if (auto* t = getTrack (i))
            if (auto* volume = t->getVolumePlugin())
            {
                if (refuseIfAutomated (volume->panParam.get(), "Pan"))
                    return false;

                volume->setPan (juce::jlimit (-1.0f, 1.0f, pan));
                return true;
            }

        return false;
    }

    juce::Array<te::EditItemID> Session::getTrackIDs() const
    {
        juce::Array<te::EditItemID> ids;

        if (edit != nullptr)
            for (auto* t : te::getAudioTracks (*edit))
                ids.add (t->itemID);

        return ids;
    }

    float Session::getMasterVolumeDb() const
    {
        return edit != nullptr && edit->getMasterVolumePlugin() != nullptr ? te::volumeFaderPositionToDB (edit->getMasterVolumePlugin()->volume.get()) : 0.0f;
    }

    void Session::setMasterVolumeDb (float db)
    {
        if (edit != nullptr)
            if (auto master = edit->getMasterVolumePlugin())
                master->setVolumeDb (juce::jlimit (minVolumeDb, maxVolumeDb, db));
    }

    float Session::getMasterPan() const
    {
        return edit != nullptr && edit->getMasterVolumePlugin() != nullptr ? edit->getMasterVolumePlugin()->pan.get() : 0.0f;
    }

    void Session::setMasterPan (float pan)
    {
        if (edit != nullptr)
            if (auto master = edit->getMasterVolumePlugin())
                master->setPan (juce::jlimit (-1.0f, 1.0f, pan));
    }

    //==============================================================================
    // Timeline editing
    te::TimePosition Session::getPosition() const
    {
        return edit != nullptr ? edit->getTransport().getPosition() : te::TimePosition();
    }

    void Session::setPosition (te::TimePosition position)
    {
        if (edit != nullptr)
            edit->getTransport().setPosition (juce::jmax (te::TimePosition(), position));
    }

    te::TimePosition Session::snapToGrid (te::TimePosition time, double gridBeats) const
    {
        if (edit == nullptr || gridBeats <= 0.0)
            return time;

        const auto beats = edit->tempoSequence.toBeats (time).inBeats();
        const auto snapped = std::round (beats / gridBeats) * gridBeats;
        return edit->tempoSequence.toTime (te::BeatPosition::fromBeats (juce::jmax (0.0, snapped)));
    }

    bool Session::moveClip (te::Clip& clip, te::TimePosition newStart, int targetTrackIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (isRecording())
            return fail ("Stop recording before moving clips");

        if (clip.getClipTrack() == nullptr)
            return fail ("That clip has been deleted");

        if (newStart < te::TimePosition())
            return fail ("A clip can't start before the beginning of the song");

        auto* target = getTrack (targetTrackIndex);

        if (target == nullptr)
            return fail ("No such track");

        edit->getUndoManager().beginNewTransaction();

        if (clip.getClipTrack() != target && ! clip.moveTo (*target))
            return fail ("This clip can't go on that track");

        clip.setStart (newStart, false, true);
        return true;
    }

    //==============================================================================
    // Selection
    void Session::selectClip (te::Clip* clip, bool addToSelection)
    {
        if (clip == nullptr)
            selectionManager.deselectAll();
        else if (addToSelection)
            selectionManager.select (clip, true);
        else
            selectionManager.selectOnly (clip);

        notifyStateChanged();
    }

    void Session::toggleClipSelected (te::Clip& clip)
    {
        if (isClipSelected (clip))
            selectionManager.deselect (&clip);
        else
            selectionManager.select (&clip, true);

        notifyStateChanged();
    }

    void Session::selectClips (const juce::Array<te::Clip*>& clips)
    {
        selectionManager.deselectAll();

        for (auto* c : clips)
            selectionManager.select (c, true);

        notifyStateChanged();
    }

    void Session::selectAllClips()
    {
        juce::Array<te::Clip*> all;

        if (edit != nullptr)
            for (auto* track : te::getAudioTracks (*edit))
                all.addArray (track->getClips());

        selectClips (all);
    }

    te::Clip* Session::getSelectedClip() const
    {
        return getSelectedClips().getFirst();
    }

    juce::Array<te::Clip*> Session::getSelectedClips() const
    {
        juce::Array<te::Clip*> clips;

        for (auto* c : selectionManager.getItemsOfType<te::Clip>())
            if (c->getClipTrack() != nullptr)   // ignore deleted clips
                clips.add (c);

        return clips;
    }

    bool Session::isClipSelected (const te::Clip& clip) const
    {
        return selectionManager.isSelected (const_cast<te::Clip&> (clip));
    }

    //==============================================================================
    // Editing clips
    bool Session::refuseWhileRecording (const juce::String& action)
    {
        return isRecording() && ! fail ("Stop recording before " + action);
    }

    int Session::getTrackIndex (const te::Track* track) const
    {
        if (edit == nullptr || track == nullptr)
            return -1;

        return te::getAudioTracks (*edit).indexOf (dynamic_cast<te::AudioTrack*> (const_cast<te::Track*> (track)));
    }

    bool Session::deleteSelectedClips()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto clips = getSelectedClips();

        if (clips.isEmpty())
            return fail ("Nothing selected");

        if (refuseWhileRecording ("deleting clips"))
            return false;

        beginUndoStep();
        selectionManager.deselectAll();

        for (auto* clip : clips)
            clip->removeFromParent();

        notifyStateChanged();
        return true;
    }

    bool Session::moveSelectedClips (te::TimeDuration delta, int trackDelta)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto clips = getSelectedClips();

        if (clips.isEmpty())
            return fail ("Nothing selected");

        if (refuseWhileRecording ("moving clips"))
            return false;

        if (! canShiftClips (clips, delta, trackDelta))
            return false;

        beginUndoStep();

        for (auto* clip : clips)
        {
            auto* target = getTrack (getTrackIndex (clip->getClipTrack()) + trackDelta);

            if (clip->getClipTrack() != target)
                clip->moveTo (*target);

            clip->setStart (clip->getPosition().getStart() + delta, false, true);
        }

        return true;
    }

    bool Session::canShiftClips (const juce::Array<te::Clip*>& clips, te::TimeDuration delta, int trackDelta)
    {
        // Checked for every clip first, so a move either all happens or none of it does
        for (auto* clip : clips)
        {
            if (clip->getPosition().getStart() + delta < te::TimePosition())
                return fail ("A clip can't start before the beginning of the song");

            if (getTrack (getTrackIndex (clip->getClipTrack()) + trackDelta) == nullptr)
                return fail ("There's no track to move to");
        }

        return true;
    }

    bool Session::copySelectedClipsBy (te::TimeDuration delta, int trackDelta)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto clips = getSelectedClips();

        if (clips.isEmpty())
            return fail ("Nothing selected");

        if (refuseWhileRecording ("copying clips") || ! canShiftClips (clips, delta, trackDelta))
            return false;

        beginUndoStep();
        juce::Array<te::Clip*> copies;

        for (auto* clip : clips)
            if (auto* target = getTrack (getTrackIndex (clip->getClipTrack()) + trackDelta))
                if (auto* copy = placeClipCopy (*clip, clip->getPosition().getStart() + delta, *target))
                    copies.add (copy);

        if (copies.isEmpty())
            return fail ("Could not copy");

        selectClips (copies);
        return true;
    }

    te::Clip* Session::placeClipCopy (const te::Clip& source, te::TimePosition start, te::AudioTrack& target)
    {
        auto copy = te::duplicateClip (source);

        if (copy == nullptr)
            return nullptr;

        if (copy->getClipTrack() != &target && ! copy->moveTo (target))
        {
            copy->removeFromParent();
            return nullptr;
        }

        copy->setStart (start, false, true);
        return copy.get();
    }

    bool Session::copyClipTo (te::Clip& clip, te::TimePosition newStart, int targetTrackIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* target = getTrack (targetTrackIndex);

        if (target == nullptr || clip.getClipTrack() == nullptr)
            return fail ("Can't copy there");

        if (newStart < te::TimePosition())
            return fail ("A clip can't start before the beginning of the song");

        if (refuseWhileRecording ("copying clips"))
            return false;

        beginUndoStep();

        if (auto* copy = placeClipCopy (clip, newStart, *target))
        {
            selectClip (copy);
            return true;
        }

        return fail ("Could not copy the clip");
    }

    bool Session::copySelectedClips()
    {
        const auto clips = getSelectedClips();

        if (clips.isEmpty())
            return fail ("Nothing selected to copy");

        // Each clip is stored relative to the earliest start and the top-most track in the selection
        auto earliest = clips.getFirst()->getPosition().getStart();
        int topTrack = getTrackIndex (clips.getFirst()->getClipTrack());

        for (auto* c : clips)
        {
            earliest = juce::jmin (earliest, c->getPosition().getStart());
            topTrack = juce::jmin (topTrack, getTrackIndex (c->getClipTrack()));
        }

        auto content = std::make_unique<te::Clipboard::Clips>();

        for (auto* c : clips)
        {
            auto state = c->state.createCopy();
            state.setProperty (te::IDs::start, (c->getPosition().getStart() - earliest).inSeconds(), nullptr);
            content->addClip (getTrackIndex (c->getClipTrack()) - topTrack, state);
        }

        clipboard = std::move (content);
        return true;
    }

    bool Session::cutSelectedClips()
    {
        if (refuseWhileRecording ("cutting clips"))
            return false;

        return copySelectedClips() && deleteSelectedClips();
    }

    bool Session::pasteClips (te::TimePosition at, int trackIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (clipboard == nullptr)
            return fail ("Nothing to paste: copy some clips first");

        if (getTrack (trackIndex) == nullptr)
            return fail ("No track to paste on");

        if (refuseWhileRecording ("pasting"))
            return false;

        // Placed by hand (not Clipboard::pasteIntoEdit) so where things land is exact and testable
        beginUndoStep();
        juce::Array<te::Clip*> pasted;
        const auto tracks = te::getAudioTracks (*edit);

        for (auto& info : clipboard->clips)
        {
            auto* destination = tracks[juce::jmin (trackIndex + info.trackOffset, tracks.size() - 1)];
            auto state = info.state.createCopy();
            te::EditItemID::remapIDs (state, nullptr, *edit);   // a paste is a new clip, with new IDs
            const auto start = at + te::TimeDuration::fromSeconds ((double) state.getProperty (te::IDs::start));
            state.setProperty (te::IDs::start, juce::jmax (0.0, start.inSeconds()), nullptr);

            if (auto* clip = te::insertClipCopy (*destination, te::ClipCopy::fromClipboardState (state, false)))
                pasted.add (clip);
        }

        if (pasted.isEmpty())
            return fail ("Nothing could be pasted");

        selectClips (pasted);
        return true;
    }

    bool Session::duplicateSelectedClips()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto clips = getSelectedClips();

        if (clips.isEmpty())
            return fail ("Nothing selected to duplicate");

        if (refuseWhileRecording ("duplicating clips"))
            return false;

        // The whole selection repeats once, straight after itself, so patterns stay in time
        auto start = clips.getFirst()->getPosition().getStart();
        auto end = clips.getFirst()->getPosition().getEnd();

        for (auto* c : clips)
        {
            start = juce::jmin (start, c->getPosition().getStart());
            end = juce::jmax (end, c->getPosition().getEnd());
        }

        const auto shift = end - start;
        beginUndoStep();
        juce::Array<te::Clip*> copies;

        for (auto* c : clips)
            if (auto* track = dynamic_cast<te::AudioTrack*> (c->getClipTrack()))
                if (auto* copy = placeClipCopy (*c, c->getPosition().getStart() + shift, *track))
                    copies.add (copy);

        if (copies.isEmpty())
            return fail ("Could not duplicate");

        selectClips (copies);
        return true;
    }

    bool Session::splitClipsAt (te::TimePosition time)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (refuseWhileRecording ("splitting clips"))
            return false;

        // Tracktion only splits strictly inside a clip (not within 1 ms of an edge)
        const auto isInside = [time] (te::Clip& c) { return c.getPosition().time.reduced (te::TimeDuration::fromSeconds (0.001)).contains (time); };
        const auto selected = getSelectedClips();
        te::SelectableList toSplit;

        for (auto* c : selected)
            if (isInside (*c))
                toSplit.add (c);

        if (selected.isEmpty())   // nothing selected: every clip under the time
            for (auto* track : te::getAudioTracks (*edit))
                for (auto* c : track->getClips())
                    if (isInside (*c))
                        toSplit.add (c);

        if (toSplit.isEmpty())
            return fail ("There's no clip at that point to split");

        beginUndoStep();
        const auto newClips = te::splitClips (toSplit, time);

        if (newClips.isEmpty())
            return fail ("There's no clip at that point to split");

        selectionManager.deselectAll();
        notifyStateChanged();
        return true;
    }

    bool Session::trimClipStart (te::Clip& clip, te::TimePosition newStart)
    {
        if (clip.getClipTrack() == nullptr)
            return fail ("That clip has been deleted");

        if (refuseWhileRecording ("trimming clips"))
            return false;

        const auto position = clip.getPosition();
        // Can't reveal audio from before the file starts, or make the clip vanishingly short
        const auto earliest = juce::jmax (te::TimePosition(), position.getStart() - position.getOffset());
        const auto latest = position.getEnd() - te::TimeDuration::fromSeconds (minClipSeconds);
        newStart = juce::jlimit (earliest, juce::jmax (earliest, latest), newStart);

        beginUndoStep();
        clip.setStart (newStart, true, false);   // preserve sync: the content stays where it was
        return true;
    }

    bool Session::trimClipEnd (te::Clip& clip, te::TimePosition newEnd)
    {
        if (clip.getClipTrack() == nullptr)
            return fail ("That clip has been deleted");

        if (refuseWhileRecording ("trimming clips"))
            return false;

        const auto position = clip.getPosition();
        newEnd = juce::jmax (newEnd, position.getStart() + te::TimeDuration::fromSeconds (minClipSeconds));

        // Audio can't play past the end of its file (unless the clip loops)
        if (auto* audio = dynamic_cast<te::AudioClipBase*> (&clip); audio != nullptr && ! audio->isLooping() && ! audio->getAutoTempo())
        {
            const auto fileLength = te::AudioFile (clip.edit.engine, audio->getOriginalFile()).getLength();

            if (fileLength > 0.0 && audio->getSpeedRatio() > 0.0)
                newEnd = juce::jmin (newEnd, position.getStart() - position.getOffset()
                                                 + te::TimeDuration::fromSeconds (fileLength / audio->getSpeedRatio()));
        }

        beginUndoStep();
        clip.setEnd (newEnd, true);
        return true;
    }

    bool Session::setClipMuted (te::Clip& clip, bool shouldBeMuted)
    {
        if (clip.getClipTrack() == nullptr)
            return fail ("That clip has been deleted");

        beginUndoStep();
        clip.setMuted (shouldBeMuted);
        notifyStateChanged();
        return true;
    }

    bool Session::renameClip (te::Clip& clip, const juce::String& newName)
    {
        if (newName.trim().isEmpty())
            return fail ("A clip needs a name");

        beginUndoStep();
        clip.setName (newName.trim());
        return true;
    }

    bool Session::setClipColour (te::Clip& clip, juce::Colour colour)
    {
        beginUndoStep();
        clip.setColour (colour);
        // Tracktion writes a colour on every new clip, so mark the ones the user picked (the clip look uses it)
        if (colour.isTransparent())
            clip.state.removeProperty (userColourProp, &edit->getUndoManager());
        else
            clip.state.setProperty (userColourProp, true, &edit->getUndoManager());
        return true;
    }

    bool Session::createMidiClip (int trackIndex, te::TimePosition start, te::TimeDuration length, const juce::String& name)
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return fail ("No such track");

        if (length <= te::TimeDuration())
            return fail ("A clip needs a length");

        if (refuseWhileRecording ("adding clips"))
            return false;

        beginUndoStep();
        auto clip = track->insertMIDIClip ({ juce::jmax (te::TimePosition(), start), length }, nullptr);

        if (clip == nullptr)
            return fail ("Could not create a MIDI clip");

        if (name.isNotEmpty())
            clip->setName (name);   // same undo step as the clip

        selectClip (clip.get());
        return true;
    }

    te::TimeDuration Session::getSongLength() const
    {
        return edit != nullptr ? edit->getLength() : te::TimeDuration();
    }

    //==============================================================================
    // Loop
    void Session::setLoopRange (te::TimePosition start, te::TimePosition end)
    {
        if (edit == nullptr)
            return;

        if (end < start)
            std::swap (start, end);

        edit->getTransport().setLoopRange ({ juce::jmax (te::TimePosition(), start), end });
        notifyStateChanged();
    }

    te::TimeRange Session::getLoopRange() const
    {
        return edit != nullptr ? edit->getTransport().getLoopRange() : te::TimeRange();
    }

    bool Session::isLooping() const     { return edit != nullptr && edit->getTransport().looping.get(); }

    bool Session::setLooping (bool shouldLoop)
    {
        if (edit == nullptr)
            return fail ("No song is open");

        if (isRecording())
            return fail ("Stop recording before turning the loop on or off");

        edit->getTransport().looping = shouldLoop;
        notifyStateChanged();
        return true;
    }

    //==============================================================================
    juce::String Session::getTrackName (int trackIndex) const
    {
        if (edit != nullptr)
            if (auto* track = te::getAudioTracks (*edit)[trackIndex])
                return track->getName();

        return {};
    }

    // getTrackOutputChoices, getTrackOutput and setTrackOutput: SessionMix.cpp (buses are outputs)

    juce::Array<Session::RoutingChoice> Session::getTrackInputChoices() const
    {
        juce::Array<RoutingChoice> choices;
        choices.add ({ {}, "No input" });

        if (edit != nullptr)
            for (auto* input : edit->getAllInputDevices())
            {
                const auto type = input->getInputDevice().getDeviceType();

                if (type == te::InputDevice::waveDevice || type == te::InputDevice::physicalMidiDevice
                     || type == te::InputDevice::virtualMidiDevice)
                {
                    const auto& device = input->getInputDevice();
                    choices.add ({ device.getDeviceID(), (device.isMidi() ? "MIDI: " : "Audio: ") + device.getAliasOrName() });
                }
            }

        return choices;
    }

    juce::String Session::getTrackInput (int trackIndex) const
    {
        if (edit != nullptr)
            if (auto* track = te::getAudioTracks (*edit)[trackIndex])
                for (auto* input : edit->getAllInputDevices())
                    if (input->getTargets().contains (track->itemID))
                        return input->getInputDevice().getDeviceID();

        return {};
    }

    bool Session::setTrackInput (int trackIndex, const juce::String& choiceID)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* track = edit != nullptr ? te::getAudioTracks (*edit)[trackIndex] : nullptr;

        if (track == nullptr)
            return fail ("No such track");

        if (isRecording())
            return fail ("Stop recording before changing inputs");

        if (choiceID.isNotEmpty() && kindOf (*track) != TrackKind::normal)
            return fail ("Buses and FX returns don't record: they play what other tracks send them");

        te::InputDeviceInstance* newInput = nullptr;

        if (choiceID.isNotEmpty())
        {
            for (auto* input : edit->getAllInputDevices())
                if (input->getInputDevice().getDeviceID() == choiceID)   // IDs: an audio and a MIDI input can share a name
                    newInput = input;

            if (newInput == nullptr)
                return fail ("That input isn't available (is it enabled in Audio Setup?)");
        }

        auto& um = edit->getUndoManager();
        um.beginNewTransaction();

        bool wasArmed = false;

        for (auto* input : edit->getAllInputDevices())
            if (input->getTargets().contains (track->itemID))
                wasArmed = wasArmed || input->isRecordingEnabled (track->itemID);

        // Connect the new input first, so a failure leaves the track as it was
        if (newInput != nullptr && ! newInput->getTargets().contains (track->itemID))
            if (auto result = newInput->setTarget (track->itemID, false, &um, 0); ! result)
                return fail ("Could not connect the input: " + result.error());

        for (auto* input : edit->getAllInputDevices())
            if (input != newInput && input->getTargets().contains (track->itemID))
                if (auto result = input->removeTarget (track->itemID, &um); result.failed())
                    log ("Could not disconnect input: " + result.getErrorMessage());

        if (newInput != nullptr && wasArmed)
            newInput->setRecordingEnabled (track->itemID, true);   // keep the track armed

        edit->restartPlayback();
        return true;
    }

    void Session::routeMidiInputsTo (te::AudioTrack& track)
    {
        edit->getTransport().ensureContextAllocated();

        for (auto* input : edit->getAllInputDevices())
        {
            const auto type = input->getInputDevice().getDeviceType();

            if (type != te::InputDevice::physicalMidiDevice && type != te::InputDevice::virtualMidiDevice)
                continue;

            // true = move: the newest instrument track is the one your keyboard plays
            if (auto result = input->setTarget (track.itemID, true, &edit->getUndoManager(), 0); ! result)
                log ("Could not connect MIDI input: " + result.error());
            else
                input->setRecordingEnabled (track.itemID, true);
        }

        edit->restartPlayback();
    }

    void Session::undo()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (refuseWhileRecording ("undoing"))   // it could take away the track or clip being recorded into
        {
            showHint (lastError);
            return;
        }

        if (edit != nullptr)
        {
            edit->getUndoManager().undo();
            syncBusRouting();

            // Tracktion's undo removes a freeze's freeze point but keeps the track frozen: undoing a freeze unfreezes
            for (auto* track : te::getAudioTracks (*edit))
            {
                bool hasFreezePoint = false;

                for (auto* plugin : track->pluginList)
                    hasFreezePoint = hasFreezePoint || dynamic_cast<te::FreezePointPlugin*> (plugin) != nullptr;

                if (track->isFrozen (te::Track::individualFreeze) && ! hasFreezePoint)
                    if (const auto r = track->setFrozen (false, te::Track::individualFreeze); r.failed())
                        ez::log ("Could not unfreeze \"" + track->getName() + "\" after undo: " + r.getErrorMessage());
            }

            notifyStateChanged();   // the tempo, key or anything else may have changed back
        }
    }

    void Session::redo()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (refuseWhileRecording ("redoing"))
        {
            showHint (lastError);
            return;
        }

        if (edit != nullptr)
        {
            edit->getUndoManager().redo();
            syncBusRouting();
            notifyStateChanged();
        }
    }

    //==============================================================================
    void Session::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        const bool recordingNow = isRecording();

        // A take just finished (by any route: Stop, Record, Home, end of song): save it
        if (wasRecording && ! recordingNow)
            saveWhenStopped = true;

        wasRecording = recordingNow;

        if (saveWhenStopped && ! isPlaying() && ! recordingNow && ! isExportingNow())   // a stem export's mutes must not be saved
        {
            saveWhenStopped = false;
            writeSongNow();
        }

        notifyStateChanged();
    }

    void Session::timerCallback()
    {
        // An export moves along here too (the window polls it as well), and nothing is saved while it runs:
        // a stem export mutes the tracks that feed it for the duration
        if (exportJobs != nullptr || playbackNeedsRestoring)
            updateExport();

        updateMaster();

        if (isExportingNow())
            return;

        // A save that had to wait (playback, or the song was mid-operation) happens as soon as it safely can
        if (saveWhenStopped && ! isPlaying() && ! isRecording() && ! edit->isSaveInhibited())
        {
            saveWhenStopped = false;
            writeSongNow();
        }

        if (juce::Time::getCurrentTime() - lastAutosave >= juce::RelativeTime::milliseconds (autosaveIntervalMs))
            autosaveNow();
    }
}
