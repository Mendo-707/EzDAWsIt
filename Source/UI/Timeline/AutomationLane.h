/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Automation lanes: under a track, how one of its controls (volume, pan, a send, any
    effect or instrument control) moves over the song.

    AutomationLane (the graph, in the timeline's lanes):
      Double-click empty      add a point there (on the grid; Alt: off it)
      Double-click a point    delete it
      Drag a point            move it (and every selected point); Shift: up/down only; Alt: off the grid
      Shift+click a point     add it to / take it out of the selection
      Drag on empty           select the points in the box (Shift adds)
      Click empty             deselect (it never moves the playhead: a double-click would jump the song)
      Ctrl+drag a line        bend it (the curve after the point to its left)
      Right-click             point: delete, straight/ease in/ease out; lane: add a point, select all,
                              paste, change control, add a lane, clear, hide
      Keys (after a click)    Delete, Ctrl+A, Esc, Ctrl+C/X/V/D, Left/Right (a grid step),
                              Up/Down (value; Shift: fine)
      Wheel                   scrolls like the rest of the timeline

    AutomationLaneHeader (to its left): which control (click: change it), its value at the playhead,
    Clear, Hide; right-click for the same menu as the lane.
*/

#pragma once

#include "../../Engine/Session.h"
#include "TimelineViewState.h"
#include "../Shell/EditorTarget.h"
#include "../Widgets.h"
#include <set>

namespace ez
{
    namespace AutomationMenus
    {
        /** Every control of a track, grouped (Track, Sends, then a submenu per plugin), as items base + index
            into Session::getAutomatableParams. The current one is ticked; automated ones say so. */
        void addParamItems (juce::PopupMenu&, Session&, int trackIndex, int base, const Session::ParamRef& current);

        /** The lane menu's lane part (change control, add a lane, clear, hide), shared by lane and header. */
        void showLaneMenu (Session&, int trackIndex, int laneIndex, const Session::ParamRef&,
                           std::function<void (const juce::String&)> onProblem, juce::PopupMenu extraItemsFirst = {},
                           std::function<void (int)> onExtraItem = {});
    }

    //==============================================================================
    class AutomationLane : public juce::Component,
                           public juce::SettableTooltipClient,
                           public EditorTarget
    {
    public:
        AutomationLane (Session&, TimelineViewState&, int trackIndex, int laneIndex, Session::ParamRef);

        int getTrackIndex() const noexcept                  { return trackIndex; }
        const Session::ParamRef& getParam() const noexcept  { return param; }

        /** Points changed elsewhere (undo, recording with Write): redraw. Cheap when nothing changed. */
        void refreshIfChanged();

        std::function<void (const juce::String&)> onProblem;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        // EditorTarget
        bool hasEditFocus() const override;
        bool deleteSelection() override;
        void selectAll() override;
        void deselectAll() override;
        void nudge (int direction) override;
        void copyNotes() override;
        void cutNotes() override;
        void pasteNotes() override;
        void duplicateNotes() override;
        bool handleKey (const juce::KeyPress&) override;

    private:
        Session& session;
        TimelineViewState& view;
        const int trackIndex, laneIndex;
        const Session::ParamRef param;
        ClickFocusTracker focus { *this };

        std::vector<Session::AutoPoint> points;
        std::set<int> selected, selectedBeforeBox;
        enum class Drag { none, points, curve, box } drag = Drag::none;
        std::vector<std::pair<int, Session::AutoPoint>> dragStart;   // the selected points as they were
        int downIndex = -1, curveIndex = -1, hover = -1;
        size_t pointsAtMouseDown = 0;    // an undo mid-drag changes the points: the drag stops
        bool focusShown = false;
        float curveStart = 0.0f;
        bool gestureStarted = false, moved = false;
        juce::Rectangle<int> box;
        juce::String drawnFor;   // what the points looked like when last drawn

        static constexpr int pad = 6, hitRadius = 6;

        void reload();
        juce::String describePoints() const;
        float valueToY (float) const;
        float yToValue (float) const;
        juce::Point<float> pointPos (int index) const;
        int pointAt (juce::Point<int>) const;
        double snapSeconds (double seconds, bool snap) const;
        double secondsAt (int x) const;
        int segmentAt (double seconds) const;
        void showMenu (int pointIndex, juce::Point<int>);
        void moveSelected (double dt, float dv);
        void report (bool ok);
        void updateTooltip (juce::Point<int>);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationLane)
    };

    //==============================================================================
    class AutomationLaneHeader : public juce::Component
    {
    public:
        AutomationLaneHeader (Session&, int trackIndex, int laneIndex, Session::ParamRef);

        /** The value at the playhead, and whether Clear can do anything. */
        void refresh();

        std::function<void (const juce::String&)> onProblem;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        Session& session;
        const int trackIndex, laneIndex;
        const Session::ParamRef param;
        juce::TextButton paramButton;
        IconButton hideButton { "Hide lane", Icon::close, {}, IconButton::Style::ghost };
        juce::TextButton clearButton { "Clear" };
        juce::String valueText;

        void showMenu();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationLaneHeader)
    };
}
