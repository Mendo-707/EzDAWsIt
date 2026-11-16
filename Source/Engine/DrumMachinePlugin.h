/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The Drum Machine instrument: 16 pads, each playing one sample. Pad n plays
    on MIDI note 36 + n (pad 1 = C1 = the kick, like MPC and GM).

    Per pad: name, sample file, tune, decay, start, pan, level, choke group,
    mute, solo, reverse. All of it lives in the plugin's ValueTree, so it is
    saved with the song and undoable. Change it through Session (the command
    layer), never directly.

    Threads: the message thread loads samples and builds a read-only snapshot;
    the audio thread swaps in the newest snapshot under a spin lock held only
    for a pointer swap. Voices remember which sample they play by a generation
    number, never by a pointer they could keep after the sample is gone.
*/

#pragma once

#include "../Core/Common.h"
#include "DrumKits.h"

namespace ez
{
    class DrumMachinePlugin : public te::Plugin,
                              private juce::AsyncUpdater
    {
    public:
        static constexpr int numPads = DrumKits::numPads;
        static constexpr int firstNote = 36;
        static constexpr int maxVoices = 64;

        // Ranges of the pad values (Session clamps to these)
        static constexpr double minTune = -24.0, maxTune = 24.0;       // semitones
        static constexpr double minDecay = 0.02, maxDecay = 1.0;       // fraction of the sample that plays
        static constexpr double minStart = 0.0, maxStart = 0.9;        // fraction skipped at the start
        static constexpr double minLevel = -60.0, maxLevel = 6.0;      // dB
        static constexpr int maxChokeGroup = 4;

        static int noteForPad (int pad) noexcept    { return firstNote + pad; }
        static int padForNote (int note) noexcept   { return juce::isPositiveAndBelow (note - firstNote, numPads) ? note - firstNote : -1; }

        explicit DrumMachinePlugin (te::PluginCreationInfo);
        ~DrumMachinePlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS ("Drum Machine"); }
        static const char* xmlTypeName;

        //==============================================================================
        enum class Value { tune, decay, start, pan, level };

        struct PadSettings
        {
            juce::String name, file;
            double tune = 0.0, decay = 1.0, start = 0.0, pan = 0.0, level = 0.0;
            int choke = 0;
            bool muted = false, soloed = false, reversed = false;
        };

        PadSettings getPad (int pad) const;
        juce::String getKitName() const;

        /** The pad's state (a child of the plugin's state). Session writes it with the edit's undo manager. */
        juce::ValueTree getPadState (int pad) const;

        static juce::Identifier idFor (Value);
        static double defaultFor (Value);
        static double clampValue (Value, double);

        // State property names
        static const juce::Identifier kitID, padType, nameID, fileID, tuneID, decayID, startID, panID, levelID, chokeID, muteID, soloID, reverseID;

        /** The pad's sample as loaded (for drawing its waveform). Message thread. */
        std::shared_ptr<const juce::AudioBuffer<float>> getPadAudio (int pad) const;

        /** Why the pad's sample didn't load, or empty. Message thread. */
        juce::String getPadLoadError (int pad) const;

        /** Counts up each time the pad is played (by a clip, your pads or a click): views flash the pad. */
        juce::uint32 getPadHitCount (int pad) const noexcept;

        /** What a pad's sound is: "kit:<kit name>:<pad>" (made by DrumKits), a path relative to the
            song's folder (copied in with the song), or an absolute path. */
        static juce::String makeKitSource (int kitIndex, int pad);

        /** "Boom Bap Dust · Snare", or the file's name. */
        static juce::String describeSource (const juce::String&);

        /** Applies pending state changes now (normally done asynchronously). Message thread. */
        void flushPendingUpdates();

        //==============================================================================
        juce::String getName() const override                   { return TRANS ("Drum Machine"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "Drums"; }
        juce::String getSelectableDescription() override        { return TRANS ("Drum Machine"); }
        bool isSynth() override                                 { return true; }
        bool takesMidiInput() override                          { return true; }
        bool takesAudioInput() override                         { return false; }
        bool producesAudioWhenNoAudioInput() override           { return true; }
        int getNumOutputChannelsGivenInputs (int) override      { return 2; }
        BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }
        bool hasNameForMidiNoteNumber (int note, int midiChannel, juce::String& name) override;

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override;
        void applyToBuffer (const te::PluginRenderContext&) override;
        void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    private:
        //==============================================================================
        struct PadSound
        {
            std::shared_ptr<const juce::AudioBuffer<float>> audio;
            double sourceSampleRate = 44100.0;
            juce::uint32 generation = 0;   // changes whenever `audio` does
            float tuneRatio = 1.0f, gainL = 1.0f, gainR = 1.0f;
            float decay = 1.0f, start = 0.0f;
            int choke = 0;
            bool playable = true, reversed = false;
        };

        struct Snapshot
        {
            std::array<PadSound, numPads> pads;
        };

        struct Voice
        {
            bool active = false, releasing = false;
            int pad = 0, delay = 0;
            juce::uint32 generation = 0;
            double position = 0.0, increment = 1.0;   // in source samples
            double first = 0.0, last = 0.0;           // the part of the sample that plays
            double fadeFrom = 0.0;                    // where the decay fade starts
            float gainL = 1.0f, gainR = 1.0f, release = 1.0f, releaseStep = 0.0f;
            int chokeAt = -1;   // in this block: the sample where a choking hit starts the release
            juce::uint32 age = 0;
        };

        // Message thread
        struct LoadedSample
        {
            juce::String file;
            std::shared_ptr<const juce::AudioBuffer<float>> audio;
            double sampleRate = 44100.0;
            juce::uint32 generation = 0;
            juce::String error;
        };

        std::array<LoadedSample, numPads> loaded;
        juce::uint32 nextGeneration = 1;
        juce::AudioFormatManager formats;

        // Shared: the newest snapshot (message thread writes, audio thread swaps it in)
        juce::SpinLock pendingLock;
        std::unique_ptr<Snapshot> pending;

        // Audio thread
        std::unique_ptr<Snapshot> live;
        std::array<Voice, maxVoices> voices;
        juce::uint32 voiceAge = 0;
        std::array<std::atomic<juce::uint32>, numPads> hits {};

        std::unique_ptr<Snapshot> retired;   // the snapshot the audio thread replaced: freed on the message thread
        juce::SpinLock retiredLock;

        std::atomic<bool> needsRebuild { false };

        juce::File resolveSource (const juce::String&, juce::String& error) const;
        void ensurePads();
        void rebuildSnapshot();
        void loadSampleIfChanged (int pad, const juce::String& file);
        void startVoice (int pad, int velocity, int delay, const Snapshot&);
        void renderVoice (Voice&, const PadSound&, juce::AudioBuffer<float>&, int startSample, int numSamples);

        void valueTreeChanged() override;
        void handleAsyncUpdate() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumMachinePlugin)
    };
}
