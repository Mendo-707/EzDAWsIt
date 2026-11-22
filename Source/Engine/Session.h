/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Session = the song that is open right now (a Tracktion Edit) plus every
    action the user can take on it. This is the app's command layer: the UI
    calls Session, never te::Edit directly (the arrangement view is the one
    exception, it draws the Edit).

    Safety rules this class guarantees (see Tests/CoreTests.cpp):
      - A song file is never overwritten until the replacement has been checked.
      - Autosaves and backups are renamed aside, never deleted.
      - If a save fails, an emergency copy is written and listeners are told.
      - Nothing is written to disk while recording; while playing only a
        background autosave is written.

    All methods must be called on the message thread.
*/

#pragma once

#include "../Core/Common.h"
#include <set>
#include "DrumMachinePlugin.h"
#include "Meters.h"

namespace ez
{
    class MasteringChainPlugin;
    class ReferencePlugin;
    class TunerPlugin;
    class MidiLearn;
    class LoudnessScan;

    class AudioEngine;

    class Session : private juce::ChangeListener,
                    private juce::ValueTree::Listener,
                    private juce::Timer
    {
    public:
        static constexpr int defaultTrackCount = 4;
        static constexpr int autosaveIntervalMs = 60 * 1000;
        static constexpr int autosaveCheckIntervalMs = 10 * 1000;
        static constexpr double minBpm = 20.0, maxBpm = 300.0;
        static constexpr const char* importedAudioFolderName = "Imported Audio";

        //==============================================================================
        struct Listener
        {
            virtual ~Listener() = default;

            /** The current song is about to be destroyed: drop every reference to it. */
            virtual void sessionSongClosing() {}

            /** A different song is now open (or none). */
            virtual void sessionSongChanged() {}

            /** Play/record state, tempo, click or count-in changed. */
            virtual void sessionStateChanged() {}

            /** Something went wrong that the user must know about (e.g. a save failed). */
            virtual void sessionProblem (const juce::String& /*message*/) {}
        };

        //==============================================================================
        explicit Session (AudioEngine&);
        ~Session() override;

        //==============================================================================
        // Songs
        bool createSong (const juce::String& name);

        /** Opens a song. If useAutosave is true, unsaved work from a crash is restored
            (the current file is kept as a timestamped .bak). Either way an autosave is
            renamed aside, never deleted. Nothing is changed on disk if the file is unreadable. */
        bool openSong (const juce::File& songFile, bool useAutosave = false);

        /** Saves now, or right after playback stops if the song is playing
            (writing the file during playback can cause audio dropouts). */
        bool saveSong();

        /** Saves the song as a new song (its own folder, with a copy of its recordings) and carries on in
            the copy. The original stays as it was last saved. */
        bool saveSongAs (const juce::String& newName);

        /** Something has changed since the song was last saved (cheap: checked when the song changes). */
        bool hasUnsavedChanges();

        /** Stops, saves and closes. Returns false if the save failed
            (an emergency copy is attempted and sessionProblem() is called). */
        bool closeSong();

        /** Writes the crash-recovery copy now (normally done by a timer every minute). */
        void autosaveNow();

        bool hasSong() const noexcept                       { return edit != nullptr; }
        juce::String getSongName() const;
        juce::File getSongFile() const;

        /** Human-readable reason for the last failed operation. */
        const juce::String& getLastError() const noexcept   { return lastError; }

        /** True if an autosave newer than the saved song exists (i.e. the app crashed). */
        static bool hasNewerAutosave (const juce::File& songFile);
        static juce::File getAutosaveFile (const juce::File& songFile);

        //==============================================================================
        // Transport
        void togglePlay();
        void toggleRecord();
        void returnToStart();

        bool isPlaying() const;
        bool isRecording() const;

        /** e.g. "3.2   0:04.512" (bar.beat and minutes:seconds) */
        juce::String getPositionText() const;
        juce::String getBarsBeatsText() const;          // "5.1.1": bar.beat.sixteenth
        juce::String getTimeText() const;               // "0:10.43"

        //==============================================================================
        // Song settings
        double getBpm() const;
        void setBpm (double);

        bool isClickEnabled() const;
        void setClickEnabled (bool);

        bool isCountInEnabled() const;
        void setCountInEnabled (bool);

        //==============================================================================
        // Tempo, time signature and key over the song. Positions are song beats (quarter notes
        // in 4/4; Tracktion's beats). Every change keeps clips on the beats they were on, is one
        // undo step, and is refused (changing nothing) if it can't be done. The first marker of
        // each kind is the song's start value: it can be changed, never moved or deleted.
        struct TempoMarker { double beat = 0.0, bpm = 120.0; bool ramp = false; };   // ramp: glides to the next marker
        struct TimeSigMarker { double beat = 0.0; int bar = 0, numerator = 4, denominator = 4; };
        struct KeyMarker { double beat = 0.0; int root = -1; bool minor = false; };   // root 0-11 (C..B), -1 = no key

        std::vector<TempoMarker> getTempoMarkers() const;
        double getBpmAt (te::TimePosition) const;     // follows ramps
        bool setBpmAt (te::TimePosition, double bpm, bool continuesGesture = false);   // the marker in effect there
        bool addTempoChange (double beat, double bpm);
        bool setTempoChangeBpm (int index, double bpm, bool continuesGesture = false);
        bool moveTempoChange (int index, double newBeat);
        bool setTempoRamp (int index, bool ramp);
        bool deleteTempoChange (int index);

        std::vector<TimeSigMarker> getTimeSigMarkers() const;
        std::pair<int, int> getTimeSigAt (te::TimePosition) const;
        double getBeatsPerBarAt (te::TimePosition) const;
        bool setTimeSigAt (te::TimePosition, int numerator, int denominator);
        bool addTimeSigChange (int bar, int numerator, int denominator);   // bar 0 = the first
        bool moveTimeSigChange (int index, int newBar);   // newBar as it reads after the move
        bool deleteTimeSigChange (int index);
        static bool isValidTimeSig (int numerator, int denominator);

        std::vector<KeyMarker> getKeyMarkers() const;
        KeyMarker getKeyAt (te::TimePosition) const;
        bool setKeyAt (te::TimePosition, int root, bool minor);   // root -1 = no key
        bool addKeyChange (double beat, int root, bool minor);
        bool moveKeyChange (int index, double newBeat);
        bool deleteKeyChange (int index);
        static juce::String describeKey (int root, bool minor);    // "G minor", "No key"

        //==============================================================================
        // Sections: named parts of the song (Intro, Verse, Hook...). They never overlap. Kept as
        // marker clips, so they move with the music when time is inserted or deleted.
        struct Section
        {
            te::EditItemID id;
            juce::String name;
            juce::Colour colour;
            double startBeat = 0.0, endBeat = 0.0;
        };

        static constexpr double minSectionBeats = 1.0;

        std::vector<Section> getSections() const;   // in song order
        std::optional<Section> getSection (te::EditItemID) const;

        /** Refused if it starts inside another section; the end is cut back to the next one. */
        bool addSection (double startBeat, double endBeat, const juce::String& name);
        bool renameSection (te::EditItemID, const juce::String& name);
        bool setSectionColour (te::EditItemID, juce::Colour);
        bool moveSection (te::EditItemID, double newStartBeat);                 // keeps its length; never onto another
        bool resizeSection (te::EditItemID, double newStartBeat, double newEndBeat);
        bool deleteSection (te::EditItemID);                                    // the music stays

        /** Inserts `times` copies of the section (its clips and its marker) straight after it; everything
            later moves along. Clips that start before the section and run into it are not copied. */
        bool repeatSection (te::EditItemID, int times);

        /** Removes the section's stretch of the song from every track and closes the gap. */
        bool deleteSectionAndMusic (te::EditItemID);

        /** Selects every clip that starts inside the section. */
        bool selectSectionClips (te::EditItemID);

        /** Grid lines between two beats: (beat, is a bar line). Fine grids step by gridBeats; once the grid is
            a bar or more it steps by whole bars, so bar lines stay right in any time signature. */
        std::vector<std::pair<double, bool>> getGridLines (double fromBeat, double toBeat, double gridBeats) const;

        /** How long one bar is at a time (for "one bar" defaults). */
        te::TimeDuration getBarLengthAt (te::TimePosition) const;

        /** Song beat of the start of a bar (bar 0 = the first), following time signature changes. */
        double getBarStartBeat (int bar) const;
        /** The bar a beat is in (0-based) and how far into it, in beats. */
        std::pair<int, double> getBarAt (double beat) const;

        //==============================================================================
        // Editing
        void addAudioTrack();
        int getNumAudioTracks() const;

        /** Copies the file into the song's "Imported Audio" folder (so the song never
            depends on files elsewhere), then puts it on the selected track (or track 1)
            at the playhead. */
        bool importAudioFile (const juce::File&);

        void undo();
        void redo();

        //==============================================================================
        // Instruments
        struct PluginChoice
        {
            juce::String name;
            juce::String pluginType;                 // a te::Plugin xmlTypeName
            juce::PluginDescription description;     // only for VST3/AU plugins
            juce::String category = {};              // effects: "Dynamics", "EQ & Filter"... (Effects::categories); VST3s: empty
        };

        using InstrumentChoice = PluginChoice;

        /** Built-in instruments first, then every scanned plugin instrument. */
        juce::Array<InstrumentChoice> getAvailableInstruments() const;

        /** Adds a track with this instrument, sends every MIDI input to it and arms it, so playing
            your keyboard/pads sounds straight away. Nothing is added if the instrument fails to load. */
        bool addInstrumentTrack (const InstrumentChoice&, bool openInstrumentWindow, int drumKitIndex = 0);   // kit: Drum Machine only

        /** Names of the plugins on audio track n (0-based), first to last. */
        juce::StringArray getPluginNamesOnTrack (int audioTrackIndex) const;

        //==============================================================================
        // Effects on a track (the track's own volume/pan and meter are not listed)
        juce::Array<PluginChoice> getAvailableEffects() const;

        juce::StringArray getEffectNames (int trackIndex) const;
        bool addEffect (int trackIndex, const PluginChoice&, bool openWindow);
        bool removeEffect (int trackIndex, int effectIndex);
        bool openEffectWindow (int trackIndex, int effectIndex);

        /** The track's tuner (the first, if it has two); nullptr if it has none. */
        TunerPlugin* getTuner (int trackIndex) const;
        /** The track's tuner, added first in its effects if it has none (so it hears the input before
            any amp or effect). One undo step when added; nullptr and lastError if it can't be. */
        TunerPlugin* ensureTuner (int trackIndex);
        bool openInstrumentWindow (int trackIndex);

        /** What the devices panel shows for one plugin. */
        struct DeviceInfo
        {
            juce::String name, maker;
            bool isBuiltIn = true, isEnabled = true;
            juce::String id;   // stays the same while the plugin exists: lets a view check it still means this one
            juce::String type = {};   // its te::Plugin xmlTypeName ("ezvocalchain" gets the rack card)
        };

        /** The track's effects, first to last (signal order). */
        juce::Array<DeviceInfo> getEffectInfo (int trackIndex) const;

        /** The track's instrument, if it has one. */
        std::optional<DeviceInfo> getInstrumentInfo (int trackIndex) const;

        /** The effect with this DeviceInfo id, for drawing its state (device previews). Read it, never keep it:
            look it up again each time (undo and removal replace plugins). Nullptr if it has gone. */
        te::Plugin* findEffect (int trackIndex, const juce::String& id) const;

        /** Bypass: a switched-off effect passes the sound through untouched. Undoable. */
        bool setEffectEnabled (int trackIndex, int effectIndex, bool);

        /** Moves an effect to another place in the chain (both indexes among the effects). Undoable. */
        bool moveEffect (int trackIndex, int fromIndex, int toIndex);

        /** How many times playback was rebuilt because effects were reordered (for tests). */
        int getPlaybackRebuildsForReorder() const noexcept  { return playbackRebuildsForReorder; }

        /** Sends a change message whenever the scanned plugin list changes (after a scan). */
        juce::ChangeBroadcaster& getPluginListBroadcaster();

        //==============================================================================
        // Track controls (0-based audio track indexes)
        static constexpr float minVolumeDb = -60.0f, maxVolumeDb = 6.0f;
        static constexpr double minClipSeconds = 0.01;

        /** Starts a new undo step: call when a continuous gesture (e.g. a fader drag) begins. */
        void beginUndoStep();

        bool renameTrack (int trackIndex, const juce::String& newName);
        bool isTrackMuted (int trackIndex) const;
        void setTrackMuted (int trackIndex, bool);
        bool isTrackSoloed (int trackIndex) const;
        void setTrackSoloed (int trackIndex, bool);
        bool isTrackArmed (int trackIndex) const;

        /** Freeze: the track is rendered to audio once and its effects and instrument stop running (saves CPU).
            Unfreeze (the same command) to edit it again. Not an undo step, as in other DAWs; refuses while
            recording, for buses, and for empty tracks. */
        bool setTrackFrozen (int trackIndex, bool);

        /** Bounce in place: renders the track (instrument, effects, fader; not the master chain) from its first
            clip to its last plus a 2 s tail, puts the audio on a new track right below it, and mutes the
            original (kept, so it can be edited and bounced again). One undo step. The audio goes in the song's
            "Bounces" folder. Returns false, with a reason, for buses, empty or frozen tracks, or while recording. */
        bool bounceTrackInPlace (int trackIndex);
        bool trackHasClips (int trackIndex) const;   // something to freeze or bounce
        static constexpr const char* bouncesFolderName = "Bounces";
        bool isTrackFrozen (int trackIndex) const;
        bool setTrackArmed (int trackIndex, bool);     // fails if the track has no input
        /** Volume and pan. When automated (and automation is on) the getters give what the lane says now,
            and the setters refuse with a hint, unless Write is on and the song is playing (that records). */
        float getTrackVolumeDb (int trackIndex) const;
        bool setTrackVolumeDb (int trackIndex, float);
        float getTrackPan (int trackIndex) const;       // -1 (left) .. +1 (right)
        bool setTrackPan (int trackIndex, float);

        /** The audio tracks' IDs in order (views compare it to know when to rebuild). */
        juce::Array<te::EditItemID> getTrackIDs() const;

        /** The master volume and pan (after the master effects; a new song starts at -3 dB).
            The caller starts the undo step, as for tracks (one drag = one step). */
        float getMasterVolumeDb() const;
        void setMasterVolumeDb (float);
        float getMasterPan() const;
        void setMasterPan (float);

        //==============================================================================
        // Timeline editing
        te::TimePosition getPosition() const;
        void setPosition (te::TimePosition);

        /** Rounds a time to the nearest multiple of gridBeats (follows tempo changes). */
        te::TimePosition snapToGrid (te::TimePosition, double gridBeats) const;

        /** Moves a clip to a new start time, and optionally another audio track. One undo step. */
        bool moveClip (te::Clip&, te::TimePosition newStart, int targetTrackIndex);

        void selectClip (te::Clip*, bool addToSelection = false);   // nullptr clears the selection
        void toggleClipSelected (te::Clip&);
        void selectClips (const juce::Array<te::Clip*>&);
        void selectAllClips();
        te::Clip* getSelectedClip() const;              // the first selected clip, if any
        juce::Array<te::Clip*> getSelectedClips() const;
        bool isClipSelected (const te::Clip&) const;

        bool deleteSelectedClips();
        bool deleteSelectedClip()                       { return deleteSelectedClips(); }

        /** Moves every selected clip by the same amount (and the same number of tracks). One undo step. */
        bool moveSelectedClips (te::TimeDuration delta, int trackDelta);

        /** Ctrl+drag: copies every selected clip, shifted by the same amount. The copies become the selection. */
        bool copySelectedClipsBy (te::TimeDuration delta, int trackDelta);

        /** Copies one clip to a new place. The copy becomes the selection. */
        bool copyClipTo (te::Clip&, te::TimePosition newStart, int targetTrackIndex);

        // Clipboard
        bool copySelectedClips();
        bool cutSelectedClips();
        bool pasteClips (te::TimePosition at, int trackIndex);
        bool hasClipsToPaste() const noexcept           { return clipboard != nullptr; }

        /** Puts a copy of each selected clip straight after itself. */
        bool duplicateSelectedClips();

        /** Splits the selected clips at a time; with nothing selected, every clip under that time. */
        bool splitClipsAt (te::TimePosition);

        /** Moves a clip edge, keeping the audio/notes in place (like trimming tape). */
        bool trimClipStart (te::Clip&, te::TimePosition newStart);
        bool trimClipEnd (te::Clip&, te::TimePosition newEnd);

        bool setClipMuted (te::Clip&, bool);
        bool renameClip (te::Clip&, const juce::String&);
        bool setClipColour (te::Clip&, juce::Colour);

        /** Creates an empty MIDI clip (for drawing notes in). */
        bool createMidiClip (int trackIndex, te::TimePosition start, te::TimeDuration length, const juce::String& name = {});

        //==============================================================================
        // Notes (the piano roll). Beats are "content beats": counted from the start of the clip's
        // content, which stays put when the clip's edges are trimmed. Notes not in the clip are ignored.
        using NoteList = juce::Array<te::MidiNote*>;

        static constexpr double minNoteBeats = 1.0 / 32.0;

        te::MidiNote* addMidiNote (te::MidiClip&, int pitch, double startBeat, double lengthBeats, int velocity);
        bool deleteMidiNotes (te::MidiClip&, const NoteList&);

        /** Moves notes in time and pitch together. Refused (nothing moves) if any would leave 0-127 or start before beat 0. */
        bool moveMidiNotes (te::MidiClip&, const NoteList&, double deltaBeats, int deltaPitch);

        /** Makes notes longer or shorter by the same amount (never shorter than minNoteBeats). */
        bool resizeMidiNotes (te::MidiClip&, const NoteList&, double deltaLengthBeats);

        /** Sets velocity (1-127). A drag is one undo step: pass continuesGesture = true for every
            call after the first, and they join the first call's step. */
        bool setMidiNoteVelocity (te::MidiClip&, const NoteList&, int velocity, bool continuesGesture = false);

        /** Moves note starts to the nearest grid line of the song (all notes if the list is empty).
            strength 0..1: how far toward the line (1 = right on it). swing 0..1: delays every second
            line by that fraction of half a grid step (0.67 is about a triplet feel). */
        bool quantiseMidiNotes (te::MidiClip&, const NoteList&, double gridBeats, double strength = 1.0, double swing = 0.0);

        /** Drags the start edge: the note ends where it did. Clamped so no note starts before 0 or gets
            shorter than minNoteBeats. */
        bool resizeMidiNotesFromStart (te::MidiClip&, const NoteList&, double deltaBeats);

        /** Gives every listed note this length. A drag of a value is one undo step: pass
            continuesGesture = true after the first call. */
        bool setMidiNoteLength (te::MidiClip&, const NoteList&, double lengthBeats, bool continuesGesture = false);

        /** Stretches each note up to the start of the next one (all notes if the list is empty). Notes with
            nothing after them stay as they are. */
        bool legatoMidiNotes (te::MidiClip&, const NoteList&);

        // Note clipboard: the session's, so notes can go to another clip or song
        bool copyMidiNotes (te::MidiClip&, const NoteList&);
        bool cutMidiNotes (te::MidiClip&, const NoteList&);
        bool hasNoteClipboard() const                   { return ! noteClipboard.empty(); }

        /** Pastes with the earliest copied note at atBeat. Returns the new notes (empty if refused). */
        NoteList pasteMidiNotes (te::MidiClip&, double atBeat);

        /** Copies the notes to just after the selection (by its own length). Returns the new notes. */
        NoteList duplicateMidiNotes (te::MidiClip&, const NoteList&);

        /** Like moveMidiNotes, but leaves the originals where they are and moves copies (Ctrl-drag). */
        NoteList copyMoveMidiNotes (te::MidiClip&, const NoteList&, double deltaBeats, int deltaPitch);

        /** A clip that repeats its content over its whole length. */
        bool isClipLooping (const te::MidiClip&) const;
        bool setClipLooping (te::MidiClip&, bool shouldLoop);

        /** Where the clip's content beat 0 sits in the song, in beats (for drawing bar lines). */
        double getContentStartInSongBeats (const te::MidiClip&) const;

        /** Plays a note on the clip's track so you hear what you click. Stops the previous one.
            autoRelease: it stops by itself shortly after (for when no mouse-up will follow). */
        void auditionNote (te::MidiClip&, int pitch, int velocity, bool autoRelease = false);
        void stopAudition (te::MidiClip&);

        //==============================================================================
        // Drum machine. A pattern is notes in a MIDI clip: pad n plays note 36 + n
        // (DrumMachinePlugin::noteForPad). Each command is one undo step and changes nothing if refused.
        static constexpr const char* drumTrackName = "Drums";

        DrumMachinePlugin* getDrumMachine (int trackIndex) const;
        DrumMachinePlugin* getDrumMachineFor (const te::Clip&) const;   // the drum machine on the clip's track

        /** A new track with a Drum Machine playing this kit, MIDI inputs routed and armed. */
        bool addDrumMachineTrack (int kitIndex);

        /** Every pad gets the kit's sound and name, and its tone values go back to the defaults
            (mute and solo are kept: they are about the mix, not the sound). */
        bool loadDrumKit (DrumMachinePlugin&, int kitIndex);

        /** A drag of a knob is one undo step: pass continuesGesture = true after the first call. Clamped. */
        bool setDrumPadValue (DrumMachinePlugin&, int pad, DrumMachinePlugin::Value, double value, bool continuesGesture = false);
        bool setDrumPadChoke (DrumMachinePlugin&, int pad, int group);   // 0 = none
        bool setDrumPadMuted (DrumMachinePlugin&, int pad, bool);
        bool setDrumPadSoloed (DrumMachinePlugin&, int pad, bool);
        bool setDrumPadReversed (DrumMachinePlugin&, int pad, bool);
        bool renameDrumPad (DrumMachinePlugin&, int pad, const juce::String&);

        /** Copies the file into the song's folder (so the song never depends on files elsewhere) and
            puts it on the pad. The pad is named after the file. */
        bool loadDrumPadSample (DrumMachinePlugin&, int pad, const juce::File&);

        /** The pad gets the current kit's sound, name and default values back. */
        bool resetDrumPad (DrumMachinePlugin&, int pad);

        /** Gives pad `to` everything pad `from` has (sound, name, values). */
        bool copyDrumPad (DrumMachinePlugin&, int from, int to);

        /** Plays one pad now (it stops by itself). */
        void auditionDrumPad (DrumMachinePlugin&, int pad, int velocity);

        /** Turns one step on or off: on adds a note at stepStart (if none starts in that step), off removes
            every note of that pitch that starts in the step. A paint-drag is one undo step: pass
            continuesGesture = true after the first call. placeAt: where in the step a new hit goes (swing);
            default the step's start. */
        bool setDrumStep (te::MidiClip&, int pitch, double stepStart, double stepBeats, bool on, int velocity,
                          bool continuesGesture = false, bool* changed = nullptr, std::optional<double> placeAt = {});

        /** Notes of one pitch (-1 = all) that start in [fromBeat, toBeat). */
        NoteList getNotesInRange (te::MidiClip&, int pitch, double fromBeat, double toBeat) const;

        bool clearDrumRow (te::MidiClip&, int pitch, double fromBeat, double toBeat);   // pitch -1 = every pad
        /** A roll: `hits` even hits in one step (1 = a single hit, up to 8: trap hat rolls). Keeps the step's
            loudest velocity, or uses `velocity` if the step was empty. One undo step. */
        bool setDrumRoll (te::MidiClip&, int pitch, double stepStart, double stepBeats, int hits, int velocity);

        /** Puts a hit on every `every`-th step from fromBeat (existing hits are kept). */
        bool fillDrumRow (te::MidiClip&, int pitch, double fromBeat, double toBeat, double stepBeats, int every, int velocity);
        /** Moves the hits later (or earlier) inside the range; what falls off one end comes in at the other. */
        bool shiftDrumRow (te::MidiClip&, int pitch, double fromBeat, double toBeat, double deltaBeats);
        /** A pattern lifted out of a clip: offsets are from the start of the range it came from. */
        struct DrumHit { int pitch = 36; double offset = 0.0, length = 0.25; int velocity = 100; };
        std::vector<DrumHit> getDrumHits (te::MidiClip&, double fromBeat, double toBeat) const;

        /** Replaces what is in [fromBeat, toBeat) with the hits (any clip: this is how a pattern is pasted). */
        bool replaceDrumRange (te::MidiClip&, double fromBeat, double toBeat, const std::vector<DrumHit>&);

        /** Replaces [destBeat, destBeat + length) with a copy of [fromBeat, toBeat). */
        bool copyDrumPattern (te::MidiClip&, double fromBeat, double toBeat, double destBeat);
        /** Repeats [fromBeat, toBeat) over the rest of the clip (what was after it is replaced). */
        bool repeatDrumPatternToEnd (te::MidiClip&, double fromBeat, double toBeat);

        /** Puts a copy of the file on a track at a time. */
        bool importAudioFileAt (const juce::File&, int trackIndex, te::TimePosition);

        /** Drag-and-drop: the files go one after another on a track; trackIndex == getNumAudioTracks()
            makes a new track. One undo step. Files that can't be imported are listed in `problems`.
            Returns true if at least one file was imported. */
        bool importAudioFilesAt (const juce::Array<juce::File>&, int trackIndex, te::TimePosition, juce::StringArray& problems);

        // Tracks
        bool deleteTrack (int trackIndex);
        bool duplicateTrack (int trackIndex);
        bool moveTrack (int trackIndex, int newIndex);
        bool setTrackColour (int trackIndex, juce::Colour);
        juce::Colour getTrackColour (int trackIndex) const;

        /** The colours new tracks get, in turn (the design's track colours). Buses get busColour. */
        struct NamedColour { const char* name; juce::Colour colour; };
        static const std::vector<NamedColour>& getTrackPalette();
        static juce::Colour getBusColour()          { return juce::Colour (0xff8a93a6); }

        /** On a clip whose colour the user picked (Tracktion gives every new clip a colour of its own). */
        static inline const juce::Identifier userColourProp { "ezUserColour" };

        /** Every save stamps the song with the format it was written in. A song from a newer
            EzDAWsIt (a higher number) is refused rather than opened: saving it here could drop
            what this version doesn't understand. Raise this when the song format changes in a
            way an older EzDAWsIt would misread. */
        static constexpr int songFormatVersion = 1;
        static inline const juce::Identifier formatVersionProp { "ezFormatVersion" };
        static inline const juce::Identifier savedWithProp { "ezSavedWith" };

        /** The format a song file was saved in: 0 for songs from before the stamp (or unreadable). */
        static int readSongFormatVersion (te::Engine&, const juce::File&);

        /** Length of the song: the end of the last clip. */
        te::TimeDuration getSongLength() const;

        //==============================================================================
        // Loop
        void setLoopRange (te::TimePosition start, te::TimePosition end);
        te::TimeRange getLoopRange() const;
        bool isLooping() const;
        bool setLooping (bool);     // refused while recording (Tracktion would end the take)

        /** Punch recording: only what plays inside the loop region is recorded (the rest of the take is
            kept out). Needs a loop region; refused while recording. Saved with the song. */
        bool isPunchOn() const;
        bool setPunch (bool);

        //==============================================================================
        // Audio clips (SessionAudioEdit.cpp): fades, gain, reverse, normalise, transpose, follow the tempo.
        // Each is one undo step; refused (false, lastError) for MIDI clips and while recording.
        double getClipFadeIn (te::Clip&) const;                  // seconds; 0 for MIDI clips
        double getClipFadeOut (te::Clip&) const;
        bool setClipFades (te::Clip&, double fadeInSeconds, double fadeOutSeconds);
        float getClipGainDb (te::Clip&) const;
        bool setClipGainDb (te::Clip&, float db);                // -48 .. +24
        bool isClipReversed (te::Clip&) const;
        bool setClipReversed (te::Clip&, bool);
        bool normaliseClip (te::Clip&, float peakDb = -1.0f);   // the loudest peak of what it plays to peakDb
        float getClipTranspose (te::Clip&) const;                // semitones
        bool setClipTranspose (te::Clip&, float semitones);      // -24 .. +24, tempo kept
        bool isClipFollowingTempo (te::Clip&) const;
        /** A loop plays at the song's tempo (and follows tempo changes). Its own tempo comes from the file,
            its name ("90bpm"), or its length in whole bars. */
        bool setClipFollowsTempo (te::Clip&, bool);
        static double guessLoopBpm (double lengthSeconds, double songBpm, const juce::String& fileName);

        /** Runs an edit on every selected audio clip as one undo step (false if none is selected). */
        bool editSelectedAudioClips (const std::function<bool (te::Clip&)>&);

        /** Ducking (sidechain): the track gets quieter each time the trigger track plays (bass under the kick,
            a pad pumping to the beat). Adds or retunes a compressor at the end of its effects. One undo step. */
        bool duckUnder (int trackIndex, int triggerTrack);
        bool stopDucking (int trackIndex);
        bool canDuckUnder (int trackIndex, int triggerTrack) const;   // false where it would make a loop
        int getDuckingTrigger (int trackIndex) const;            // -1: not ducking

        /** Tap tempo: call on every tap; from the second tap on, the tempo follows the taps. */
        bool tapTempo();

        // Takes: recording over a loop keeps every pass as a take of one clip; pick the one you want
        // (split the clip first to pick a different take for each part: comping)
        int getNumTakes (te::Clip&) const;
        int getCurrentTake (te::Clip&) const;
        juce::StringArray getTakeNames (te::Clip&) const;
        bool setClipTake (te::Clip&, int takeIndex);            // one undo step
        /** The next (+1) or previous (-1) take of every selected clip that has takes. One undo step. */
        bool stepSelectedTakes (int delta);

        //==============================================================================
        // Routing (tracks are 0-based audio track indexes)
        struct RoutingChoice
        {
            juce::String id, name;
        };

        /** The main mix (master channel -> main output). Track outputs default to this. */
        static constexpr const char* mainMixID = "main-mix";

        juce::String getTrackName (int trackIndex) const;

        /** "Main mix" first, then every hardware output (direct outs bypass the master channel). */
        /** Where you hear the song. Refused while recording: switching devices would break the take. */
        bool setMainOutput (const juce::String& outputID);

        /** For a track: "Main mix", the buses it can play into (never itself or one that already plays
            into it), then every hardware output. FX returns are offered no buses (that could loop back
            through a send). trackIndex -1: just the main mix and the hardware outputs. */
        juce::Array<RoutingChoice> getTrackOutputChoices (int trackIndex = -1) const;
        juce::String getTrackOutput (int trackIndex) const;
        bool setTrackOutput (int trackIndex, const juce::String& choiceID);

        /** "No input" (empty id) first, then every audio and MIDI input. */
        juce::Array<RoutingChoice> getTrackInputChoices() const;
        juce::String getTrackInput (int trackIndex) const;
        bool setTrackInput (int trackIndex, const juce::String& choiceID);

        //==============================================================================
        // FX returns, sends and buses (SessionMix.cpp). All of them are ordinary tracks with a kind, so
        // they get a strip, a header, mute/solo and effects like any track.
        //   FX return: plays what tracks send to it through its effects (one shared reverb instead of
        //              one per track). Solo-safe: soloing a track keeps its reverb. It has no sends and
        //              plays only into the main mix or a hardware output, so nothing can feed back.
        //   Bus:       tracks can play into it instead of the main mix (one fader for all the drums).
        //              A bus can play into another bus (never in a circle) and can send to returns.
        // Routing into a bus is kept by the bus's ID, so it survives moving, adding, deleting and
        // undoing tracks, and saving.
        enum class TrackKind { normal, bus, fxReturn };
        TrackKind getTrackKind (int trackIndex) const;
        static juce::String describeKind (TrackKind);     // "Track", "Bus", "FX return"

        enum class ReturnEffect { reverb, delay, none };
        static constexpr int maxReturns = 16;

        /** Adds an FX return at the end, its effect set 100% wet. Returns its track index, or -1. One undo step. */
        int addReturnTrack (ReturnEffect, const juce::String& name = {});
        /** Adds a bus at the end. Returns its track index, or -1. One undo step. */
        int addBusTrack (const juce::String& name = {});
        /** Adds a bus and plays the tracks into it (one undo step). Returns the bus's index, or -1. */
        int routeTracksToNewBus (const juce::Array<int>& trackIndexes);

        struct ReturnInfo { int trackIndex = -1; int bus = 0; juce::String name; };
        std::vector<ReturnInfo> getReturns() const;      // in track order
        /** The tracks playing into a bus (empty if it isn't one). */
        juce::Array<int> getTracksIntoBus (int busTrackIndex) const;

        struct SendInfo
        {
            bool exists = false;        // no send = nothing goes to that return (shown as -inf)
            float levelDb = minVolumeDb;
            bool preFader = false;      // pre: the send ignores the track's fader
            bool enabled = true;        // off keeps the level for later
        };

        bool canSend (int trackIndex) const;             // FX returns can't
        SendInfo getSend (int trackIndex, int bus) const;
        /** Sets how much goes to the return on `bus`; makes the send (post-fader) if there is none.
            Drags: pass continuesGesture = true after the first call (one undo step). minVolumeDb = -inf. */
        bool setSendLevel (int trackIndex, int bus, float db, bool continuesGesture = false);
        bool setSendPreFader (int trackIndex, int bus, bool preFader);
        bool setSendEnabled (int trackIndex, int bus, bool enabled);
        bool removeSend (int trackIndex, int bus);

        //==============================================================================
        // Automation (SessionAutomation.cpp): a lane under a track draws how one of its controls moves
        // over the song. Points are in seconds; values 0..1 of the control's range (a fader's travel, pan
        // left to right). Every edit is one undo step; drags pass continuesGesture after the first call.
        struct ParamRef
        {
            te::EditItemID plugin;      // the track's fader plugin, a send, an instrument or an effect
            juce::String paramID;

            bool isValid() const                            { return plugin.isValid() && paramID.isNotEmpty(); }
            bool operator== (const ParamRef& o) const       { return plugin == o.plugin && paramID == o.paramID; }
            bool operator!= (const ParamRef& o) const       { return ! operator== (o); }
            juce::String toString() const                   { return plugin.toString() + ":" + paramID; }
            static ParamRef fromString (const juce::String&);
        };

        struct ParamInfo { ParamRef ref; juce::String name, group; };   // group: "Track", "Sends", or the plugin's name

        /** Volume, pan, each send, then every instrument and effect control, in that order. */
        std::vector<ParamInfo> getAutomatableParams (int trackIndex) const;
        ParamRef getVolumeParam (int trackIndex) const;
        ParamRef getPanParam (int trackIndex) const;
        ParamRef getSendParam (int trackIndex, int bus) const;                // invalid if there is no send
        juce::String getParamName (int trackIndex, const ParamRef&) const;   // "Volume", "Reverb: Room size"

        struct AutoPoint { double seconds = 0; float value = 0; float curve = 0; };   // curve -1..1: 0 straight
        std::vector<AutoPoint> getAutomationPoints (int trackIndex, const ParamRef&) const;
        bool isAutomated (int trackIndex, const ParamRef&) const;            // has points
        /** What the lane gives at a time (0..1); with no points, the control's value now. */
        float getAutomationValueAt (int trackIndex, const ParamRef&, double seconds) const;
        float getParamValue (int trackIndex, const ParamRef&) const;          // 0..1, now
        juce::String formatParamValue (int trackIndex, const ParamRef&, float value) const;   // "-6.0 dB", "25L"

        /** Adds a point; returns its index, or -1. The first point on an empty lane also gets a point at the
            start with the control's value now, so the song doesn't jump to the new value everywhere. */
        int addAutomationPoint (int trackIndex, const ParamRef&, double seconds, float value);
        /** Moves a point, never past its neighbours (so its index stays). Returns false if refused. */
        bool moveAutomationPoint (int trackIndex, const ParamRef&, int index, double seconds, float value, bool continuesGesture = false);
        bool setAutomationCurve (int trackIndex, const ParamRef&, int index, float curve, bool continuesGesture = false);
        /** Adds several points as one undo step (paste). Values 0..1, curves -1..1. */
        bool addAutomationPoints (int trackIndex, const ParamRef&, const std::vector<AutoPoint>&);
        bool deleteAutomationPoints (int trackIndex, const ParamRef&, juce::Array<int> indexes);
        bool clearAutomation (int trackIndex, const ParamRef&);

        /** Which lanes show under a track, top to bottom. Saved with the song; not an undo step. */
        std::vector<ParamRef> getShownAutomationLanes (int trackIndex) const;
        bool setAutomationLaneShown (int trackIndex, const ParamRef&, bool shown);
        bool replaceAutomationLane (int trackIndex, int laneIndex, const ParamRef&);
        /** Shows the volume lane (or hides every lane if some show). Returns whether lanes now show. */
        bool toggleAutomationLanes (int trackIndex);

        /** Read: the lanes play (off: controls hold where they are and move by hand).
            Write: holding a control while the song plays records it into its lane (touch). */
        bool isAutomationReadOn() const;
        void setAutomationRead (bool);
        bool isAutomationWriteOn() const;
        void setAutomationWrite (bool);

        /** A control drag starts/ends (needed to record while writing). Unpaired ends are ignored. */
        void beginParameterGesture (int trackIndex, const ParamRef&);
        void endParameterGesture (int trackIndex, const ParamRef&);
        /** True if a control can't be moved by hand now (it is automated, Read is on, not writing while playing). */
        bool isParamLockedByAutomation (int trackIndex, const ParamRef&) const;

        //==============================================================================
        // Controls of our own effects (SessionEffects.cpp), by their index (EffectPlugin / VocalChainPlugin enums).
        // The rack card and tests use these; plugin windows set parameters directly, as Tracktion's do.
        ParamRef getEffectParam (int trackIndex, int effectIndex, int control) const;
        float getEffectControl (int trackIndex, int effectIndex, int control) const;          // stored value, its own units
        juce::String formatEffectControl (int trackIndex, int effectIndex, int control) const;   // "40%", "On", "G"
        /** One undo step (drags: continuesGesture after the first call). Refused, with a hint, if automated. */
        bool setEffectControl (int trackIndex, int effectIndex, int control, float value, bool continuesGesture = false);

        struct ChainPreset { juce::String name, description; };
        static std::vector<ChainPreset> getVocalChainPresets();
        /** The preset last applied to a Vocal Chain, " (edited)" once a macro or switch moved since. */
        juce::String getVocalChainPresetName (int trackIndex, int effectIndex) const;
        /** Sets every macro and switch of the preset (the key stays). One undo step. */
        bool applyVocalChainPreset (int trackIndex, int effectIndex, int presetIndex);
        /** How hard the chain's compressors work now (dB, <= 0). */
        float getVocalChainReductionDb (int trackIndex, int effectIndex) const;

        //==============================================================================
        /** A short line for the status bar (why a control didn't move, say); cleared after a few seconds. */
        void showHint (const juce::String&);
        juce::String getHint() const;

        //==============================================================================
        // Export (SessionExport.cpp): renders in the background, one file after another, through
        // Tracktion's RenderQueue. The song can't be changed while it runs (the export window is modal);
        // playback stops, and closing the song cancels it.
        enum class ExportWhat   { fullMix, stems, busStems, midi };   // stems: tracks with clips; bus stems: buses and FX returns
        enum class ExportRange  { wholeSong, loop, selection };       // selection: from the first to the last selected clip
        enum class ExportFormat { wav, flac, ogg, mp3 };

        struct ExportSettings
        {
            ExportWhat what = ExportWhat::fullMix;
            ExportRange range = ExportRange::wholeSong;
            ExportFormat format = ExportFormat::wav;
            double sampleRate = 48000.0;
            int bitDepth = 24;                  // 16, 24 or 32 (float); WAV and FLAC (FLAC tops out at 24)
            bool dither = true;                 // only matters at 16-bit
            // The full mix always goes through the master fader; stems and MIDI skip the whole master.
            bool useMasterChain = true;         // full mix: the mastering chain, if it's on (off: switched off for the render)
            bool matchLoudness = true;          // full mix only: gain to the target, true peak kept under the ceiling
            float targetLufs = -14.0f;
            float ceilingDb = -1.0f;
            juce::File folder;
            juce::String fileName;              // without extension; stems add " - <track>"
        };

        struct ExportPreset { juce::String name, description; ExportSettings settings; };

        /** The built-in presets (folder and file name left empty: the dialog keeps its own). */
        static const juce::Array<ExportPreset>& getExportPresets();

        /** Your own export presets (Export > Save as preset), saved in Documents\EzDAWsIt\Presets\Export. */
        static juce::Array<ExportPreset> getUserExportPresets();
        bool saveExportPreset (const juce::String& name, const ExportSettings&);   // replaces one of the same name
        bool deleteExportPreset (const juce::String& name);
        static juce::File getExportPresetFolder();
        static juce::String describeExportSettings (const ExportSettings&);         // e.g. "WAV 24-bit, -14 LUFS" (with a middle dot)
        /** Where the song will be played: how loud, and the true-peak ceiling (the Master view and Export). */
        struct LoudnessTarget { juce::String name; float lufs, ceilingDb; };
        static const juce::Array<LoudnessTarget>& getLoudnessTargets();

        /** Last folder used (or Documents\EzDAWsIt\Exports), "<Song> - Master", the song's loudness target and dither. */
        ExportSettings getDefaultExportSettings() const;
        bool isExportFormatAvailable (ExportFormat) const;      // MP3 needs lame.exe next to EzDAWsIt.exe
        static juce::String getExportExtension (const ExportSettings&);

        /** "1.1 to 17.1"; empty when that range can't be exported (no loop, nothing selected, empty song). */
        juce::String describeExportRange (ExportRange) const;

        /** The files an export would write, in order (the dialog checks them for overwriting). */
        juce::Array<juce::File> getExportFiles (const ExportSettings&) const;

        /** "About 12 MB · 3:42" */
        juce::String estimateExport (const ExportSettings&) const;

        /** Starts exporting in the background. False (with getLastError) if it can't start. */
        bool startExport (const ExportSettings&);
        void cancelExport();
        bool isExporting() const noexcept;

        struct ExportStatus
        {
            enum class State { idle, running, measuring, done, failed, cancelled };
            State state = State::idle;
            float progress = 0.0f;              // 0..1 over every file
            int filesDone = 0, filesTotal = 0;
            juce::String current;               // what's rendering now ("Lead Vox")
            juce::String message;               // problems, or how it went
            juce::Array<juce::File> files;      // written
            bool loudnessValid = false;         // full mix: measured from the file
            float integratedLufs = -100.0f, truePeakDb = -100.0f;
        };

        /** Moves the export along (progress, the loudness measurement, playback back afterwards).
            Session's own timer calls it; the export window and the tests call it more often. Message thread. */
        void updateExport();
        const ExportStatus& getExportStatus() const noexcept   { return exportStatus; }

        /** The sample rates a format can write (MP3: up to 48 kHz). */
        juce::Array<int> getExportSampleRates (ExportFormat) const;

        /** After a finished export: back to idle. */
        void clearExportStatus();

        //==============================================================================
        // Presets (SessionPresets.cpp): ready-made setups, built in and your own (saved in
        // Documents\EzDAWsIt\Presets\<kind>, one file each, safe to copy to a friend).
        //   Effect chain    a track's effects in order, with their settings
        //   Track preset    a whole track: instrument (with its sound), effects, colour, level, pan
        //   Song template   a song without its recordings and notes: tracks, buses, returns, sends,
        //                   tempo, key, sections, master chain
        // Applying one is a single undo step; a missing VST3 is still added (shown as missing).
        enum class PresetKind { effectChain, trackPreset, songTemplate };

        struct PresetInfo
        {
            PresetKind kind = PresetKind::effectChain;
            juce::String name, description;
            juce::File file;          // yours; empty for a built-in
            int builtIn = -1;         // built-ins: their index
            bool isBuiltIn() const noexcept     { return builtIn >= 0; }
        };

        static juce::File getPresetFolder (PresetKind);
        static juce::String getPresetExtension (PresetKind);
        juce::Array<PresetInfo> getPresets (PresetKind) const;    // built-ins first, then yours (A-Z)

        bool saveEffectChain (int trackIndex, const juce::String& name);     // replaces one of yours with that name
        bool saveTrackPreset (int trackIndex, const juce::String& name);
        bool saveSongTemplate (const juce::String& name);

        bool applyEffectChain (int trackIndex, const PresetInfo&);           // after the track's own effects
        bool addTrackFromPreset (const PresetInfo&);                         // a new track at the end
        bool createSongFromTemplate (const juce::String& songName, const PresetInfo&);
        bool deletePreset (const PresetInfo&);                               // yours only
        bool presetExists (PresetKind, const juce::String& name) const;     // one of yours already has that name

        /** Says when your presets change (saved, deleted), wherever that happened. */
        juce::ChangeBroadcaster& getPresetBroadcaster() noexcept             { return presetBroadcaster; }

        //==============================================================================
        // Master (SessionMaster.cpp). The master plugin list is: mastering chain, main meter, reference.
        // Every song gets the chain (switched off at first) and the reference, outside the undo history.
        // The chain's controls, presets and on/off are undoable; the reference's A/B is not.

        /** The chain itself (for its control ranges and reduction meters); nullptr without a song. */
        MasteringChainPlugin* getMasteringChain() const;

        bool isMasterChainOn() const;
        bool setMasterChainOn (bool);                          // A/B the whole chain; undoable

        float getMasterControl (int control) const;            // MasteringChainPlugin::Control
        juce::String formatMasterControl (int control) const;
        bool setMasterControl (int control, float value, bool continuesGesture = false);

        static std::vector<ChainPreset> getMasterPresets();
        juce::String getMasterPresetName() const;
        bool applyMasterPreset (int presetIndex);              // also switches the chain on; one undo step

        /** The song's loudness target (index into getLoudnessTargets()); saved with the song. */
        int getMasterTarget() const;
        void setMasterTarget (int index);

        /** Dither 16-bit exports (the song's choice; Export starts with it). */
        bool getMasterDither() const;
        void setMasterDither (bool);

        /** The main meter's loudness (master fader included): integrated, short-term, true peak, range. */
        te::LoudnessMeter::Readings getMasterLoudness() const;

        /** Sets the limiter so the song lands on its target (needs an integrated reading: play the song first).
            Switches the chain and limiter on. One undo step; returns false (with getLastError) if it can't. */
        bool matchMasterTarget();

        // Reference A/B: a finished song to compare against. B plays it instead of the mix (the meters
        // still measure the mix), looped from the song position.
        bool loadReference (const juce::File&);                // measures its loudness in the background
        void clearReference();
        juce::File getReferenceFile() const;
        bool isReferenceListening() const;
        void setReferenceListening (bool);                     // B on / off (not undoable, not saved)
        bool getReferenceMatchLoudness() const;
        void setReferenceMatchLoudness (bool);                 // play it as loud as the mix: a fair comparison
        bool isReferenceMeasured() const noexcept              { return referenceLufsValid; }
        float getReferenceLufs() const noexcept                { return referenceLufs; }

        /** Session's timer calls this (and the Master view, more often): reference loudness, its gain. */
        void updateMaster();

        // The Master assistant: listens through the main meter and suggests one fix at a time
        struct MasterAdvice
        {
            enum class Fix { none, lowerFader, chainOn, moreGlue, monoBass, matchTarget, matchReference };
            Fix fix = Fix::none;
            juce::String title, text, button;    // button empty: nothing to apply
            float amount = 0.0f;                 // lowerFader: dB
        };

        struct MasterState
        {
            bool measured = false, chainOn = false, glueOn = false, monoBassOn = false, referenceMatching = false, referenceMeasured = false;
            float integratedLufs = -100.0f, truePeakDb = -100.0f, rangeLu = 0.0f, glueAmount = 0.0f;
            float targetLufs = -14.0f, ceilingDb = -1.0f, referenceLufs = -100.0f;
            juce::String targetName;
            bool clubTarget = false;
        };

        static MasterAdvice adviseMaster (const MasterState&);   // the rules (tests use them directly)
        MasterAdvice getMasterAdvice() const;
        bool applyMasterAdvice (const MasterAdvice&);            // one undo step; the meter starts again

        //==============================================================================
        /** Every level meter's latest values (track headers, the main meter, later the mixer). */
        MeterBank& getMeters() noexcept                     { return *meters; }
        MidiLearn& getMidiLearn() noexcept                  { return *midiLearn; }   // controllers and MIDI clock

        // For views that draw the song directly (arrangement view only)
        te::Edit* getEdit() noexcept                        { return edit.get(); }
        te::SelectionManager& getSelectionManager() noexcept { return selectionManager; }

        void addListener (Listener* l)                      { listeners.add (l); }
        void removeListener (Listener* l)                   { listeners.remove (l); }

    private:
        //==============================================================================
        AudioEngine& audioEngine;
        te::SelectionManager selectionManager;
        std::unique_ptr<te::Edit> edit;
        std::unique_ptr<te::EditFileOperations> fileOps;   // kept alive so background autosaves stay in the background
        juce::File songFile;
        std::unique_ptr<te::Clipboard::Clips> clipboard;
        juce::ListenerList<Listener> listeners;
        std::unique_ptr<MeterBank> meters;
        std::unique_ptr<MidiLearn> midiLearn;
        std::vector<double> tapTimes;   // tap tempo, seconds
        bool batchEdit = false;          // inside editSelectedAudioClips: one undo step for all
        juce::String lastError;
        juce::Time lastAutosave;

        // Presets
        int effectInsertIndex (te::AudioTrack&) const;   // after the instrument and effects, before sends and the fader
        /** Adds a plugin from a saved state. missing: added but not installed here; unknown: not added (no such type);
            full: not added, the track has no room. */
        te::Plugin::Ptr insertPluginState (te::AudioTrack&, const juce::ValueTree& state, int index,
                                           juce::StringArray& missing, juce::StringArray& unknown, bool& full);
        juce::ChangeBroadcaster presetBroadcaster;

        // Master
        void ensureMasterPlugins();
        ReferencePlugin* getReference() const;
        void updateReferenceGain();
        void startReferenceScan (const juce::File&);
        std::unique_ptr<LoudnessScan> referenceScan;
        float referenceLufs = -100.0f;
        bool referenceLufsValid = false;

        // Export
        struct ExportJobs;
        std::unique_ptr<ExportJobs> exportJobs;
        ExportStatus exportStatus;
        void finishExport();
        bool isExportingNow() const noexcept;
        void restorePlaybackAfterExport();
        bool playbackNeedsRestoring = false;   // the render freed the playback graph
        void abandonExport();   // closing the song: cancel and wait, no callbacks
        void switchMasterChainBackOn (ExportJobs&);
        te::TimeRange getExportTimeRange (ExportRange) const;
        juce::Array<te::AudioTrack*> getExportTracks (ExportWhat) const;
        juce::int64 lastWrittenFingerprint = 0;   // content of the last save/autosave
        juce::int64 lastSavedQuickPrint = 0;      // the song file as last saved (no plugin flush: see hasUnsavedChanges)
        std::atomic<bool> maybeEdited { false };
        bool unsavedCache = false;
        juce::int64 getQuickFingerprint() const;
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { maybeEdited = true; }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { maybeEdited = true; }
        bool saveWhenStopped = false;
        bool wasRecording = false;

        bool adoptEdit (std::unique_ptr<te::Edit>, const juce::File&, bool isNewSong);
        void ensureMasterTap();
        /** mustWriteNow = the song is about to be closed: if Tracktion is mid-operation,
            write an emergency copy and report it, instead of postponing. */
        bool writeSongNow (bool mustWriteNow = false);
        bool writeEmergencyCopy();
        juce::int64 getContentFingerprint() const;
        void connectInputs (bool armFirstInputOnTrackOne);
        void routeMidiInputsTo (te::AudioTrack&);
        te::AudioTrack* getTrack (int trackIndex) const;
        NoteList notesInClip (te::MidiClip&, const NoteList&) const;
        bool applyDrumKit (DrumMachinePlugin&, int kitIndex);
        te::MarkerClip* findSectionClip (te::EditItemID) const;
        te::TimePosition beatToTime (double beat) const;
        std::vector<int> getTimeSigBars() const;
        void keepTimeSigsOnBars (const std::vector<int>& barsOfChangesAfterTheFirst, int skipIndex = -1);
        bool checkDrumPad (DrumMachinePlugin&, int pad);
        bool setDrumPadProperty (DrumMachinePlugin&, int pad, const juce::Identifier&, const juce::var&, bool continuesGesture = false);
        bool checkDrumClip (te::MidiClip&);
        double getClipContentEndBeat (const te::MidiClip&) const;

        struct ClipboardNote { int pitch = 60; double startBeat = 0.0, lengthBeats = 0.25; int velocity = 100; };
        std::vector<ClipboardNote> noteClipboard;   // starts are relative to the earliest note
        int getTrackIndex (const te::Track*) const;
        bool canShiftClips (const juce::Array<te::Clip*>&, te::TimeDuration delta, int trackDelta);
        bool refuseWhileRecording (const juce::String& action);
        std::optional<te::TimeDuration> insertAudioFile (const juce::File&, te::AudioTrack&, te::TimePosition start);
        te::Clip* placeClipCopy (const te::Clip&, te::TimePosition start, te::AudioTrack& target);
        juce::Array<te::Plugin*> getEffects (int trackIndex) const;

        // SessionAutomation.cpp
        te::AutomatableParameter* findParam (int trackIndex, const ParamRef&) const;
        std::set<juce::String> activeGestures;
        juce::String hint;
        juce::uint32 hintTime = 0;
        bool refuseIfAutomated (te::AutomatableParameter*, const juce::String& what);
        void endAllParameterGestures();

        // SessionMix.cpp
        static TrackKind kindOf (const te::AudioTrack&);
        te::AuxSendPlugin* findSend (te::AudioTrack&, int bus) const;
        te::AudioTrack* findReturnTrack (int bus) const;
        int findFreeBus() const;
        te::AudioTrack* findBusTarget (const te::AudioTrack&) const;   // the bus this track plays into
        bool canPlayInto (const te::AudioTrack& source, const te::AudioTrack& bus) const;
        juce::String makeUniqueTrackName (const juce::String& base) const;
        te::AudioTrack* appendTrack (const juce::String& name, TrackKind);

        /** Gives every track from `firstIndex` on that has no colour the next palette colour (part of the open undo step). */
        void colourNewTracks (int firstIndex);

        /** On opening a song: tracks with no colour get one, outside the undo history. */
        void colourUncolouredTracks();
        bool routeIntoBus (te::AudioTrack& source, te::AudioTrack& bus);   // in the current undo step
        void removeSendsTo (int bus);                                        // in the current undo step
        void rerouteTracksInto (te::AudioTrack& bus);                       // to the bus's own output, undoable
        /** Tracktion stores "plays into track N" by track NUMBER: after tracks move (or an undo), put the
            numbers right again from the bus IDs we keep. Not an undo step of its own. */
        void syncBusRouting();
        int makeBus (const juce::Array<int>& trackIndexes, const juce::String& name);
        bool fail (const juce::String& reason);
        void reportProblem (const juce::String& message);
        void notifyStateChanged();

        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        /** Tracktion doesn't rebuild playback when plugins are reordered (by us, or by undo/redo):
            we do, or the sound keeps the old order. */
        void valueTreeChildOrderChanged (juce::ValueTree& parent, int oldIndex, int newIndex) override;
        int playbackRebuildsForReorder = 0;
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
    };
}
