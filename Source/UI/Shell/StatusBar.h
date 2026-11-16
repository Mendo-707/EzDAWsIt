/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The bottom line: what's selected (with the useful shortcuts for it),
    the audio device and its latency, and CPU load.
*/

#pragma once

#include "../../Engine/Session.h"

namespace ez
{
    class AudioEngine;

    class StatusBar : public juce::Component,
                      private juce::Timer
    {
    public:
        StatusBar (AudioEngine&, Session&);

        void paint (juce::Graphics&) override;

    private:
        AudioEngine& audioEngine;
        Session& session;
        juce::String message, device, cpu;
        int tick = 0;

        juce::String describeState() const;
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusBar)
    };
}
