/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The main window's contents. Owns the command manager: every action
    (button, shortcut, later MIDI) is handled in perform().

    Layout (the "shell", docs/ui/DESIGN-NOTES.md):
      app bar / transport / [browser | timeline over detail panel] / status bar
*/

#pragma once

#include <deque>

#include "../Engine/Session.h"
#include "Timeline/TimelineView.h"
#include "MixerView.h"
#include "Shell/AppBar.h"
#include "Shell/BrowserPanel.h"
#include "Shell/DetailPanel.h"
#include "MasterView.h"
#include "Shell/StatusBar.h"
#include "Shell/TransportBar.h"

namespace ez
{
    class AudioEngine;

    class MainComponent : public juce::Component,
                          public juce::DragAndDropContainer,       // sounds dragged from the browser
                          public juce::ApplicationCommandTarget,
                          private Session::Listener
    {
    public:
        MainComponent (AudioEngine&, Session&);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

        // ApplicationCommandTarget
        juce::ApplicationCommandTarget* getNextCommandTarget() override  { return nullptr; }
        void getAllCommands (juce::Array<juce::CommandID>&) override;
        void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
        bool perform (const InvocationInfo&) override;
        bool keyPressed (const juce::KeyPress&) override;   // piano roll keys that have no command (Up/Down, 1-3, Q, F)

    private:
        AudioEngine& audioEngine;
        Session& session;

        juce::ApplicationCommandManager commandManager;
        juce::TooltipWindow tooltipWindow { this };

        std::unique_ptr<AppBar> appBar;
        std::unique_ptr<TransportBar> transportBar;
        std::unique_ptr<BrowserPanel> browser;
        TimelineView timeline;
        std::unique_ptr<MixerView> mixer;
        std::unique_ptr<MasterView> masterView;
        juce::Viewport masterScroller;   // the Master view scrolls when the window is short
        enum class View { arrange, mix, master };
        View view = View::arrange;
        bool mixShown = false;   // view == mix (kept as a flag: many places ask)
        void showView (View);
        void showMix (bool shouldShow)              { showView (shouldShow ? View::mix : View::arrange); }
        bool arrangeTakesKeys() const noexcept      { return view == View::arrange; }   // the hidden arrange never acts on keys
        /** Where editing keys go: a bottom-panel editor, or an automation lane, whichever you last clicked. */
        EditorTarget* focusedEditor() const;
        std::unique_ptr<DetailPanel> detailPanel;
        std::unique_ptr<StatusBar> statusBar;
        bool browserVisible = true;

        std::unique_ptr<juce::FileChooser> fileChooser;
        std::unique_ptr<juce::DocumentWindow> tunerWindow, shortcutsWindow;
        std::unique_ptr<juce::Component> tour;

        /** Easy | Pro changed: what shows depends on it. */
        struct ViewModeListener : public juce::ChangeListener
        {
            std::function<void()> onChange;
            void changeListenerCallback (juce::ChangeBroadcaster*) override   { if (onChange) onChange(); }
        } viewModeListener;

        void beginTour();
        void openTuner (int trackIndex);   // -1: the highlighted track
        juce::ScopedMessageBox questionBox, errorBox;   // separate, so an error can't close a question mid-callback
        std::deque<std::pair<juce::String, juce::String>> pendingErrors;   // shown one at a time, none lost
        bool errorBoxShowing = false;

        // Session::Listener
        void sessionSongClosing() override;
        void sessionSongChanged() override;
        void sessionStateChanged() override;
        void sessionProblem (const juce::String& message) override;

        void openStartupSong();
        void showAboutBox();

    public:
        /** A song double-clicked in Explorer or opened with "Open with". At start-up it is opened
            instead of the last song; later the open song is saved first, as for File > Open. */
        void openSongFromSystem (const juce::File&);

    private:
        juce::File songFromSystem;   // opened by openStartupSong when set
        bool startupSongOpened = false;
        void openSongWithRecoveryCheck (const juce::File&, bool createNewSongIfItFails = false);
        void askForNewSongName (int templateIndex = -1);   // index into the song templates; -1: Empty song
        void chooseSongToOpen();
        void chooseAudioFileToImport();
        void showAddTrackMenu (juce::Component* anchor);
        void showInstrumentMenu (juce::Component* anchor);
        void openSongOrReport (const juce::File&, bool useAutosave, bool createNewSongIfItFails);
        void showError (const juce::String& title, const juce::String& message);
        void showNextError();
        void updateWindowTitle();
        bool hasRoomForBrowser() const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
