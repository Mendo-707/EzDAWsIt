/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    What the bottom panel's editors (piano roll, drum machine) offer the app's
    editing shortcuts. While you are working in one (you last clicked inside it),
    Delete, Ctrl+C/X/V/D/A, Esc, the arrows and its own keys go to it instead of
    the arrangement.
*/

#pragma once

#include "../../Core/Common.h"

namespace ez
{
    struct EditorTarget
    {
        virtual ~EditorTarget() = default;

        virtual bool hasEditFocus() const = 0;
        virtual bool deleteSelection() = 0;
        virtual void selectAll() = 0;
        virtual void deselectAll() = 0;      // Esc
        virtual void nudge (int direction) = 0;
        virtual void copyNotes() = 0;
        virtual void cutNotes() = 0;
        virtual void pasteNotes() = 0;
        virtual void duplicateNotes() = 0;

        /** Keys that have no app command (the editor's own). True if used. */
        virtual bool handleKey (const juce::KeyPress&) = 0;
    };

    /** Remembers whether the last click in the main window landed inside one component. */
    class ClickFocusTracker : private juce::MouseListener
    {
    public:
        explicit ClickFocusTracker (juce::Component& c) : owner (c)    { juce::Desktop::getInstance().addGlobalMouseListener (this); }
        ~ClickFocusTracker() override                                  { juce::Desktop::getInstance().removeGlobalMouseListener (this); }

        bool hasFocus() const noexcept      { return focused; }
        void setFocus (bool f) noexcept     { focused = f; }

    private:
        juce::Component& owner;
        bool focused = false;

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* c = e.eventComponent;

            // Clicks in other windows (a dropdown's popup, a dialog) don't move the focus:
            // picking a value in a list must not make Delete hit the arrangement
            if (c == nullptr || c->getTopLevelComponent() != owner.getTopLevelComponent())
                return;

            focused = c == &owner || owner.isParentOf (c);
        }

        JUCE_DECLARE_NON_COPYABLE (ClickFocusTracker)
    };
}
