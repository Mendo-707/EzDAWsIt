/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The sections lane: the song's parts (Intro, Verse, Hook...) as coloured
    blocks above the tempo lane. Arrange the song by its sections.

    Controls (AGENTS.md rule 0a):
      Click a section           select it and move the playhead to its start
      Double-click a section    rename it (Enter applies, Esc cancels)
      Drag a section            move it (snaps to bars; Alt: to beats)
      Drag its left/right edge  make it start/end elsewhere
      Drag the repeat handle    (the ⧉ at its right end) repeat it: drag right one length per copy
      Double-click empty        add a section from that bar (pick its name)
      Right-click empty         add a section here (Intro, Verse, Pre-hook, Hook, Bridge, Break, Drop, Outro)
      Right-click a section     rename, colour, loop it, select its clips, repeat it,
                                delete it (keep the music), delete it with its music
      Delete / Backspace        delete the selected section (keeps the music), after clicking in the lane
      Esc                       cancel a drag
      Wheel                     scrolls the song like the ruler
*/

#pragma once

#include "../../Engine/Session.h"
#include "TimelineViewState.h"
#include "../Shell/EditorTarget.h"

namespace ez
{
    class SectionsLane : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        SectionsLane (Session&, TimelineViewState&);

        std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;
        std::function<void (const juce::String& title, const juce::String& message)> onProblem;

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;

        static const juce::StringArray& getSectionNames();

    private:
        enum class Mode { none, move, resizeStart, resizeEnd, repeat };

        Session& session;
        TimelineViewState& view;
        ClickFocusTracker lastClick { *this };   // keys only act after a click in here
        te::EditItemID selected, hovered;
        Mode mode = Mode::none;
        std::optional<Session::Section> dragged;
        double previewStart = 0.0, previewEnd = 0.0;
        int repeatCount = 0;
        juce::TextEditor nameEditor;
        te::EditItemID renaming;

        int xForBeat (double beat) const;
        double beatAtX (int x) const;
        double snap (double beat, bool toBeat) const;
        std::optional<Session::Section> sectionAt (int x) const;
        juce::Rectangle<int> blockFor (const Session::Section&) const;
        juce::Rectangle<int> repeatHandleFor (const Session::Section&) const;
        Mode modeAt (const Session::Section&, int x) const;
        void showEmptyMenu (double beat);
        void showSectionMenu (const Session::Section&);
        void addSectionAt (double beat, const juce::String& name);
        void startRenaming (const Session::Section&);
        void commitRename();
        void returnFocus();
        void report (bool ok, const juce::String& title);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionsLane)
    };
}
