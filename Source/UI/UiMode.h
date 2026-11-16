/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Easy or Pro view (the App bar's Easy | Pro, Ctrl+Shift+P). Easy keeps the screen to what most
    songs need; Pro adds the routing and inserts rows in the mixer, punch and MIDI sync in the
    transport. Nothing is switched off: every feature still works and has its shortcut.
    Saved for next time.
*/

#pragma once

#include "../Core/Common.h"

namespace ez::UiMode
{
    void initialise (te::Engine&);         // reads the saved choice (Pro for anyone who never chose)
    bool isPro();
    void setPro (bool);                    // saves it and tells every listener
    juce::ChangeBroadcaster& getBroadcaster();
}
