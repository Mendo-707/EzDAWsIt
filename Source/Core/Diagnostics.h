/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Logging and crash reporting. Create one Diagnostics object at startup
    and keep it alive until the very end of shutdown.
*/

#pragma once

#include "Common.h"

namespace ez
{
    class Diagnostics
    {
    public:
        Diagnostics (const juce::String& appName, const juce::String& appVersion);
        ~Diagnostics();

        juce::File getLogFile() const;

        /** The log of the app's last run, if that run didn't shut down cleanly (it crashed, hung and
            was ended, or the PC lost power); else {}. Only the app's own runs count, not tests. */
        juce::File getUncleanPreviousLog() const        { return uncleanPreviousLog; }
        bool previousRunCrashed() const                 { return previousCrashed; }

        /** Reads a log: was it written by the app (not tests or tools), did it end cleanly, did it crash? */
        struct LogSummary { bool isAppRun = false, endedCleanly = false, crashed = false; };
        static LogSummary summarise (const juce::File& log, const juce::String& appName);

        static constexpr const char* cleanShutdownLine = "Clean shutdown";
        static constexpr const char* crashLine = "*** CRASH ***";

    private:
        std::unique_ptr<juce::FileLogger> logger;
        juce::File uncleanPreviousLog;
        bool previousCrashed = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Diagnostics)
    };
}
