/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The title bars of our windows. Windows draws a white title bar by default, which glares above a dark
    app; pro apps either draw their own or tint the native one. We tint it (Windows 10 2004+ dark mode,
    Windows 11 caption, text and border colours from Theme), so snapping, the caption buttons and
    double-click to maximise keep working exactly as Windows does them.

    install() once at start-up: every window we open afterwards (dialogs, help, plug-in windows) is
    tinted as it appears. Elsewhere (Linux, older Windows) it does nothing.
*/

#pragma once

#include "../Core/Common.h"

namespace ez::WindowChrome
{
    void install();

    /** Tints one window now (install() does it for every window as it appears). True if the OS took it. */
    bool apply (juce::Component& window);

    /** The colours asked for, as 0x00BBGGRR (what Windows takes): caption, text, border. For the tests. */
    struct Colours { juce::uint32 caption, text, border; };
    Colours colours();
}
