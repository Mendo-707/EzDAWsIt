/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The Tuner window (Ctrl+U): the note you're playing into the highlighted track and how far
    off it is. It follows the track you click. The track needs to be armed (so the guitar is
    heard); the window puts a Tuner first in the track's effects, where it hears the plain input.
*/

#pragma once

#include "../Engine/Session.h"
#include "../Engine/Effects.h"

namespace ez
{
    class TunerView : public juce::Component,
                      private juce::Timer,
                      private Session::Listener
    {
    public:
        TunerView (Session&, std::function<int()> getHighlightedTrack);
        ~TunerView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int preferredWidth = 420, preferredHeight = 300;

    private:
        Session& session;
        std::function<int()> getHighlightedTrack;
        int track = -1;
        int addedFor = -1;   // the track the window put a tuner on (once: an undo that takes it away is respected)
        juce::ReferenceCountedObjectPtr<TunerPlugin> tuner;   // kept alive while shown (an undo may remove it)
        TunerPlugin::Reading reading;
        float shownCents = 0;
        juce::uint32 lastNoteTime = 0;
        juce::String problem;

        juce::Label trackLabel;
        juce::TextButton armButton { "Arm track" }, addButton { "Add the tuner" }, muteButton { "Mute while tuning" },
                         lowerButton { "-" }, higherButton { "+" };
        juce::Label referenceLabel;

        juce::Rectangle<int> displayArea;

        void timerCallback() override;
        void sessionSongClosing() override;   // the tuner belongs to the song: let go of it first
        void followTrack();
        void setReference (float hz);
        void updateControls();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerView)
    };

    /** A window holding a TunerView; onClose runs when its close button is pressed. */
    std::unique_ptr<juce::DocumentWindow> createTunerWindow (Session&, std::function<int()> getHighlightedTrack,
                                                             std::function<void()> onClose);
}
