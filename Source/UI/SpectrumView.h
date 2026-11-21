/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The main output's spectrum (Master view): what frequencies the mix has, live. Reads the meter
    bank's bands (Engine/Meters); draws them as a soft filled curve over a log frequency grid.
*/

#pragma once

#include "../Engine/Session.h"

namespace ez
{
    class SpectrumView : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::Timer
    {
    public:
        explicit SpectrumView (Session&);

        void paint (juce::Graphics&) override;
        void visibilityChanged() override;

    private:
        Session& session;
        std::vector<float> lastDrawn;

        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
    };
}
