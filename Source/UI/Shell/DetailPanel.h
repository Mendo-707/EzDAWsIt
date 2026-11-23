/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The bottom panel: the editor for whatever is highlighted.
      Devices      the track's instrument and effects (DevicesView)
      Clip editor  the piano roll for the selected MIDI clip (PianoRollView), or the
                   drum machine (DrumMachineView) for a track with a Drum Machine.
                   A drum clip can be seen either way: Steps / Notes in the header.
    Collapsed it is just its header. Open it with the chevron, a tab, D or E;
    drag the header to make it taller or shorter. Selecting a MIDI clip while
    the panel is open switches to the clip editor.
*/

#pragma once

#include "../../Engine/Session.h"
#include "../Widgets.h"
#include "EditorTarget.h"

namespace ez
{
    class DevicesView;
    class PianoRollView;
    class DrumMachineView;

    class DetailPanel : public juce::Component,
                        private Session::Listener,
                        private juce::Timer
    {
    public:
        explicit DetailPanel (Session&);
        ~DetailPanel() override;

        /** The track the panel is about (the timeline's highlighted track). */
        void setFocusedTrackSource (std::function<int()>);
        void setProblemHandler (std::function<void (const juce::String&, const juce::String&)>);

        /** Called when the panel wants a different height (opened, closed, dragged). */
        std::function<void()> onHeightChanged;

        enum class Tab { devices, clipEditor };

        bool isOpen() const noexcept        { return open; }
        void setOpen (bool);
        Tab getTab() const noexcept         { return tab; }

        /** Opens the panel on the clip editor (a MIDI clip was double-clicked). */
        void showClipEditor()               { showTab (Tab::clipEditor); setOpen (true); }

        /** Opens the panel on that tab, or closes it if it is already showing that tab. */
        void toggleTab (Tab);

        /** The piano roll, for routing editing shortcuts (Delete, Ctrl+A, arrows, Esc). */
        PianoRollView& getPianoRoll() noexcept     { return *pianoRoll; }

        /** True while the user is working on notes: editing shortcuts then go to the piano roll. */
        bool pianoRollHasEditFocus() const;

        /** The editor the user is working in (last clicked inside it), if any: editing shortcuts go there. */
        EditorTarget* getFocusedEditor() const;
        int getPreferredHeight() const;

        /** Until the user drags the panel's edge, its height follows the space there is: a big screen gets a
            roomier editor (pads, knobs and a full inspector), the minimum window keeps the timeline usable. */
        void fitDefaultHeightTo (int mainAreaHeight);

        /** What dragging the top edge does: the body becomes this tall (within limits) and stops following the window. */
        void resizeBodyTo (int bodyHeight);

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        Session& session;
        std::function<int()> getFocusedTrack;
        std::unique_ptr<DevicesView> devices;
        std::unique_ptr<PianoRollView> pianoRoll;
        std::unique_ptr<DrumMachineView> drums;

        IconButton expandButton  { "Open or close the panel", Icon::chevronUp, {}, IconButton::Style::ghost };
        IconButton devicesTab    { "Devices", Icon::none, "Devices", IconButton::Style::ghost };
        IconButton clipEditorTab { "Clip editor", Icon::none, "Clip editor", IconButton::Style::ghost };
        IconButton stepsView     { "Steps", Icon::drum, "Steps", IconButton::Style::ghost };
        IconButton notesView     { "Notes", Icon::piano, "Notes", IconButton::Style::ghost };
        bool drumNotes = false;   // a drum clip shown in the piano roll
        bool showingDrums = false, lastDrumClip = false;

        bool open = false;
        Tab tab = Tab::devices;
        te::EditItemID lastSelectedMidiClip;
        int bodyHeight = 230, bodyHeightAtDragStart = 0;
        bool bodySizedByUser = false;
        juce::Rectangle<int> headerArea, contextArea, hintArea;
        juce::String trackName;
        juce::Colour trackColour;

        void showTab (Tab);
        void sessionSongClosing() override      { lastSelectedMidiClip = {}; }
        void updateChildren();
        bool shouldShowDrums() const;
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DetailPanel)
    };
}
