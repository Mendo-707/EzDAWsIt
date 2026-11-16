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

    private:
        std::unique_ptr<juce::FileLogger> logger;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Diagnostics)
    };
}
