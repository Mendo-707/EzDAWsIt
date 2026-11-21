/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The transport row:
      |< Play Rec Loop · position · tempo · 4/4 · key | Click Count-in · Punch | Snap Undo Redo  ...  Sync · meter
    Tempo, time signature and key show what is in effect at the playhead and change it there.
    Tempo: drag up/down (Shift for fine), mouse wheel, or double-click to type.
    Time signature and key: click for a menu, mouse wheel to step.
    When the window is narrow, planned items hide first, then labels shrink to icons.
*/

#pragma once

#include "../../Engine/Session.h"
#include "../Widgets.h"
#include "../LevelMeter.h"

namespace ez
{
    class TransportBar : public juce::Component,
                         private Session::Listener,
                         private juce::Timer
    {
    public:
        TransportBar (Session&, juce::ApplicationCommandManager&);
        ~TransportBar() override;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

        /** The grid's name for its button ("Auto", "1/16"...), and where its menu opens. */
        std::function<juce::String()> gridText;
        juce::Component& getGridButton()    { return gridButton; }

    private:
        class PositionDisplay;
        class TempoDisplay;
        class LcdText;

        Session& session;

        IconButton returnButton { "Return to start", Icon::returnToStart };
        IconButton playButton   { "Play", Icon::play, {}, IconButton::Style::play };
        IconButton recordButton { "Record", Icon::record };
        IconButton loopButton   { "Loop", Icon::loop };
        IconButton clickButton  { "Click", Icon::metronome, "Click" };
        IconButton countInButton { "Count-in", Icon::countIn, "Count-in" };
        IconButton punchInButton  { "Punch in", Icon::none, "Punch in" };
        IconButton punchOutButton { "Punch out", Icon::none, "Punch out" };
        IconButton snapButton   { "Snap", Icon::magnet, "Snap" };
        IconButton undoButton   { "Undo", Icon::undo };
        IconButton redoButton   { "Redo", Icon::redo };
        IconButton syncButton   { "Sync", Icon::clock, "Sync" };
        IconButton gridButton   { "Grid", Icon::none, "Auto" };
        IconButton selectToolButton { "Select tool", Icon::pointer };
        IconButton splitToolButton  { "Split tool", Icon::scissors };
        IconButton eraseToolButton  { "Erase tool", Icon::eraser };

        std::unique_ptr<PositionDisplay> position;
        std::unique_ptr<TempoDisplay> tempo;
        std::unique_ptr<LcdText> signature, key;
        std::unique_ptr<LevelMeter> meter;   // the main output: level and loudness

        juce::Array<int> separatorXs;
        bool wasPlaying = false, wasRecording = false;

        void refreshTransportButtons();
        void sessionSongChanged() override      { refreshTransportButtons(); }
        void sessionStateChanged() override     { refreshTransportButtons(); }
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
    };
}
