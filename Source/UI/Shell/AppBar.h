/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The top row of the window:
      logo · New Open Save · song name | Arrange Mix Master | output · Setup · Export · Easy/Pro · Help
    The output box switches where you hear the song (headphones/speakers) in one click.
    Buttons trigger commands; planned features are greyed with their roadmap phase.
    When the window is narrow, the least important items hide first.
*/

#pragma once

#include "../../Engine/Session.h"
#include "../Widgets.h"

namespace ez
{
    class AudioEngine;

    class AppBar : public juce::Component,
                   private Session::Listener,
                   private juce::Timer
    {
    public:
        AppBar (AudioEngine&, Session&, juce::ApplicationCommandManager&);
        ~AppBar() override;

        /** Easy | Pro shows the current view (call when it changes). */
        void refreshViewMode();

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

    private:
        AudioEngine& audioEngine;
        Session& session;
        juce::ApplicationCommandManager& commandManager;

        IconButton newButton    { "New song", Icon::newFile, "New", IconButton::Style::ghost };
        IconButton openButton   { "Open song", Icon::open, "Open", IconButton::Style::ghost };
        IconButton saveButton   { "Save song", Icon::save, "Save", IconButton::Style::ghost };
        IconButton arrangeButton { "Arrange view", Icon::none, "Arrange" };
        IconButton mixButton     { "Mix view", Icon::none, "Mix" };
        IconButton masterButton  { "Master view", Icon::none, "Master" };
        juce::ComboBox outputBox;
        IconButton sizeButton   { "Interface size: make text and controls bigger or smaller (Ctrl+= bigger, Ctrl+- smaller, Ctrl+0 automatic)",
                                  Icon::none, "Aa", IconButton::Style::ghost };
        IconButton setupButton  { "Setup", Icon::sliders, "Setup" };
        IconButton exportButton { "Export", Icon::exportFile, "Export" };
        IconButton tunerButton  { "Tuner", Icon::none, "Tuner", IconButton::Style::ghost };
        IconButton easyButton   { "Easy view", Icon::none, "Easy" };
        IconButton proButton    { "Pro view", Icon::none, "Pro" };
        IconButton helpButton   { "Guided tour and help", Icon::help, {}, IconButton::Style::ghost };

        juce::Rectangle<int> logoArea, songNameArea, outputIconArea;
        juce::StringArray outputIDs;
        juce::String lastOutputSignature;

        void refreshOutputs();
        void showSizeMenu();
        void sessionSongChanged() override      { repaint(); }
        void sessionStateChanged() override     { outputBox.setEnabled (! session.isRecording()); }
        void timerCallback() override;          // devices can come and go; Saved / Edited
        void mouseDown (const juce::MouseEvent&) override;   // the song name: Save as, recent songs, show in folder
        bool lastUnsaved = false;

    public:
        /** The song menu: Save as new song, recent songs, show the folder (also Ctrl+Alt+O). */
        void showSongMenu();
        std::function<void (const juce::File&)> onOpenSong;   // a recent song was picked
    private:

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppBar)
    };
}
