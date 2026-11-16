/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The piano roll: edits the notes of the selected MIDI clip, in the bottom panel.

    Tools (toolbar, keys 1 / 2 / 3, or the right-click menu)
      Draw     click empty space to add a note (drag sets its length). Shift+drag box-selects.
      Select   click / drag to select and move. Double-click empty space adds a note.
      Erase    click, or drag across notes, to delete them (one undo step)

    Mouse
      Click / Shift+click       select / add to or remove from the selection
      Drag empty space          box-select (Select tool)
      Drag a note               move every selected note (time and pitch)
      Ctrl+drag a note          copy the selected notes to where you drop them
      Drag a note's edge        make the selected notes longer or shorter (left edge moves the start)
      Hold Alt while dragging   ignore the grid for that drag
      Double-click a note       delete it (Select tool)
      Right-click               menu with every action in here
      Middle-drag               pan
      Wheel / Shift / Ctrl / Alt   scroll pitch / scroll time / zoom time / zoom pitch
      Click or drag the ruler   move the playhead
      Velocity lane (bottom)    drag across the stems to set how hard notes are played
      Keyboard (left)           click to hear a note

    Keys (while you're working in here)
      Delete, Ctrl+A, Esc (cancels a drag, then deselects), Ctrl+C / X / V / D
      Left / Right nudge by the grid, Shift+Left / Right by a beat
      Up / Down transpose a semitone, Shift+Up / Down an octave
      Q quantise, F fit the view to the notes, 1 / 2 / 3 tools

    The inspector (left) shows and edits the clip and the selected notes. In a narrow panel the
    inspector hides itself (the toolbar button brings it back) and low-priority toolbar items drop
    out: everything that drops out is still in the right-click menu.

    Reads the clip directly to draw; every change goes through Session.
*/

#pragma once

#include "../../Engine/Session.h"
#include "../Widgets.h"
#include "EditorTarget.h"

namespace ez
{
    class PianoRollView : public juce::Component,
                          public EditorTarget,
                          private juce::ScrollBar::Listener,
                          private Session::Listener,
                          private juce::Timer
    {
    public:
        explicit PianoRollView (Session&);
        ~PianoRollView() override;

        std::function<void (const juce::String& title, const juce::String& message)> onProblem;

        /** True while the user's last click was in here: editing shortcuts then act on notes. */
        bool hasEditFocus() const override;

        //==============================================================================
        // Everything the keys, the menu and the toolbar can do. Each is also reachable from the mouse.
        bool deleteSelection() override;
        void selectAll() override;
        void deselectAll() override;         // Esc: cancels a drag in progress, otherwise deselects
        void invertSelection();
        void nudge (int direction) override;                 // by the grid
        void copyNotes() override;
        void cutNotes() override;
        void pasteNotes() override;
        void duplicateNotes() override;
        void transpose (int semitones);
        void quantiseNotes();
        void legatoNotes();
        void fitView();

        enum class Tool { draw, select, erase };
        void setTool (Tool);
        Tool getTool() const noexcept               { return tool; }

        /** Keys that aren't global commands (arrows, Q, F, 1-3). True if it used the key. */
        bool handleKey (const juce::KeyPress&) override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class Keyboard;
        class Grid;
        class Ruler;
        class VelocityLane;
        class Inspector;
        friend class Keyboard;
        friend class Grid;
        friend class Ruler;
        friend class VelocityLane;
        friend class Inspector;

        Session& session;

        // What is shown
        te::EditItemID clipID;
        Session::NoteList selected;
        juce::int64 lastSignature = 0;
        bool songWasClosed = false;

        // View
        double viewStartBeat = 0.0, pixelsPerBeat = 48.0;
        int rowHeight = 14, scrollY = 0;
        double gridBeats = 0.25, newNoteBeats = 0.25;
        int newNoteVelocity = 100;
        bool snapEnabled = true;
        int scaleRoot = 0, scaleType = 0;   // type: 0 off, 1 major, 2 minor
        bool highlightScale = true, foldToScale = false, followPlayhead = false;
        double quantiseStrength = 1.0, quantiseSwing = 0.0;
        Tool tool = Tool::draw;
        std::optional<bool> inspectorOverride;   // the user's choice; otherwise it follows the width
        double lastPlayheadBeat = -1.0;

        // The keyboard's rows, top to bottom. All 128 pitches, or with Fold only the scale's pitches
        // (plus any pitch that already has a note, so notes never disappear)
        juce::Array<int> rowPitches;
        int pitchRow[128] {};

        std::unique_ptr<Keyboard> keyboard;
        std::unique_ptr<Grid> grid;
        std::unique_ptr<Ruler> ruler;
        std::unique_ptr<VelocityLane> velocityLane;
        std::unique_ptr<Inspector> inspector;
        ClickFocusTracker focusWatcher { *this };
        juce::Viewport inspectorViewport;
        juce::ScrollBar horizontalScroll { false }, verticalScroll { true };

        // Toolbar
        IconButton drawTool       { "Draw tool", Icon::none, "Draw" };
        IconButton selectTool     { "Select tool", Icon::none, "Select" };
        IconButton eraseTool      { "Erase tool", Icon::none, "Erase" };
        juce::ComboBox gridBox, strengthBox, swingBox;
        IconButton snapButton     { "Snap notes to the grid", Icon::magnet, "Snap" };
        IconButton quantiseButton { "Quantise", Icon::wand, "Quantise" };
        IconButton followButton   { "Follow the playhead", Icon::returnToStart, {}, IconButton::Style::ghost };
        IconButton fitButton      { "Fit the view to the notes", Icon::search, {}, IconButton::Style::ghost };
        IconButton zoomOutButton  { "Zoom out", Icon::zoomOut, {}, IconButton::Style::ghost };
        IconButton zoomInButton   { "Zoom in", Icon::zoomIn, {}, IconButton::Style::ghost };
        IconButton inspectorButton { "Show or hide the inspector", Icon::sliders, {}, IconButton::Style::ghost };

        // Inspector controls whose state the toolbar and menu share
        juce::ComboBox scaleRootBox, scaleTypeBox;
        IconButton followKeyButton { "Follow the song's key", Icon::none, "Follow song key" };
        IconButton highlightButton { "Highlight the scale", Icon::none, "Highlight scale" };
        IconButton foldButton      { "Fold to the scale", Icon::none, "Fold to scale" };

        struct ToolbarLabel { juce::String text; juce::Rectangle<int> bounds; };
        juce::Array<ToolbarLabel> toolbarLabels;
        juce::Rectangle<int> toolbarArea, titleArea, velocityLabelArea, rulerCorner;

        //==============================================================================
        te::MidiClip* getClip() const;
        void showClip (te::MidiClip*);
        void pruneSelection();
        juce::int64 makeSignature (te::MidiClip&) const;
        void report (bool ok, const juce::String& title);
        void returnFocusToMainWindow();

        // Rows <-> pitches
        int rowCount() const                        { return rowPitches.size(); }
        int rowAt (int y) const                     { return (int) std::floor ((double) (y + scrollY) / juce::jmax (1, rowHeight)); }
        int pitchToRow (int pitch) const            { return pitchRow[juce::jlimit (0, 127, pitch)]; }
        int pitchToY (int pitch) const              { return pitchToRow (pitch) * rowHeight - scrollY; }
        int yToPitch (int y) const                  { return rowPitches.getUnchecked (juce::jlimit (0, rowCount() - 1, rowAt (y))); }
        void rebuildRows();

        double beatToX (double beat) const          { return (beat - viewStartBeat) * pixelsPerBeat; }
        double xToBeat (double x) const             { return viewStartBeat + x / pixelsPerBeat; }
        double snapBeat (te::MidiClip&, double contentBeat, bool floorIt, bool bypassSnap = false) const;
        bool isInScale (int pitch) const;
        bool isInspectorShown() const;

        void setGridIndex (int index);
        void setStrengthIndex (int index);
        void setSwingIndex (int index);
        void setFollowSongKey (bool);
        void applySongKey();
        bool followSongKey = true;
        void setScaleRoot (int root);
        void setScaleType (int type);
        void setHighlightScale (bool);
        void setFoldToScale (bool);
        void setFollowPlayhead (bool);
        void setSnap (bool);
        void pasteAt (double contentBeat);
        void setSelectedVelocity (int velocity);
        void setSelectedLengthToGrid();
        void showContextMenu (double contentBeatAtClick);
        void handleMenuResult (int result, double contentBeatAtClick);

        void zoomTime (double factor, double anchorX);
        void setRowHeight (int newHeight, int anchorY);
        void clampView();
        void updateScrollBars();
        void repaintAll();
        void layoutToolbar();

        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void sessionSongClosing() override;
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollView)
    };
}
