/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    A window with a knob (or a switch) for every control of a plugin that has no editor of its
    own: Tracktion's EQ, compressor, reverb, delay... and all of ours (Effects.h). "Open" on a
    device card, a mixer insert or a track's FX menu shows it.

      Knob            drag (Shift: fine), wheel, double-click: back to its default;
                      right-click: default, and the exact value to type in
      Switch          pick from the list (Type, Key, Mode...)
      On              bypass the whole effect (an undo step)
      Esc             closes the window
      Automated       a control with a lane follows it (dimmed) unless Write is on while playing;
                      drags with Write on record into the lane
*/

#pragma once

#include "../Core/Common.h"

namespace ez
{
    namespace ParamEditor
    {
        /** The window for a plugin without its own editor (te::UIBehaviour::createPluginWindow). */
        std::unique_ptr<juce::Component> createWindow (te::Plugin&);

        /** Just the controls, to put in a window or a panel. */
        std::unique_ptr<juce::Component> createContent (te::Plugin&);
    }
}
