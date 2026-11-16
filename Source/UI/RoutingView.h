/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    One place for all audio routing:
      - where you hear the song (main output), with a test beep per output
      - each track's input and output
    Rebuilds itself whenever outputs, inputs or tracks change.
*/

#pragma once

#include "../Engine/AudioEngine.h"
#include "../Engine/Session.h"

namespace ez
{
    class RoutingView : public juce::Component,
                        private Session::Listener,
                        private juce::Timer
    {
    public:
        RoutingView (AudioEngine&, Session&);
        ~RoutingView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct OutputRow;
        struct TrackRow;

        AudioEngine& audioEngine;
        Session& session;

        juce::Viewport viewport;
        juce::Component content;

        juce::Label mainHeading, mainLabel, outputsHeading, tracksHeading, hint, status;
        juce::ComboBox mainOutputBox;
        juce::TextButton testMainButton { "Test" };
        juce::OwnedArray<OutputRow> outputRows;
        juce::OwnedArray<TrackRow> trackRows;

        juce::Array<AudioEngine::Output> outputs;
        juce::String lastSignature;

        void rebuild();
        juce::String makeSignature();
        void layoutContent();
        void showStatus (const juce::String& message, bool isError);

        void sessionSongChanged() override      { rebuild(); }
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingView)
    };
}
