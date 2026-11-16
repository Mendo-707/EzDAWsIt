/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Interface size: everything (text, buttons, panels) bigger or smaller, like a browser's zoom.
    "Automatic" picks a size from the screen: a big monitor at 100% Windows scaling gets 125-150%,
    so the 12 px text of the design doesn't shrink to a speck at 2560 px wide.

      Ctrl+=  bigger    Ctrl+-  smaller    Ctrl+0  automatic    (also the size button in the top bar)

    Saved in the app's settings (not the song).
*/

#pragma once

#include "../Core/Common.h"

namespace ez::InterfaceScale
{
    /** The sizes offered, as factors (1.0 = 100%). */
    const juce::Array<float>& steps();

    /** The saved setting: 0 = automatic. */
    float getSetting (te::Engine&);

    /** The biggest offered size that still fits the smallest window on this screen. */
    float largest();

    /** Shrinks and moves every open window (title bar included) back onto its screen. */
    void fitWindows();

    /** What automatic means on this screen. */
    float automatic();

    /** The size in use now. */
    float current();

    /** Saves and applies a setting (0 = automatic), then keeps every window on screen. */
    void apply (te::Engine&, float setting);

    /** One step bigger (+1) or smaller (-1) from the size in use. */
    void step (te::Engine&, int direction);

    /** At start-up, before any window opens. */
    void applySaved (te::Engine&);

    /** "125%" or "Automatic (125%)". */
    juce::String describe (float setting);
}
