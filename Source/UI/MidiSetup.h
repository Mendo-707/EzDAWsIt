/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Setup > MIDI & Controllers: which controllers are heard, MIDI clock out (a drum machine or
    groovebox follows the tempo, play and stop), and the knobs, faders and pads linked by MIDI learn.
*/

#pragma once

#include "../Engine/MidiLearn.h"
#include "Widgets.h"

namespace ez
{
    class MidiSetup : public juce::Component,
                      private juce::Timer
    {
    public:
        explicit MidiSetup (Session&);
        ~MidiSetup() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct LinkRow;

        Session& session;
        MidiLearn& learn;

        juce::Label inputsHeading, inputsList, clockHeading, clockHint, linksHeading, linksHint, learnStatus;
        juce::OwnedArray<juce::ToggleButton> clockButtons;
        juce::Array<juce::String> clockIDs;
        juce::ComboBox actionBox;
        juce::TextButton learnActionButton { "Learn" }, clearButton { "Remove all" };
        juce::Viewport linksViewport;
        juce::Component linksContent;
        juce::OwnedArray<LinkRow> linkRows;
        juce::String lastSignature;

        void timerCallback() override;
        void rebuild();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiSetup)
    };
}
