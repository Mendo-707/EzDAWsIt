/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The highlighted track's devices, shown in the bottom panel:
      left:  the track's name, input and output
      right: its chain, left to right as the sound flows:
             instrument → effects (bypass, move, open, remove) → Add effect → volume & pan
    Rebuilds itself when the track, its effects or the routing change.
*/

#pragma once

#include "../../Engine/Session.h"
#include "../Widgets.h"

namespace ez
{
    class DevicesView : public juce::Component,
                        private Session::Listener,
                        private juce::Timer
    {
    public:
        explicit DevicesView (Session&);
        ~DevicesView() override;

        std::function<int()> getFocusedTrack;
        std::function<void (const juce::String& title, const juce::String& message)> onProblem;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class DeviceCard;
        class ChainContent;

        Session& session;
        juce::Viewport chainViewport;
        std::unique_ptr<ChainContent> chain;
        juce::ComboBox inputBox, outputBox;
        juce::Array<Session::RoutingChoice> inputChoices, outputChoices;
        juce::Rectangle<int> inspectorArea, nameArea, typeArea, inputLabelArea, outputLabelArea;

        int track = -1;
        juce::String lastSignature;
        bool checkSoon = true;
        int ticks = 0;

        juce::String makeSignature() const;
        void rebuild();
        void refreshRouting();
        void showAddEffectMenu (juce::Component& anchor);
        void report (bool ok, const juce::String& title);

        void sessionSongChanged() override      { checkSoon = true; }
        void sessionStateChanged() override     { checkSoon = true; }   // never rebuild inside a card's own click
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevicesView)
    };
}
