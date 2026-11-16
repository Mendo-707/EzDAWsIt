/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The Master view (docs/ui/mockup-generator.py, master_view): final polish for the whole song.

      Mastering chain  [on]  [preset ▼]                     (Ctrl+Shift+A: chain on / off)
      [EQ] → [Multiband] → [Stereo] → [Limiter] → [Dither]   each with its own on switch
      ┌ Loudness ─────────────────────────────┐ ┌ Reference A/B ──────┐
      │ Spotify Apple YouTube Club CD          │ │ file · LUFS   Load  │
      │ INTEGRATED SHORT-TERM TRUE PEAK RANGE  │ │ [A: My mix|B: Ref]  │  (Ctrl+Shift+F)
      │ status ............ [Match target]     │ │ [x] match loudness  │
      │ loudness over the song (target line)   │ ├ Master assistant P5 ┤
      └────────────────────────────────────────┘ └─────────────────────┘
      Main output [▼]   Setup   Export...

    Knobs: drag (one undo step), double-click for the default, right-click for a menu.
    Everything here goes through Session (undoable where it changes the song).
*/

#pragma once

#include "../Engine/Session.h"
#include "Widgets.h"

namespace ez
{
    class AudioEngine;

    class MasterView : public juce::Component,
                       private Session::Listener,
                       private juce::Timer
    {
    public:
        MasterView (AudioEngine&, Session&, juce::ApplicationCommandManager&);
        ~MasterView() override;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

        std::function<void (const juce::String& title, const juce::String& message)> onProblem;

        /** Brings the controls up to date (also after an undo). */
        void refresh();

    private:
        class Knob;
        class Card;
        class ChainRow;
        class LoudnessPanel;
        class ReferencePanel;

        AudioEngine& audioEngine;
        Session& session;
        juce::ApplicationCommandManager& commands;

        IconButton chainButton { "Mastering chain on / off", Icon::power, "Chain on", IconButton::Style::normal };
        juce::ComboBox presetBox;
        juce::Viewport chainViewport;
        std::unique_ptr<ChainRow> chainRow;
        std::unique_ptr<LoudnessPanel> loudness;
        std::unique_ptr<ReferencePanel> reference;

        juce::ComboBox outputBox;
        juce::StringArray outputIDs;
        juce::String lastOutputSignature;
        IconButton setupButton { "Setup", Icon::sliders, "Setup" };
        IconButton exportButton { "Export", Icon::exportFile, "Export...", IconButton::Style::primary };

        juce::Rectangle<int> headerArea, chainTitleArea, assistantArea, outputArea;
        Session::MasterAdvice advice;
        juce::TextButton adviceButton;
        int outputTicks = 0;

        void report (bool ok, const juce::String& title);
        void refreshOutputs();

        void sessionSongChanged() override;
        void sessionStateChanged() override     { refresh(); }
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterView)
    };
}
