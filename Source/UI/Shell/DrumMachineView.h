/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The drum machine: shown in the bottom panel for a track that has a Drum
    Machine (and for a MIDI clip on such a track). Three columns:
      Pads       4x4, pad 1 bottom-left (MPC / MC-707 style)
      Pad editor the selected pad's sound and values (hidden when narrow)
      Steps      one row per pad, editing the selected clip's notes directly

    Every control (AGENTS.md rule 0a):
    Toolbar     Kit, Genre (filters the kits), Pattern A-D, Steps, Length,
                Swing, Fill clip, New clip (no clip), Editor show/hide.
                Narrow: Swing, Length, Steps drop out (all are in the menus).
    Pads        click: select + play (higher on the pad = louder);
                double-click: load a sample; drop an audio file: load it;
                right-click: Load, Reset, Rename, Copy to pad, Choke,
                Mute, Solo, Reverse, Clear steps, Fill steps. Pads flash when hit.
    Pad editor  name (Enter applies, Esc reverts), waveform (click plays;
                shows start and decay), sound source and load errors,
                Tune / Decay / Start / Pan / Level knobs (drag, wheel,
                double-click resets, click the value to type), Choke,
                Mute, Solo, Reverse, Load..., Reset.
    Steps       click toggles; drag paints (one undo step); Shift+click
                toggles an accent (127); right-click a step: velocity,
                Fill every 1/2/4/8, Shift row, Clear row, Clear pattern,
                Copy pattern to, Fill clip; click a row name: select + play;
                M / S per row; wheel scrolls the rows; Shift+wheel, a sideways
                swipe or the bar under the steps scrolls along them when the
                steps don't fit (a step is never narrower than 20 px; the
                view follows the playing step); the playing step is lit.
    Velocity    stems for the selected pad; drag across them (one undo step).
    Keys        (after clicking in here) Up/Down: pad; Enter: play pad;
                Left/Right: shift the pad's row a step (Shift: every row);
                Delete: clear the pad's row; 1-4 or PageUp/PageDown: pattern;
                M / S: mute / solo the pad; Ctrl+C/X/V: copy/cut/paste the
                pattern; Ctrl+D: copy it to the next pattern; Esc: stop painting.

    Pattern A-D are consecutive parts of the clip, each one Length long.
*/

#pragma once

#include "../../Engine/Session.h"
#include "../Widgets.h"
#include "EditorTarget.h"

namespace ez
{
    class DrumMachineView : public juce::Component,
                            public EditorTarget,
                            public juce::FileDragAndDropTarget,
                            public juce::DragAndDropTarget,   // sounds from the browser
                            private Session::Listener,
                            private juce::ScrollBar::Listener,
                            private juce::Timer
    {
    public:
        explicit DrumMachineView (Session&);
        ~DrumMachineView() override;

        /** The track the panel is about (the timeline's highlighted track). */
        std::function<int()> getFocusedTrack;
        std::function<void (const juce::String& title, const juce::String& message)> onProblem;

        /** There is a drum machine to show (the selected clip's track, or the highlighted track, has one). */
        bool hasSomethingToShow() const;

        /** ...and a pattern clip to edit. */
        bool hasClip() const;

        // EditorTarget
        bool hasEditFocus() const override;
        bool deleteSelection() override;
        void selectAll() override       {}
        void deselectAll() override;
        void nudge (int direction) override;
        void copyNotes() override;
        void cutNotes() override;
        void pasteNotes() override;
        void duplicateNotes() override;
        bool handleKey (const juce::KeyPress&) override;

        void paint (juce::Graphics&) override;
        void resized() override;

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragMove (const juce::StringArray&, int x, int y) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int x, int y) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragMove (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

    private:
        class Pad;
        class PadEditor;
        class StepGrid;
        class VelocityLane;

        struct Target
        {
            DrumMachinePlugin* drums = nullptr;
            te::MidiClip* clip = nullptr;
        };

        struct PatternSettings
        {
            int page = 0, stepsIndex = 2, lengthIndex = 0, swingIndex = 0;
        };

        Session& session;
        ClickFocusTracker focus { *this };

        te::EditItemID drumsID, clipID;
        int selectedPad = 0;
        PatternSettings pattern;
        std::map<te::EditItemID, PatternSettings> settingsPerClip;
        std::vector<Session::DrumHit> clipboard;
        double clipboardBeats = 0.0;
        juce::int64 lastSignature = 0;
        bool songWasClosed = false;
        std::optional<bool> editorOverride;
        int dropPad = -1;

        // Children
        juce::OwnedArray<Pad> pads;
        std::unique_ptr<PadEditor> editor;
        juce::Viewport editorViewport;
        std::unique_ptr<StepGrid> steps;
        std::unique_ptr<VelocityLane> velocityLane;
        juce::ScrollBar rowScroll { true };
        juce::ScrollBar stepScroll { false };   // along the steps, when they don't fit

        /** Keeps the step scroll in range and the bar in step with it (after a resize or a steps change). */
        void updateStepScroll();
        void setStepScroll (int newScrollX);

        juce::ComboBox kitBox, genreBox, stepsBox, lengthBox, swingBox;
        juce::OwnedArray<IconButton> patternButtons;
        IconButton fillClipButton { "Fill clip", Icon::layers, "Fill clip" };
        IconButton newClipButton { "New pattern clip", Icon::plus, "New pattern clip" };
        IconButton editorButton { "Pad editor", Icon::sliders };
        std::unique_ptr<juce::FileChooser> chooser;

        struct Label { juce::String text; juce::Rectangle<int> bounds; };
        juce::Array<Label> toolbarLabels;
        juce::Rectangle<int> toolbarArea, padsArea, hintArea, sequencerArea, messageArea;

        // What is being edited
        Target resolveTarget() const;
        DrumMachinePlugin* getDrums() const;
        te::MidiClip* getClip() const;
        void retarget (const Target&);

        // The pattern on screen
        int getNumSteps() const;
        double getPatternBeats() const;
        double getPageStart() const     { return pattern.page * getPatternBeats(); }
        double getStepBeats() const     { return getPatternBeats() / getNumSteps(); }
        double getSwingAmount() const;
        double getStepStart (te::MidiClip&, int step) const;   // where a new hit goes (with swing)
        int getNumPages() const;
        void setPage (int);
        void setStepsIndex (int);
        void setLengthIndex (int);
        void setSwingIndex (int);
        void rememberPattern();

        // Actions
        void selectPad (int pad, bool play, int velocity = 100);
        void loadSampleWithChooser (int pad);
        void loadSample (int pad, const juce::File&);
        void showPadMenu (int pad);
        void showStepMenu (int pad, int step);
        void fillRow (int pad, int every);
        void shiftRow (int pad, int steps);   // pad -1 = every row
        void clearRow (int pad);              // pad -1 = every row
        void copyPatternTo (int page);
        void fillClip();
        void newClip();
        void toggleMute (int pad);
        void toggleSolo (int pad);
        void report (bool ok, const juce::String& title, const juce::String& message = {});
        void returnFocusToMainWindow();
        void finishTyping();   // a pad name being typed is applied to its own pad
        void refreshAll();
        void refreshToolbar();
        bool isEditorShown() const;
        int padAt (juce::Point<int>) const;   // in this component; -1 if none

        void sessionSongClosing() override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void timerCallback() override;
        juce::int64 makeSignature (const Target&) const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumMachineView)
    };
}
