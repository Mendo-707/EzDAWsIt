/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    One Setup window for everything about the audio system:
      Audio device · Routing (headphones/speakers, track ins/outs) · Plugins
      · MIDI & Controllers · Recording
    A sidebar picks the page; Done (or Esc) closes it.
*/

#pragma once

#include "../Engine/Session.h"
#include "Widgets.h"

namespace ez
{
    class AudioEngine;

    class SetupView : public juce::Component
    {
    public:
        enum class Page { audioDevice, routing, plugins, midi, recording };

        SetupView (AudioEngine&, Session&, Page firstPage);
        ~SetupView() override;

        void showPage (Page);

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

    private:
        class PageRow;

        juce::OwnedArray<PageRow> rows;
        std::unique_ptr<juce::Component> audioPage, routingPage, pluginsPage, recordingPage, midiPage;
        IconButton doneButton { "Done", Icon::none, "Done", IconButton::Style::primary };
        juce::Rectangle<int> sidebarArea, footerArea;
        Page current = Page::audioDevice;

        juce::Component* getPageComponent (Page) const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetupView)
    };
}
