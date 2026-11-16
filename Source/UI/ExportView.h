/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The Export window (docs/ui/mockup-generator.py, export_dialog), titled "Export song":

      Export presets  |  What to export   Full mix / Stems / Bus stems / MIDI
      (click one)     |  Range            Whole song / Loop / Selection
                      |  Format           WAV FLAC OGG MP3 · sample rate · bit depth · dither
                      |  Loudness         match a target (LUFS), true peak under -1 dB
                      |  Save to          file name · folder (Browse, Open folder)
      ------------------------------------------------------------------------------
      About 42 MB · 3:42                                   [Cancel]  [Export]

    While it runs: a progress bar and what's rendering; Esc or Cancel stops it. When it's done:
    how loud the file came out, and Show in folder. Keys: Enter exports, Esc cancels / closes,
    1-6 pick a preset.
*/

#pragma once

#include "../Engine/Session.h"
#include "Widgets.h"

namespace ez
{
    class ExportView : public juce::Component,
                       private juce::Timer
    {
    public:
        explicit ExportView (Session&);
        ~ExportView() override;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        static constexpr int preferredWidth = 880, preferredHeight = 640;

    private:
        class Choice;
        class Body;

        Session& session;
        Session::ExportSettings settings;
        int presetIndex = 0;              // -1 once something is changed by hand
        bool nameIsAutomatic = true;      // follows the song name until typed over
        juce::String note;                // a one-line note in the footer (MP3 swapped for Ogg, ...)

        juce::OwnedArray<Choice> presetRows;
        juce::Array<Session::ExportPreset> allPresets;   // the built-ins, then yours
        int numBuiltIn = 0;
        bool rightClicked = false;   // a right-click opens the menu: it doesn't pick the preset
        IconButton savePresetButton { "Save as preset", Icon::bookmark, "Save as preset" };

        std::unique_ptr<Body> body;
        juce::Viewport bodyViewport;

        double progress = 0.0;
        juce::ProgressBar progressBar { progress };
        IconButton showButton { "Show in folder", Icon::open, "Show in folder" };
        IconButton cancelButton { "Close", Icon::none, "Close" };
        IconButton exportButton { "Export", Icon::exportFile, "Export", IconButton::Style::primary };
        juce::Rectangle<int> footerText;
        Session::ExportStatus::State lastState = Session::ExportStatus::State::idle;
        std::unique_ptr<juce::FileChooser> chooser;

        void applyPreset (int index);
        void rebuildPresets();
        void saveAsPreset();
        void mouseDown (const juce::MouseEvent&) override;   // right-click one of your presets: delete it
        void changedByHand (bool deselectPreset = true);
        void refreshControls();
        juce::String footerMessage();
        void startExport();
        void startExportNow();
        void cancelOrClose();
        void chooseFolder();
        void timerCallback() override;
        bool isRunning();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportView)
    };
}
