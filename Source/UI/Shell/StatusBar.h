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
                      public juce::TooltipClient,
                      private juce::Timer
    {
    public:
        StatusBar (AudioEngine&, Session&);

        /** The delay readout's menu runs these commands (low-latency monitoring, audio setup). */
        void setCommands (juce::ApplicationCommandManager* c)    { commands = c; }

        /** "Delay 9.8 ms" for a monitoring delay, and its colour: amber over 12 ms, red over 25. */
        static juce::String describeDelay (const Session::MonitoringDelay&);
        static juce::Colour colourForDelay (const Session::MonitoringDelay&);
        static juce::String explainDelay (const Session::MonitoringDelay&, bool lowLatencyOn);

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        juce::String getTooltip() override;

    private:
        AudioEngine& audioEngine;
        Session& session;
        juce::String message, device, cpu, delayText, delayTip;
        juce::Colour delayColour;
        juce::Rectangle<int> delayArea;
        juce::ApplicationCommandManager* commands = nullptr;
        int tick = 0;

        juce::String describeState() const;
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusBar)
    };
}
