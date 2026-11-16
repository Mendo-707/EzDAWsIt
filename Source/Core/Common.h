/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Common include for all EzDAWsIt code. Every .cpp includes this first.
    The juce namespace is NOT pulled in globally (DONT_SET_USING_JUCE_NAMESPACE),
    so our code always writes juce:: explicitly.
*/

#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

namespace ez
{
    /** Writes a line to the EzDAWsIt log file (Documents\EzDAWsIt\Logs). */
    inline void log (const juce::String& message)
    {
        juce::Logger::writeToLog (message);
    }
}
