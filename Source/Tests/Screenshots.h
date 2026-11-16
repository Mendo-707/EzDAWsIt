/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Developer tool: renders every main view of a demo song to PNG files, so a change to the
    look can be checked at both window sizes without clicking through the app.
        EzDAWsIt.exe --screenshots <folder>
    Like the tests, it never touches the user's songs or settings.
*/

#pragma once

#include "../Core/Common.h"

namespace ez::Screenshots
{
    constexpr const char* commandLineFlag = "--screenshots";

    /** Writes the PNGs into the folder. Returns the number of problems (0 = all written). */
    int run (const juce::File& folder);
}
