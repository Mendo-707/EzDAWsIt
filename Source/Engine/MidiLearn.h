/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    MIDI controllers: knobs, faders and pads on a controller (DDJ-REV1, MC-707, any MIDI keyboard)
    moving EzDAWsIt's controls, and MIDI clock out so a drum machine or groovebox follows the tempo.

    Learn: start learning, touch a control in EzDAWsIt (a fader, a knob, an effect setting), then
    move a knob on the controller. Or pick an action (Play, Record ...) and press a pad or button.
    The links are saved with the song. A knob jumps the control to where the knob is.

    MIDI arrives on the MIDI thread; it's queued and applied on the message thread, so nothing in the
    song is touched from another thread.
*/

#pragma once

#include "Session.h"

namespace ez
{
    class AudioEngine;

    class MidiLearn : private Session::Listener,
                      private te::PhysicalMidiInputDevice::Listener,
                      private te::ParameterChangeHandler::Listener,
                      private juce::ChangeListener,
                      private juce::Timer
    {
    public:
        MidiLearn (AudioEngine&, Session&);
        ~MidiLearn() override;

        //==============================================================================
        struct Mapping
        {
            int channel = 0;          // 1..16
            int number = 0;           // controller number, or note number for a note
            bool isNote = false;
            juce::String target;      // what it moves or does, for showing
        };

        juce::Array<Mapping> getMappings() const;
        bool removeMapping (int index);
        bool removeMapping (int channel, int number, bool isNote);   // by the knob or pad
        void clearMappings();

        /** Learn a control: the next control you touch in EzDAWsIt, then the next knob/fader/pad you move. */
        void startLearning();
        /** Learn an action (one of getActionNames): the next pad or button you press does it. */
        void startLearningAction (const juce::String& actionName);
        void stopLearning();
        bool isLearning() const noexcept                { return learning; }
        /** What learning is waiting for, in words ("Touch a control in EzDAWsIt" ...); empty when not learning. */
        juce::String getLearnStatus() const;

        /** Actions that can be put on a pad or button. performAction runs them (set by the main window). */
        static juce::StringArray getActionNames();      // "Play / stop", "Record", ...
        std::function<void (const juce::String& actionName)> performAction;

        /** Called whenever the mappings or learning change. */
        std::function<void()> onChange;

        //==============================================================================
        // MIDI clock out: a drum machine or groovebox follows the song's tempo, start and stop
        struct ClockOutput { juce::String id, name; bool sending = false; };
        juce::Array<ClockOutput> getClockOutputs() const;
        bool setSendingClock (const juce::String& outputID, bool);
        bool isSendingClockAnywhere() const;

        /** The MIDI inputs being listened to (names), for showing. */
        juce::StringArray getInputNames() const;

        /** For tests: as if this arrived from a controller (message thread). */
        void handleForTest (const juce::MidiMessage&);

    private:
        struct Incoming { int channel, number, value; bool isNote; };

        AudioEngine& audioEngine;
        Session& session;
        std::vector<std::shared_ptr<te::MidiInputDevice>> listenedTo;
        te::Edit* attachedEdit = nullptr;

        juce::AbstractFifo fifo { 512 };
        juce::SpinLock writeLock;   // each controller has its own MIDI thread: one writer at a time
        std::array<Incoming, 512> queue {};

        std::atomic<bool> learning { false };
        juce::String learningAction;
        te::AutomatableParameter::Ptr touched;   // the last control touched in EzDAWsIt while learning
        juce::uint32 lastMoveTime = 0;
        std::map<int, int> lastValues;   // buttons sent as controllers: the value before

        void handleIncomingMidiMessage (const juce::MidiMessage&) override;   // MIDI thread
        void pluginParameterChanged (te::AutomatableParameter&, bool isFollowingAutomation) override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;      // MIDI devices changed
        void timerCallback() override;

        void listenToInputs();
        void attachToSong();
        void detachFromSong();
        void apply (const Incoming&);
        juce::ValueTree getState (bool createIfMissing) const;   // the song's links (invalid if none and not creating)
        te::AutomatableParameter::Ptr findParameter (const juce::ValueTree& mapping) const;
        juce::String describe (const juce::ValueTree& mapping) const;
        void changed();

        void sessionSongClosing() override      { detachFromSong(); }
        void sessionSongChanged() override      { attachToSong(); }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearn)
    };
}
