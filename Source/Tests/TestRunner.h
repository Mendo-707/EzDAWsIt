/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Headless self-tests. Run with:  EzDAWsIt.exe --run-tests <results-file>
    (test.bat does this). Tests never touch the user's real songs: every test
    redirects the app folders to a temporary folder.
*/

#pragma once

#include "../Core/Common.h"

namespace ez::Tests
{
    constexpr const char* commandLineFlag = "--run-tests";
    constexpr const char* category = "EzDAWsIt";

    /** Runs every EzDAWsIt test, writes a readable report, returns the number of failures. */
    int runAll (const juce::File& resultsFile);
}
