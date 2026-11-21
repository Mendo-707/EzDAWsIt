/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The arrangement: ruler on top; track headers on the left; lanes with clips
    on the right; playhead over everything.

      Mouse wheel             scroll tracks up/down
      Shift + wheel           scroll the song left/right
      Ctrl + wheel            zoom in/out around the mouse
      Alt + wheel             make tracks taller/shorter
      Click empty lane        move the playhead there, deselect
      Drag on empty lane      select every clip the box touches (Shift adds)
      Double-click lane       new empty MIDI clip (one bar)
      Right-click lane        paste here, new MIDI clip, ...
      Drop audio files        import them where they're dropped
      Read / Write (corner)   automation plays (Shift+W) / moving a control while playing records it (W)
      Automation lanes        under each track (A shows/hides them): see AutomationLane.h

    Draws the song directly (the one view allowed to); every change goes
    through Session. Rebuilds itself when tracks or clips change.
*/

#pragma once

#include "../../Engine/Session.h"
#include "TimelineViewState.h"
#include "../Widgets.h"

namespace ez
{
    class Ruler;
    class TempoLane;
    class SectionsLane;
    class TrackHeader;
    class ClipView;
    struct EditorTarget;

    class TimelineView : public juce::Component,
                         private Session::Listener,
                         private juce::ScrollBar::Listener,
                         private juce::Timer
    {
    public:
        explicit TimelineView (Session&);
        ~TimelineView() override;

        void zoom (double factor);
        void zoomToFit();                       // the whole song across the screen
        bool isFollowing() const noexcept       { return following; }
        void setFollowing (bool shouldFollow)   { following = shouldFollow; }   // the view pages along with the playhead
        bool loopSelection();                   // the loop region round the selected clips, loop on
        void toggleSnap();

        using Tool = TimelineViewState::Tool;
        void setTool (Tool t)                   { view.tool = t; }
        Tool getTool() const                    { return view.tool; }

        /** The grid the user chose (beats; 0 = automatic, following the zoom). */
        void setFixedGridBeats (double beats);
        double getFixedGridBeats() const        { return view.fixedGridBeats; }
        bool isSnapEnabled() const noexcept     { return view.snapEnabled; }

        // Edit actions (menus and keyboard shortcuts). Each returns false if nothing happened.
        bool copy();
        bool cut();
        bool paste();                           // at the playhead, on the focused track
        bool duplicate();
        bool splitAtPlayhead();
        bool deleteSelection();
        void selectAll();
        void deselectAll();
        void nudge (int direction);             // selected clips (or the playhead) by one grid step
        void goToEnd();

        /** The highlighted track: where Paste and browser effects go. */
        int getFocusedTrack() const noexcept    { return view.focusedTrack; }
        void setFocusedTrack (int track)        { view.focusedTrack = track; repaint(); }   // the mixer highlights tracks too

        /** The automation lane you last clicked in, if any: Delete, Ctrl+C... go to it. */
        EditorTarget* getFocusedEditor() const;

        /** "+ Track" was clicked: show the add-track menu next to this button. */
        std::function<void (juce::Component& anchor)> onAddTrack;

        /** A MIDI clip was double-clicked (or just made by double-clicking): show its notes. It is selected. */
        std::function<void()> onEditClip;

        /** Called with a title and message when something the user asked for didn't work. */
        std::function<void (const juce::String& title, const juce::String& message)> onProblem;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class Lanes;
        class TracksArea;
        class PlayheadOverlay;

        Session& session;
        TimelineViewState view;

        std::unique_ptr<Ruler> ruler;
        std::unique_ptr<TempoLane> tempoLane;
        std::unique_ptr<SectionsLane> sectionsLane;
        std::unique_ptr<TracksArea> tracksArea;
        std::unique_ptr<PlayheadOverlay> playhead;
        juce::Viewport viewport;
        juce::ScrollBar horizontalScroll { false };
        IconButton addTrackButton { "Add track", Icon::plus, "Track" };
        IconButton zoomOutButton  { "Zoom out", Icon::zoomOut, {}, IconButton::Style::ghost };
        IconButton zoomInButton   { "Zoom in", Icon::zoomIn, {}, IconButton::Style::ghost };
        IconButton zoomFitButton  { "Zoom to fit", Icon::none, "Fit", IconButton::Style::ghost };
        IconButton readButton     { "Read automation", Icon::none, "Read" };
        IconButton writeButton    { "Write automation", Icon::none, "Write" };
        void refreshAutomationButtons();
        juce::Label emptyMessage;

        juce::String lastStructure;
        int tick = 0;
        bool checkStructureSoon = false;

        void handleWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&, int xInLanes);
        void rebuild();
        void layoutForView();
        void layoutTracks();
        juce::String describeStructure() const;
        void followPlayhead();
        bool following = true;
        void updateScrollBar();
        int getPasteTrack() const;
        bool report (bool ok, const juce::String& title);

        void sessionSongClosing() override;
        void sessionSongChanged() override;
        void sessionStateChanged() override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineView)
    };
}
