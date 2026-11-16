/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The tempo / time signature / key lane above the ruler. Shows the song's
    tempo as a line, and a flag with its value at every change.

    Controls (AGENTS.md rule 0a):
      Double-click empty      add a tempo change at that bar, then type its BPM
      Double-click a tempo    type its BPM
      Double-click sig / key  pick a new one from a menu
      Drag a change           move it (snaps to bars; Alt: to beats). The first can't move.
      Wheel on a tempo        +/- 1 BPM (Shift: 0.1). On a key: next / previous root.
                              On a time signature: +/- 1 beat per bar.
      Wheel elsewhere         scrolls the song like the ruler
      Click empty             move the playhead there
      Right-click empty       add a tempo / time signature / key change here, move the playhead
      Right-click a change    set its value, glide to the next tempo, delete (not the first)
      Esc while dragging      cancel
*/

#pragma once

#include "../../Engine/Session.h"
#include "TimelineViewState.h"

namespace ez
{
    class TempoLane : public juce::Component,
                      public juce::SettableTooltipClient
    {
    public:
        TempoLane (Session&, TimelineViewState&);

        std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheelElsewhere;
        std::function<void (const juce::String& title, const juce::String& message)> onProblem;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;

        /** Menus shared with the transport's time signature and key boxes. The callback gets the choice. */
        static void addTimeSigItems (juce::PopupMenu&, int firstId, int currentNumerator, int currentDenominator);
        static bool timeSigForId (int id, int firstId, int& numerator, int& denominator);
        static void addKeyItems (juce::PopupMenu&, int firstId, int currentRoot, bool currentMinor);
        static bool keyForId (int id, int firstId, int& root, bool& minor);

    private:
        enum class Kind { tempo, timeSig, key };

        struct Item
        {
            Kind kind;
            int index;
            double beat;
            juce::Rectangle<int> bounds;   // the flag and its text
            juce::String text;
        };

        Session& session;
        TimelineViewState& view;
        std::vector<Item> items;
        std::optional<Item> dragging;
        int dragX = 0;
        double dragBeat = 0.0;            // where the drag will land (snapped), committed exactly
        bool typingAfterAdd = false;      // a double-click add and its typed tempo are one undo step
        bool dragMoved = false;
        juce::TextEditor bpmEditor;
        int editingTempo = -1;

        void rebuildItems();
        const Item* itemAt (juce::Point<int>) const;
        int xForBeat (double beat) const;
        double beatAtX (int x) const;
        double snapToBar (double beat) const;
        int barNear (double beat) const;
        void showEmptyMenu (double beat, int x);
        void showItemMenu (const Item&);
        void startTypingBpm (int tempoIndex);
        void commitTypedBpm();
        void report (bool ok, const juce::String& title);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempoLane)
    };
}
