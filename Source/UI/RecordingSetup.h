/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Setup > Recording: how the interface's inputs are split (one per channel for a band, or stereo
    pairs), a level check for every input (too quiet / good / clipping), the recording delay fix,
    punch recording and takes.
*/

#pragma once

#include "../Engine/Session.h"
#include "Widgets.h"

namespace ez
{
    class AudioEngine;

    class RecordingSetup : public juce::Component,
                           private juce::Timer,
                           private juce::ChangeListener,
                           private Session::Listener
    {
    public:
        RecordingSetup (AudioEngine&, Session&);
        ~RecordingSetup() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** "Too quiet", "Good", "Loud: turn the gain down a little", "Clipping: turn the gain down" */
        static juce::String describeLevel (float peakDb, bool clippedRecently);

    private:
        struct InputMeter;

        AudioEngine& audioEngine;
        Session& session;

        juce::Label inputsHeading, levelsHeading, delayHeading, punchHeading, levelsHint, delayHint, takesHint;
        juce::ToggleButton monoButton { "One input per channel: a microphone or guitar each (recording a band)" },
                           stereoButton { "Stereo pairs: keyboards, a mixer's stereo out" },
                           punchButton { "Punch: record only inside the loop region (P)" };
        juce::Slider delaySlider;
        juce::Viewport meterViewport;
        juce::Component meterContent;
        juce::OwnedArray<InputMeter> meters;
        juce::String lastSignature;
        int pendingMono = -1;   // chosen, while the device list is rebuilt (0 stereo, 1 mono)

        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;   // the device list changed
        void sessionStateChanged() override;
        void sessionSongChanged() override;
        void rebuildMeters();
        void dropMeters();
        void refreshControls();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingSetup)
    };
}
