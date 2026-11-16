/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Bars/beats ruler above the tracks.
      Top half:    click or drag to move the playhead.
      Bottom half: drag to set the loop region, double-click to turn looping on/off.
      Right-click: start/end the loop here, loop on/off.
*/

#pragma once

#include "../../Engine/Session.h"
#include "TimelineViewState.h"

namespace ez
{
    class Ruler : public juce::Component,
                  public juce::SettableTooltipClient
    {
    public:
        Ruler (Session&, TimelineViewState&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        Session& session;
        TimelineViewState& view;
        bool draggingLoop = false;
        te::TimePosition loopDragStart;

        te::TimePosition snappedTimeAt (int x) const;
        void showMenu (te::TimePosition);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Ruler)
    };
}
