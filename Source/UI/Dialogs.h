/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Stand-alone windows: Setup (audio device, routing, plugins) and Export (modal while it's open).
*/

#pragma once

#include "SetupView.h"

namespace ez::Dialogs
{
    /** Opens Setup at a page, or brings the open one to the front and switches page. */
    void showSetup (AudioEngine&, Session&, juce::Component* associatedComponent, SetupView::Page);

    /** Opens the Export window (modal: the song can't change while it exports). */
    void showExport (Session&, juce::Component* associatedComponent);

    /** Closes every dialog opened here. Must be called before the AudioEngine is destroyed,
        because these windows hold references into it. */
    void closeAll();
}
