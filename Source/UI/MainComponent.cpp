/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "InterfaceScale.h"
#include "MainComponent.h"
#include "TunerView.h"
#include "HelpViews.h"
#include "UiMode.h"
#include "../Engine/MidiLearn.h"
#include "TrackMenus.h"
#include "CommandIDs.h"
#include "Dialogs.h"
#include "Shell/PianoRollView.h"
#include "Theme.h"
#include "../Core/AppPaths.h"
#include "../Engine/AudioEngine.h"

namespace ez
{
    MainComponent::MainComponent (AudioEngine& e, Session& s)
        : audioEngine (e), session (s), timeline (s)
    {
        const auto problem = [this] (const juce::String& title, const juce::String& message) { showError (title, message); };
        timeline.onProblem = problem;
        mixer = std::make_unique<MixerView> (s);
        mixer->onProblem = problem;
        mixer->getFocusedTrack = [this] { return timeline.getFocusedTrack(); };
        mixer->setFocusedTrack = [this] (int t) { timeline.setFocusedTrack (t); };
        addChildComponent (*mixer);
        mixer->showMasterView = [this] { showView (View::master); };
        TrackMenus::setTunerOpener ([this] (int t) { openTuner (t); commandManager.commandStatusChanged(); });

        // Pads and buttons on a controller run commands (see MidiLearn::getActionNames)
        session.getMidiLearn().performAction = [this] (const juce::String& action)
        {
            static const std::pair<const char*, juce::CommandID> actions[] =
            {
                { "Play / stop", CommandIDs::togglePlay }, { "Record", CommandIDs::toggleRecord }, { "Back to start", CommandIDs::returnToStart },
                { "Loop on/off", CommandIDs::toggleLoop }, { "Click on/off", CommandIDs::toggleClick }, { "Undo", CommandIDs::undo },
                { "Redo", CommandIDs::redo }, { "Punch on/off", CommandIDs::togglePunch }, { "Next take", CommandIDs::nextTake },
                { "Previous take", CommandIDs::previousTake }, { "Tuner", CommandIDs::showTuner },
            };

            for (const auto& [name, id] : actions)
                if (action == name)
                    commandManager.invokeDirectly (id, true);
        };
        session.getMidiLearn().onChange = [this] { commandManager.commandStatusChanged(); };

        // Easy | Pro: the bars and the mixer lay themselves out again
        viewModeListener.onChange = [this]
        {
            appBar->refreshViewMode();
            transportBar->resized();
            mixer->resized();
            commandManager.commandStatusChanged();
        };
        UiMode::getBroadcaster().addChangeListener (&viewModeListener);

        // The first start: a short tour (Help > Take the tour shows it again)
        if (auto& props = audioEngine.getEngine().getPropertyStorage().getPropertiesFile(); ! props.getBoolValue ("ezTourSeen", false))
        {
            props.setValue ("ezTourSeen", true);
            juce::Component::SafePointer<MainComponent> safe (this);
            juce::Timer::callAfterDelay (800, [safe] { if (safe != nullptr && safe->isShowing()) safe->beginTour(); });
        }
        masterView = std::make_unique<MasterView> (e, s, commandManager);
        masterView->onProblem = problem;
        masterScroller.setViewedComponent (masterView.get(), false);
        masterScroller.setScrollBarsShown (true, false);
        masterScroller.setWantsKeyboardFocus (false);   // arrow keys stay with the app
        addChildComponent (masterScroller);
        timeline.onEditClip = [this] { detailPanel->showClipEditor(); };
        timeline.onAddTrack = [this] (juce::Component& anchor) { showAddTrackMenu (&anchor); };
        commandManager.registerAllCommandsForTarget (this);
        commandManager.setFirstCommandTarget (this);
        addKeyListener (commandManager.getKeyMappings());

        appBar = std::make_unique<AppBar> (audioEngine, session, commandManager);
        appBar->onOpenSong = [this] (const juce::File& file) { openSongWithRecoveryCheck (file); };
        transportBar = std::make_unique<TransportBar> (session, commandManager);
        browser = std::make_unique<BrowserPanel> (e, session);
        browser->getTargetTrack = [this] { return timeline.getFocusedTrack(); };
        browser->onClose = [this] { commandManager.invokeDirectly (CommandIDs::toggleBrowser, true); };
        browser->onNewSongFromTemplate = [this] (const Session::PresetInfo& preset)
        {
            const auto templates = session.getPresets (Session::PresetKind::songTemplate);

            for (int i = 0; i < templates.size(); ++i)
                if (templates[i].name == preset.name && templates[i].file == preset.file)
                    askForNewSongName (i);
        };
        browser->onProblem = problem;
        detailPanel = std::make_unique<DetailPanel> (session);
        detailPanel->setFocusedTrackSource ([this] { return timeline.getFocusedTrack(); });
        detailPanel->setProblemHandler (problem);
        detailPanel->onHeightChanged = [this] { resized(); commandManager.commandStatusChanged(); };
        statusBar = std::make_unique<StatusBar> (audioEngine, session);

        for (auto* c : std::initializer_list<juce::Component*> { appBar.get(), transportBar.get(), browser.get(), &timeline,
                                                                 detailPanel.get(), statusBar.get() })
            addAndMakeVisible (c);

        // Buttons that run a command: their tooltip is the command's description, which names the shortcut
        // already (JUCE would add it again, as "[ctrl + Z]")
        std::function<void (juce::Component&)> tooltipsFromCommands = [this, &tooltipsFromCommands] (juce::Component& c)
        {
            if (auto* b = dynamic_cast<juce::Button*> (&c); b != nullptr && b->getCommandID() != 0)
                if (auto* command = commandManager.getCommandForID (b->getCommandID()); command != nullptr && command->description.isNotEmpty())
                    b->setTooltip (command->description);

            for (auto* child : c.getChildren())
                tooltipsFromCommands (*child);
        };

        tooltipsFromCommands (*this);
        tooltipsFromCommands (*masterView);   // (not on screen yet: not a child)

        session.addListener (this);

        setWantsKeyboardFocus (true);
        setSize (Theme::windowWidth, Theme::windowHeight);

        // Open the song once the window is on screen, so any question appears on top of it
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safeThis != nullptr)
                safeThis->openStartupSong();
        });
    }

    MainComponent::~MainComponent()
    {
        UiMode::getBroadcaster().removeChangeListener (&viewModeListener);
        tour = nullptr;
        shortcutsWindow = nullptr;
        TrackMenus::setTunerOpener (nullptr);
        session.getMidiLearn().performAction = nullptr;
        session.getMidiLearn().onChange = nullptr;
        tunerWindow = nullptr;
        session.removeListener (this);
    }

    bool MainComponent::hasRoomForBrowser() const
    {
        return getWidth() - Theme::browserWidth >= Theme::trackHeaderWidth + 300;
    }

    //==============================================================================
    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::background);
    }

    void MainComponent::paintOverChildren (juce::Graphics& g)
    {
        // A soft shadow under the transport: the bars at the top sit above the work area
        const auto y = (float) transportBar->getBottom();
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::black.withAlpha (0.28f), y, juce::Colours::transparentBlack, y + 6.0f));
        g.fillRect (juce::Rectangle<float> (0.0f, y, (float) getWidth(), 6.0f));

        // ... and the bottom panel rises over the timeline when it's open
        if (detailPanel != nullptr && detailPanel->isVisible() && detailPanel->isOpen())
        {
            const auto top = detailPanel->getBounds().toFloat();
            g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::transparentBlack, top.getY() - 6.0f, juce::Colours::black.withAlpha (0.3f), top.getY()));
            g.fillRect (top.withY (top.getY() - 6.0f).withHeight (6.0f));
        }
    }

    void MainComponent::resized()
    {
        auto r = getLocalBounds();
        appBar->setBounds (r.removeFromTop (Theme::appBarHeight));
        transportBar->setBounds (r.removeFromTop (Theme::transportHeight));
        statusBar->setBounds (r.removeFromBottom (Theme::statusBarHeight));

        if (tour != nullptr)
            tour->setBounds (getLocalBounds());

        // The browser gives way first on a narrow window: the timeline needs the width most
        const auto wasVisible = browser->isVisible();
        browser->setVisible (browserVisible && hasRoomForBrowser());

        if (browser->isVisible() != wasVisible)
            commandManager.commandStatusChanged();   // the B toggle shows what's really on screen

        if (browser->isVisible())
            browser->setBounds (r.removeFromLeft (Theme::browserWidth));

        // The panel never squeezes the timeline below a usable height, nor the mixer below one with a send row
        // (at the 900x500 minimum, the open panel shrinks to its header in the Mix view)
        const auto mainMin = view == View::mix ? 340 : view == View::master ? 300 : 160;
        const auto panelHeight = juce::jlimit (Theme::detailHeaderHeight, juce::jmax (Theme::detailHeaderHeight, r.getHeight() - mainMin),
                                               detailPanel->getPreferredHeight());
        detailPanel->setBounds (r.removeFromBottom (panelHeight));
        timeline.setBounds (r);
        mixer->setBounds (r);
        masterScroller.setBounds (r);
        {
            constexpr int masterMinHeight = 600;   // the chain, loudness and reference all usable
            const auto scrolls = r.getHeight() < masterMinHeight;
            masterView->setSize (r.getWidth() - (scrolls ? masterScroller.getScrollBarThickness() : 0), juce::jmax (r.getHeight(), masterMinHeight));
        }
    }

    //==============================================================================
    void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
    {
        using namespace CommandIDs;
        commands.addArray ({ newSong, openSong, saveSong, audioSetup, pluginList,
                             togglePlay, toggleRecord, returnToStart, toggleClick, toggleCountIn,
                             addTrack, importAudio, undo, redo, addInstrumentTrack, routing,
                             deleteSelection, toggleLoop, toggleSnap, zoomIn, zoomOut,
                             copy, cut, paste, duplicate, selectAll, splitClips, deselectAll,
                             nudgeLeft, nudgeRight, goToEnd, toggleBrowser, toggleDetailPanel, toggleClipEditor, resetMeters,
                             showArrangeView, showMixView, toggleMixView, showMasterView, toggleMasterChain, toggleReference, showTuner, togglePunch, punchInHere, punchOutHere, nextTake, previousTake, midiLearn, toggleMidiClock, toggleProView, showShortcuts, startTour, showAbout, openLogFolder,
                             tapTempo, fadeClips, normaliseClips, reverseClips, followTempoClips, transposeClipsUp, transposeClipsDown, zoomToFit, loopSelection,
                             renameTrack, duplicateTrack, deleteTrack, moveTrackUp, moveTrackDown, previousTrack, nextTrack, toggleFollow, saveSongAs, songMenu, addBus, addFxReturn, routeToNewBus, toggleSends,
                             toggleAutomationLanes, toggleAutomationWrite, toggleAutomationRead,
                             interfaceBigger, interfaceSmaller, interfaceAutomatic,
                             muteTrack, soloTrack, armTrack, exportSong, focusSearch });
    }

    void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
    {
        using juce::KeyPress;
        using juce::ModifierKeys;

        const auto cmd = ModifierKeys::commandModifier;
        const bool hasSong = session.hasSong();

        switch (id)
        {
            case CommandIDs::newSong:
                info.setInfo ("New Song", "Start a new song", "Song", 0);
                info.addDefaultKeypress ('n', cmd);
                break;

            case CommandIDs::openSong:
                info.setInfo ("Open", "Open a song", "Song", 0);
                info.addDefaultKeypress ('o', cmd);
                break;

            case CommandIDs::saveSong:
                info.setInfo ("Save", "Save the song (Ctrl+S; it also autosaves every minute)", "Song", 0);
                info.addDefaultKeypress ('s', cmd);
                info.setActive (hasSong);
                break;

            case CommandIDs::saveSongAs:
                info.setInfo ("Save as new song...", "Save a copy under a new name, with its recordings, and carry on in it (Ctrl+Shift+S)", "Song", 0);
                info.addDefaultKeypress ('s', cmd | ModifierKeys::shiftModifier);
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::songMenu:
                info.setInfo ("Song menu", "Recent songs, save as, the song's folder: also click the song's name (Ctrl+Alt+O)", "Song", 0);
                info.addDefaultKeypress ('o', cmd | ModifierKeys::altModifier);
                break;

            case CommandIDs::audioSetup:
                info.setInfo ("Setup", "Audio device, routing (headphones or speakers), plugins, recording and MIDI controllers (Ctrl+,)", "Setup", 0);
                info.addDefaultKeypress (',', cmd);
                break;

            case CommandIDs::deleteSelection:
                info.setInfo ("Delete", "Delete the selected clips (Delete)", "Edit", 0);
                info.addDefaultKeypress (KeyPress::deleteKey, 0);
                info.addDefaultKeypress (KeyPress::backspaceKey, 0);
                info.setActive ((session.getSelectedClip() != nullptr && arrangeTakesKeys()) || (focusedEditor() != nullptr));
                break;

            case CommandIDs::copy:
                info.setInfo ("Copy", "Copy the selected clips (Ctrl+C)", "Edit", 0);
                info.addDefaultKeypress ('c', cmd);
                info.setActive (hasSong);
                break;

            case CommandIDs::cut:
                info.setInfo ("Cut", "Cut the selected clips (Ctrl+X)", "Edit", 0);
                info.addDefaultKeypress ('x', cmd);
                info.setActive (hasSong);
                break;

            case CommandIDs::paste:
                info.setInfo ("Paste", "Paste at the playhead, on the highlighted track (Ctrl+V)", "Edit", 0);
                info.addDefaultKeypress ('v', cmd);
                info.setActive (hasSong);
                break;

            case CommandIDs::duplicate:
                info.setInfo ("Duplicate", "Repeat the selected clips straight after themselves (Ctrl+D)", "Edit", 0);
                info.addDefaultKeypress ('d', cmd);
                info.setActive (hasSong);
                break;

            case CommandIDs::selectAll:
                info.setInfo ("Select all", "Select every clip (Ctrl+A)", "Edit", 0);
                info.addDefaultKeypress ('a', cmd);
                info.setActive (hasSong);
                break;

            case CommandIDs::splitClips:
                info.setInfo ("Split", "Split clips at the playhead: the selected ones, or every clip there if none are selected (Ctrl+E)", "Edit", 0);
                info.addDefaultKeypress ('e', cmd);
                info.setActive (hasSong);
                break;

            case CommandIDs::deselectAll:
                info.setInfo ("Deselect", "Clear the selection (Esc)", "Edit", 0);
                info.addDefaultKeypress (KeyPress::escapeKey, 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::nudgeLeft:
                info.setInfo ("Nudge left", "Move the selected clips (or the playhead) one grid step left (Left arrow)", "Edit", 0);
                info.addDefaultKeypress (KeyPress::leftKey, 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::nudgeRight:
                info.setInfo ("Nudge right", "Move the selected clips (or the playhead) one grid step right (Right arrow)", "Edit", 0);
                info.addDefaultKeypress (KeyPress::rightKey, 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::goToEnd:
                info.setInfo ("Go to end", "Move the playhead to the end of the song (End)", "Transport", 0);
                info.addDefaultKeypress (KeyPress::endKey, 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::toggleLoop:
                info.setInfo ("Loop", "Loop on/off (L). Drag in the lower half of the ruler to set the loop.", "Transport", 0);
                info.addDefaultKeypress ('l', 0);
                info.setActive (hasSong && ! session.isRecording());
                info.setTicked (session.isLooping());
                break;

            case CommandIDs::toggleSnap:
                info.setInfo ("Snap", "Snap clips and the playhead to the grid (G)", "Edit", 0);
                info.addDefaultKeypress ('g', 0);
                info.setTicked (timeline.isSnapEnabled());
                break;

            case CommandIDs::zoomIn:
                info.setInfo ("Zoom in", "Zoom in (= or Ctrl + mouse wheel)", "View", 0);
                info.addDefaultKeypress ('=', 0);
                break;

            case CommandIDs::zoomOut:
                info.setInfo ("Zoom out", "Zoom out (- or Ctrl + mouse wheel)", "View", 0);
                info.addDefaultKeypress ('-', 0);
                break;

            case CommandIDs::routing:
                info.setInfo ("Routing", "Where you hear the song, test beeps for each output, and every track's input and output (Ctrl+Alt+,)", "Setup", 0);
                info.addDefaultKeypress (',', cmd | ModifierKeys::altModifier);
                break;

            case CommandIDs::toggleDetailPanel:
                info.setInfo ("Devices", "Open or close the bottom panel on the highlighted track's instrument and effects (D)", "View", 0);
                info.addDefaultKeypress ('d', 0);
                info.setTicked (detailPanel != nullptr && detailPanel->isOpen() && detailPanel->getTab() == DetailPanel::Tab::devices);
                info.setActive (hasSong);
                break;

            case CommandIDs::showArrangeView:
                info.setInfo ("Arrange", "Arrange: record and build the song (Ctrl+1; Tab: next view)", "View", 0);
                info.addDefaultKeypress ('1', cmd);
                info.setTicked (view == View::arrange);
                break;

            case CommandIDs::showMixView:
                info.setInfo ("Mix", "Mix: every track's fader, pan, inserts and meter side by side (Ctrl+2; Tab: next view)", "View", 0);
                info.addDefaultKeypress ('2', cmd);
                info.setTicked (view == View::mix);
                break;

            case CommandIDs::showMasterView:
                info.setInfo ("Master", "Master: the mastering chain, loudness and a reference to compare with (Ctrl+3; Tab: next view)", "View", 0);
                info.addDefaultKeypress ('3', cmd);
                info.setTicked (view == View::master);
                break;

            case CommandIDs::toggleMixView:
                info.setInfo ("Next view", "Arrange, Mix, Master in turn (Shift+Tab goes back)", "View", 0);
                info.addDefaultKeypress (juce::KeyPress::tabKey, 0);
                info.addDefaultKeypress (juce::KeyPress::tabKey, ModifierKeys::shiftModifier);
                break;

            case CommandIDs::toggleMasterChain:
                info.setInfo ("Mastering chain on / off", "Compare the song with and without the mastering chain (Ctrl+Shift+A)", "Master", 0);
                info.addDefaultKeypress ('a', cmd | ModifierKeys::shiftModifier);
                info.setTicked (session.isMasterChainOn());
                info.setActive (hasSong);
                break;

            case CommandIDs::togglePunch:
                info.setInfo ("Punch recording", "Record only inside the loop region, playing the rest (P)", "Transport", 0);
                info.addDefaultKeypress ('p', 0);
                info.setTicked (session.isPunchOn());
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::punchInHere:
                info.setInfo ("Punch in here", "Start punch recording at the playhead: the loop region starts here, punch on (Ctrl+Shift+I; P turns punch off)", "Transport", 0);
                info.addDefaultKeypress ('i', cmd | ModifierKeys::shiftModifier);
                info.setTicked (session.isPunchOn());
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::punchOutHere:
                info.setInfo ("Punch out here", "Stop punch recording at the playhead: the loop region ends here, punch on (Ctrl+Shift+O; P turns punch off)", "Transport", 0);
                info.addDefaultKeypress ('o', cmd | ModifierKeys::shiftModifier);
                info.setTicked (session.isPunchOn());
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::renameTrack:
                info.setInfo ("Rename track", "Rename the highlighted track (F2)", "Track", 0);
                info.addDefaultKeypress (juce::KeyPress::F2Key, 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::duplicateTrack:
                info.setInfo ("Duplicate track", "A copy of the highlighted track, with its clips and effects (Ctrl+Shift+D)", "Track", 0);
                info.addDefaultKeypress ('d', cmd | ModifierKeys::shiftModifier);
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::deleteTrack:
                info.setInfo ("Delete track", "Delete the highlighted track (Ctrl+Delete; Ctrl+Z brings it back)", "Track", 0);
                info.addDefaultKeypress (KeyPress::deleteKey, cmd);
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::moveTrackUp:
                info.setInfo ("Move track up", "Move the highlighted track up (Ctrl+Alt+Up)", "Track", 0);
                info.addDefaultKeypress (KeyPress::upKey, cmd | ModifierKeys::altModifier);
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::moveTrackDown:
                info.setInfo ("Move track down", "Move the highlighted track down (Ctrl+Alt+Down)", "Track", 0);
                info.addDefaultKeypress (KeyPress::downKey, cmd | ModifierKeys::altModifier);
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::previousTrack:
                info.setInfo ("Track above", "Highlight the track above (Up)", "Track", 0);
                info.addDefaultKeypress (KeyPress::upKey, 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::nextTrack:
                info.setInfo ("Track below", "Highlight the track below (Down)", "Track", 0);
                info.addDefaultKeypress (KeyPress::downKey, 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::toggleFollow:
                info.setInfo ("Follow the playhead", "The arrange view pages along with the song as it plays (Shift+F)", "View", 0);
                info.addDefaultKeypress ('f', ModifierKeys::shiftModifier);
                info.setTicked (timeline.isFollowing());
                break;

            case CommandIDs::zoomToFit:
                info.setInfo ("Zoom to fit", "See the whole song across the screen (Z)", "View", 0);
                info.addDefaultKeypress ('z', 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::loopSelection:
                info.setInfo ("Loop the selection", "Loop round the selected clips, to practise or record over them (Ctrl+L)", "Transport", 0);
                info.addDefaultKeypress ('l', cmd);
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::tapTempo:
                info.setInfo ("Tap tempo", "Tap K in time with the music: the tempo follows your taps", "Transport", 0);
                info.addDefaultKeypress ('k', 0);
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::fadeClips:
                info.setInfo ("Fade in and out", "Short fades on the selected audio clips: no clicks at their edges (F). Drag the handles for longer.", "Audio", 0);
                info.addDefaultKeypress ('f', 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::normaliseClips:
                info.setInfo ("Normalise", "Selected audio clips: their loudest peak to -1 dB (Ctrl+Shift+N)", "Audio", 0);
                info.addDefaultKeypress ('n', cmd | ModifierKeys::shiftModifier);
                info.setActive (hasSong);
                break;

            case CommandIDs::reverseClips:
                info.setInfo ("Reverse", "Play the selected audio clips backwards, or forwards again (Ctrl+Alt+R)", "Audio", 0);
                info.addDefaultKeypress ('r', cmd | ModifierKeys::altModifier);
                info.setActive (hasSong);
                break;

            case CommandIDs::followTempoClips:
                info.setInfo ("Follow the song tempo", "Selected loops play at the song's tempo, and follow it when it changes (Ctrl+J)", "Audio", 0);
                info.addDefaultKeypress ('j', cmd);
                info.setActive (hasSong);
                break;

            case CommandIDs::transposeClipsUp:
                info.setInfo ("Transpose up", "Selected audio clips a semitone higher, same speed (Alt+Up)", "Audio", 0);
                info.addDefaultKeypress (KeyPress::upKey, ModifierKeys::altModifier);
                info.setActive (hasSong);
                break;

            case CommandIDs::transposeClipsDown:
                info.setInfo ("Transpose down", "Selected audio clips a semitone lower, same speed (Alt+Down)", "Audio", 0);
                info.addDefaultKeypress (KeyPress::downKey, ModifierKeys::altModifier);
                info.setActive (hasSong);
                break;

            case CommandIDs::toggleProView:
                info.setInfo ("Pro view", "Easy view shows what most songs need; Pro adds routing, inserts, punch and MIDI sync (Ctrl+Shift+P)", "View", 0);
                info.addDefaultKeypress ('p', cmd | ModifierKeys::shiftModifier);
                info.setTicked (UiMode::isPro());
                break;

            case CommandIDs::showShortcuts:
                info.setInfo ("Keyboard shortcuts", "Every command and its keys, searchable (F1)", "Help", 0);
                info.addDefaultKeypress (juce::KeyPress::F1Key, 0);
                info.setTicked (shortcutsWindow != nullptr);
                break;

            case CommandIDs::startTour:
                info.setInfo ("Take the tour", "A quick look round the screen, one part at a time (Shift+F1)", "Help", 0);
                info.addDefaultKeypress (juce::KeyPress::F1Key, ModifierKeys::shiftModifier);
                break;

            case CommandIDs::showAbout:
                info.setInfo ("About EzDAWsIt", "Version, licence and the software EzDAWsIt is built on (Ctrl+F1)", "Help", 0);
                info.addDefaultKeypress (juce::KeyPress::F1Key, cmd);
                break;

            case CommandIDs::openLogFolder:
                info.setInfo ("Open the log folder", "The logs to send with a bug report, in Explorer (Ctrl+Alt+F1)", "Help", 0);
                info.addDefaultKeypress (juce::KeyPress::F1Key, cmd | ModifierKeys::altModifier);
                break;

            case CommandIDs::midiLearn:
                info.setInfo ("MIDI learn", "Link a knob, fader or pad on your controller: touch a control here, then move one there (Ctrl+Shift+L)", "MIDI", 0);
                info.addDefaultKeypress ('l', cmd | ModifierKeys::shiftModifier);
                info.setTicked (session.getMidiLearn().isLearning());
                info.setActive (hasSong);
                break;

            case CommandIDs::toggleMidiClock:
                info.setInfo ("Send MIDI clock", "Your drum machine or groovebox follows the tempo, play and stop (Ctrl+Shift+Y)", "MIDI", 0);
                info.addDefaultKeypress ('y', cmd | ModifierKeys::shiftModifier);
                info.setTicked (session.getMidiLearn().isSendingClockAnywhere());
                break;

            case CommandIDs::nextTake:
                info.setInfo ("Next take", "Play the next take of the selected clip (T). Record over a loop to make takes.", "Edit", 0);
                info.addDefaultKeypress ('t', 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::previousTake:
                info.setInfo ("Previous take", "Play the take before of the selected clip (Shift+T)", "Edit", 0);
                info.addDefaultKeypress ('t', ModifierKeys::shiftModifier);
                info.setActive (hasSong);
                break;

            case CommandIDs::showTuner:
                info.setInfo ("Tuner", "Tune your guitar or bass on the highlighted track (Ctrl+U)", "Track", 0);
                info.addDefaultKeypress ('u', cmd);
                info.setTicked (tunerWindow != nullptr);
                info.setActive (hasSong);
                break;

            case CommandIDs::toggleReference:
                info.setInfo ("Reference A/B", "Hear the reference song instead of your mix, and back (Ctrl+Shift+F)", "Master", 0);
                info.addDefaultKeypress ('f', cmd | ModifierKeys::shiftModifier);
                info.setTicked (session.isReferenceListening());
                info.setActive (hasSong && session.getReferenceFile().existsAsFile());
                break;

            case CommandIDs::addBus:
                info.setInfo ("+ Bus", "Add a bus: tracks play into it, so one fader turns the group up or down (Ctrl+Shift+B)", "Mix", 0);
                info.addDefaultKeypress ('b', cmd | ModifierKeys::shiftModifier);
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::addFxReturn:
                info.setInfo ("+ Reverb return", "Add an FX return with a reverb every track can send to (Ctrl+Shift+R)", "Mix", 0);
                info.addDefaultKeypress ('r', cmd | ModifierKeys::shiftModifier);
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::routeToNewBus:
            {
                const auto t = timeline.getFocusedTrack();
                info.setInfo ("Play into a new bus", "Put the highlighted track in a new bus (Ctrl+Shift+G)", "Mix", 0);
                info.addDefaultKeypress ('g', cmd | ModifierKeys::shiftModifier);
                info.setActive (hasSong && ! session.isRecording() && juce::isPositiveAndBelow (t, session.getNumAudioTracks())
                                 && session.getTrackKind (t) != Session::TrackKind::fxReturn);
                break;
            }

            case CommandIDs::toggleSends:
                info.setInfo ("Sends", "Show or hide the sends on the mixer strips (Ctrl+Shift+E)", "Mix", 0);
                info.addDefaultKeypress ('e', cmd | ModifierKeys::shiftModifier);
                info.setTicked (mixer != nullptr && mixer->isShowingSends());
                break;

            case CommandIDs::focusSearch:
                info.setInfo ("Search", "Find instruments, effects, drum kits, samples and files (Ctrl+F)", "View", 0);
                info.addDefaultKeypress ('f', cmd);
                break;

            case CommandIDs::exportSong:
                info.setInfo ("Export...", "Export the song, stems or MIDI to files (Ctrl+R)", "File", 0);
                info.addDefaultKeypress ('r', cmd);
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::muteTrack:
                info.setInfo ("Mute track", "Mute or unmute the highlighted track (M)", "Track", 0);
                info.addDefaultKeypress ('m', 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::soloTrack:
                info.setInfo ("Solo track", "Hear only the highlighted track, or stop soloing it (S)", "Track", 0);
                info.addDefaultKeypress ('s', 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::armTrack:
                info.setInfo ("Arm track", "Arm or disarm the highlighted track for recording (Shift+R)", "Track", 0);
                info.addDefaultKeypress ('r', ModifierKeys::shiftModifier);
                info.setActive (hasSong);
                break;

            case CommandIDs::interfaceBigger:
                info.setInfo ("Bigger interface", "Make text and controls bigger (Ctrl+=)", "View", 0);
                info.addDefaultKeypress ('=', cmd);
                info.addDefaultKeypress ('=', cmd | ModifierKeys::shiftModifier);   // Ctrl++ on a US keyboard
                info.addDefaultKeypress ('+', cmd);
                info.addDefaultKeypress (KeyPress::numberPadAdd, cmd);
                break;

            case CommandIDs::interfaceSmaller:
                info.setInfo ("Smaller interface", "Make text and controls smaller (Ctrl+-)", "View", 0);
                info.addDefaultKeypress ('-', cmd);
                info.addDefaultKeypress (KeyPress::numberPadSubtract, cmd);
                break;

            case CommandIDs::interfaceAutomatic:
                info.setInfo ("Automatic interface size", "The size that suits this screen (Ctrl+0)", "View", 0);
                info.addDefaultKeypress ('0', cmd);
                break;

            case CommandIDs::toggleAutomationLanes:
                info.setInfo ("Automation lanes", "Show or hide the highlighted track's automation lanes (A)", "Track", 0);
                info.addDefaultKeypress ('a', 0);
                info.setActive (hasSong);   // (ignored in the Mix view: disabled, the key would beep)
                break;

            case CommandIDs::toggleAutomationWrite:
                info.setInfo ("Write automation", "Moving a control while the song plays records it into its lane (W)", "Track", 0);
                info.addDefaultKeypress ('w', 0);
                info.setTicked (session.isAutomationWriteOn());
                info.setActive (hasSong);
                break;

            case CommandIDs::toggleAutomationRead:
                info.setInfo ("Read automation", "Automation lanes move their controls as the song plays (Shift+W)", "Track", 0);
                info.addDefaultKeypress ('w', ModifierKeys::shiftModifier);
                info.setTicked (session.isAutomationReadOn());
                info.setActive (hasSong);
                break;

            case CommandIDs::resetMeters:
                info.setInfo ("Reset meters", "Turn off every clip light and clear the held peaks and loudness (Ctrl+Shift+M)", "View", 0);
                info.addDefaultKeypress ('m', cmd | ModifierKeys::shiftModifier);
                info.setActive (hasSong);
                break;

            case CommandIDs::toggleClipEditor:
                info.setInfo ("Clip editor", "Open or close the bottom panel on the selected MIDI clip's notes (E)", "View", 0);
                info.addDefaultKeypress ('e', 0);
                info.setTicked (detailPanel != nullptr && detailPanel->isOpen() && detailPanel->getTab() == DetailPanel::Tab::clipEditor);
                info.setActive (hasSong);
                break;

            case CommandIDs::toggleBrowser:
                info.setInfo ("Browser", "Show or hide the browser (B)", "View", 0);
                info.addDefaultKeypress ('b', 0);
                info.setTicked (browser != nullptr && browser->isVisible());   // what's on screen, not just the wish
                break;

            case CommandIDs::pluginList:
                info.setInfo ("Plugins", "Scan and manage VST3 plugins (Ctrl+Alt+P)", "Setup", 0);
                info.addDefaultKeypress ('p', cmd | ModifierKeys::altModifier);
                break;

            case CommandIDs::togglePlay:
                info.setInfo ("Play / Stop", "Play or stop (Space)", "Transport", 0);
                info.addDefaultKeypress (KeyPress::spaceKey, 0);
                info.setActive (hasSong);
                info.setTicked (session.isPlaying());
                break;

            case CommandIDs::toggleRecord:
                info.setInfo ("Record", "Record on armed tracks (R)", "Transport", 0);
                info.addDefaultKeypress ('r', 0);
                info.setActive (hasSong);
                info.setTicked (session.isRecording());
                break;

            case CommandIDs::returnToStart:
                info.setInfo ("Back to Start", "Jump to the start of the song (Home)", "Transport", 0);
                info.addDefaultKeypress (KeyPress::homeKey, 0);
                info.setActive (hasSong);
                break;

            case CommandIDs::toggleClick:
                info.setInfo ("Click", "Metronome on/off (C)", "Transport", 0);
                info.addDefaultKeypress ('c', 0);
                info.setActive (hasSong);
                info.setTicked (session.isClickEnabled());
                break;

            case CommandIDs::toggleCountIn:
                info.setInfo ("Count-in", "One bar of click before recording starts (Shift+C)", "Transport", 0);
                info.addDefaultKeypress ('c', ModifierKeys::shiftModifier);
                info.setActive (hasSong);
                info.setTicked (session.isCountInEnabled());
                break;

            case CommandIDs::addTrack:
                info.setInfo ("+ Track", "Add an audio track (Ctrl+T)", "Edit", 0);
                info.addDefaultKeypress ('t', cmd);
                info.setActive (hasSong);
                break;

            case CommandIDs::addInstrumentTrack:
                info.setInfo ("+ Instrument", "Add a track with a synth, sampler or plugin instrument, ready to play (Ctrl+Shift+T)", "Edit", 0);
                info.addDefaultKeypress ('t', cmd | ModifierKeys::shiftModifier);
                info.setActive (hasSong && ! session.isRecording());
                break;

            case CommandIDs::importAudio:
                info.setInfo ("Import Audio", "Put an audio file on the selected track at the playhead (Ctrl+I)", "Edit", 0);
                info.addDefaultKeypress ('i', cmd);
                info.setActive (hasSong);
                break;

            case CommandIDs::undo:
                info.setInfo ("Undo", "Undo (Ctrl+Z)", "Edit", 0);
                info.addDefaultKeypress ('z', cmd);
                info.setActive (hasSong && ! session.isRecording());   // (not canUndo: some edits change the history without telling us)
                break;

            case CommandIDs::redo:
                info.setInfo ("Redo", "Redo (Ctrl+Y)", "Edit", 0);
                info.addDefaultKeypress ('y', cmd);
                info.addDefaultKeypress ('z', cmd | ModifierKeys::shiftModifier);
                info.setActive (hasSong && ! session.isRecording());
                break;

            default:
                break;
        }
    }

    void MainComponent::showView (View newView)
    {
        view = newView;
        mixShown = view == View::mix;
        mixer->setVisible (view == View::mix);
        masterScroller.setVisible (view == View::master);
        timeline.setVisible (view == View::arrange);

        if (view == View::master)
            masterView->refresh();

        resized();   // the views need different minimum heights
        commandManager.commandStatusChanged();   // the AppBar's Arrange / Mix / Master buttons follow
    }

    EditorTarget* MainComponent::focusedEditor() const
    {
        if (auto* editor = detailPanel != nullptr ? detailPanel->getFocusedEditor() : nullptr)
            return editor;

        return arrangeTakesKeys() ? timeline.getFocusedEditor() : nullptr;
    }

    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        if (mixShown && mixer->handleKey (key))
            return true;

        // The key mappings get first go; this sees the rest (the piano roll's Up/Down, 1-3, Q, F)
        auto* editor = focusedEditor();
        return editor != nullptr && editor->handleKey (key);
    }

    bool MainComponent::perform (const InvocationInfo& info)
    {
        // Plain keys the open editor also uses (the piano roll's F fits the view; =, - and Z zoom it):
        // while it has the focus, they're its
        // (and the mixer's own keys, when it's showing: Up/Down move along its strips)
        if (info.invocationMethod == InvocationInfo::fromKeyPress
             && (info.commandID == CommandIDs::fadeClips || info.commandID == CommandIDs::zoomIn
                 || info.commandID == CommandIDs::zoomOut || info.commandID == CommandIDs::zoomToFit
                 || info.commandID == CommandIDs::previousTrack || info.commandID == CommandIDs::nextTrack))
            if (keyPressed (info.keyPress))
                return true;

        switch (info.commandID)
        {
            case CommandIDs::newSong:       askForNewSongName(); return true;
            case CommandIDs::openSong:      chooseSongToOpen(); return true;
            case CommandIDs::audioSetup:    Dialogs::showSetup (audioEngine, session, this, SetupView::Page::audioDevice); return true;
            case CommandIDs::pluginList:    Dialogs::showSetup (audioEngine, session, this, SetupView::Page::plugins); return true;
            case CommandIDs::routing:       Dialogs::showSetup (audioEngine, session, this, SetupView::Page::routing); return true;
            case CommandIDs::toggleDetailPanel:
                detailPanel->toggleTab (DetailPanel::Tab::devices);
                return true;
            case CommandIDs::toggleClipEditor:
                detailPanel->toggleTab (DetailPanel::Tab::clipEditor);
                return true;
            case CommandIDs::resetMeters:
                session.getMeters().resetAll();
                return true;

            case CommandIDs::focusSearch:
                if (! browser->isVisible())
                {
                    if (! hasRoomForBrowser())
                    {
                        showError ("No room for the browser", "Make the window wider to show the browser.");
                        return true;
                    }

                    browserVisible = true;
                    resized();
                }

                browser->focusSearch();
                return true;

            case CommandIDs::exportSong:
                if (session.isRecording())
                    session.showHint ("Stop recording before exporting");
                else
                    Dialogs::showExport (session, this);

                return true;

            case CommandIDs::muteTrack:
            case CommandIDs::soloTrack:
            case CommandIDs::armTrack:
            {
                // The drum machine (pad mute/solo) and the Mix view's own keys come first
                if (info.invocationMethod == InvocationInfo::fromKeyPress && keyPressed (info.keyPress))
                    return true;

                const auto t = timeline.getFocusedTrack();

                if (! juce::isPositiveAndBelow (t, session.getNumAudioTracks()))
                {
                    session.showHint ("Click a track first, then press the key again");
                    return true;
                }

                const auto name = "\"" + session.getTrackName (t) + "\"";

                if (info.commandID == CommandIDs::muteTrack)
                {
                    session.setTrackMuted (t, ! session.isTrackMuted (t));
                    session.showHint (name + (session.isTrackMuted (t) ? " muted (M to unmute)" : " unmuted"));
                }
                else if (info.commandID == CommandIDs::soloTrack)
                {
                    session.setTrackSoloed (t, ! session.isTrackSoloed (t));
                    session.showHint (name + (session.isTrackSoloed (t) ? " soloed (S to stop)" : " not soloed"));
                }
                else if (session.setTrackArmed (t, ! session.isTrackArmed (t)))
                {
                    session.showHint (name + (session.isTrackArmed (t) ? " armed: R records on it" : " disarmed"));
                }
                else
                {
                    session.showHint (session.getLastError());
                }

                return true;
            }

            case CommandIDs::interfaceBigger:
            case CommandIDs::interfaceSmaller:
                InterfaceScale::step (audioEngine.getEngine(), info.commandID == CommandIDs::interfaceBigger ? 1 : -1);
                session.showHint ("Interface size " + InterfaceScale::describe (InterfaceScale::current()) + " (Ctrl+= bigger, Ctrl+- smaller, Ctrl+0 automatic)");
                return true;

            case CommandIDs::interfaceAutomatic:
                InterfaceScale::apply (audioEngine.getEngine(), 0.0f);
                session.showHint ("Interface size " + InterfaceScale::describe (0.0f));
                return true;

            case CommandIDs::toggleAutomationLanes:
                if (arrangeTakesKeys())
                    session.toggleAutomationLanes (timeline.getFocusedTrack());

                return true;

            case CommandIDs::toggleAutomationWrite:
                session.setAutomationWrite (! session.isAutomationWriteOn());
                session.showHint (session.isAutomationWriteOn() ? "Write is on: move a control while the song plays to record it" : "Write is off");
                commandManager.commandStatusChanged();
                return true;

            case CommandIDs::toggleAutomationRead:
                session.setAutomationRead (! session.isAutomationReadOn());
                session.showHint (session.isAutomationReadOn() ? "Read is on: automation lanes play" : "Read is off: controls hold where they are, and you can move them by hand");
                commandManager.commandStatusChanged();
                return true;

            case CommandIDs::addBus:
            case CommandIDs::addFxReturn:
            case CommandIDs::routeToNewBus:
            {
                const auto focused = timeline.getFocusedTrack();
                const auto t = info.commandID == CommandIDs::addBus ? session.addBusTrack()
                             : info.commandID == CommandIDs::addFxReturn ? session.addReturnTrack (Session::ReturnEffect::reverb)
                             : session.routeTracksToNewBus ({ focused });

                if (t < 0)
                    showError ("Mix", session.getLastError());
                else
                    mixer->revealTrack (t);   // highlights it in the arrange too

                return true;
            }

            case CommandIDs::toggleSends:
                if (! mixShown)
                {
                    showMix (true);   // asking for sends from the arrange means you want to see them
                    mixer->setShowSends (true);
                }
                else
                {
                    mixer->setShowSends (! mixer->isShowingSends());
                }

                commandManager.commandStatusChanged();
                return true;
            case CommandIDs::showArrangeView:   showView (View::arrange); return true;
            case CommandIDs::showMixView:       showView (View::mix); return true;
            case CommandIDs::showMasterView:    showView (View::master); return true;
            case CommandIDs::toggleMixView:
                // Tab while typing a name or a number is not a view switch (it would also cut the typing short)
                if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) == nullptr)
                {
                    const bool back = info.invocationMethod == InvocationInfo::fromKeyPress && info.keyPress.getModifiers().isShiftDown();
                    const auto next = (int) view + (back ? 2 : 1);
                    showView ((View) (next % 3));
                }

                return true;

            case CommandIDs::toggleMasterChain:
                if (! session.setMasterChainOn (! session.isMasterChainOn()))
                    showError ("Mastering chain", session.getLastError());
                else
                    session.showHint (session.isMasterChainOn() ? "Mastering chain on (Ctrl+Shift+A to compare)" : "Mastering chain off: the song as it is");

                commandManager.commandStatusChanged();
                return true;

            case CommandIDs::togglePunch:
                if (! session.setPunch (! session.isPunchOn()))
                    session.showHint (session.getLastError());
                else
                    session.showHint (session.isPunchOn() ? "Punch on: recording only inside the loop region (P to turn off)" : "Punch off");

                commandManager.commandStatusChanged();
                return true;

            case CommandIDs::punchInHere:
            case CommandIDs::punchOutHere:
            {
                const auto here = session.getPosition();
                auto loop = session.getLoopRange();
                const bool in = info.commandID == CommandIDs::punchInHere;
                auto start = in ? here : loop.getStart();
                auto end = in ? loop.getEnd() : here;

                // A loop that doesn't fit round the playhead: four beats from it
                if (end <= start + te::TimeDuration::fromSeconds (0.05))
                {
                    const auto fourBeats = te::TimeDuration::fromSeconds (4.0 * 60.0 / juce::jmax (20.0, session.getBpm()));

                    if (in)  end = start + fourBeats;
                    else     start = juce::jmax (te::TimePosition(), end - fourBeats);
                }

                session.setLoopRange (start, end);

                if (! session.setPunch (true))
                    session.showHint (session.getLastError());
                else
                    session.showHint (in ? "Punch in set at the playhead (recording starts there)" : "Punch out set at the playhead (recording stops there)");

                commandManager.commandStatusChanged();
                return true;
            }

            case CommandIDs::renameTrack:
            case CommandIDs::duplicateTrack:
            case CommandIDs::deleteTrack:
            case CommandIDs::moveTrackUp:
            case CommandIDs::moveTrackDown:
            case CommandIDs::previousTrack:
            case CommandIDs::nextTrack:
            {
                if (! arrangeTakesKeys())
                    return true;

                const auto n = session.getNumAudioTracks();
                const auto t = timeline.getFocusedTrack();
                const auto id = info.commandID;

                if (id == CommandIDs::previousTrack || id == CommandIDs::nextTrack)
                {
                    if (n > 0)
                    {
                        const auto next = juce::jlimit (0, n - 1, t < 0 ? 0 : t + (id == CommandIDs::nextTrack ? 1 : -1));
                        timeline.setFocusedTrack (next);
                        session.showHint ("\"" + session.getTrackName (next) + "\": M mutes, S solos, Shift+R arms, F2 renames");
                    }

                    return true;
                }

                if (! juce::isPositiveAndBelow (t, n))
                {
                    session.showHint ("Click a track first (or Up / Down), then press the key again");
                    return true;
                }

                bool ok = true;

                if (id == CommandIDs::renameTrack)
                {
                    juce::Component::SafePointer<MainComponent> safe (this);
                    TrackMenus::askForName ("Rename track", "New name:", session.getTrackName (t), [safe, t] (const juce::String& name)
                    {
                        if (safe != nullptr && ! safe->session.renameTrack (t, name))
                            safe->session.showHint (safe->session.getLastError());
                    });
                }
                else if (id == CommandIDs::duplicateTrack)
                {
                    ok = session.duplicateTrack (t);
                }
                else if (id == CommandIDs::deleteTrack)
                {
                    ok = session.deleteTrack (t);

                    if (ok)
                        timeline.setFocusedTrack (juce::jmin (t, session.getNumAudioTracks() - 1));
                }
                else
                {
                    const auto to = t + (id == CommandIDs::moveTrackDown ? 1 : -1);

                    if (juce::isPositiveAndBelow (to, n))
                    {
                        ok = session.moveTrack (t, to);

                        if (ok)
                            timeline.setFocusedTrack (to);
                    }
                }

                if (! ok)
                    session.showHint (session.getLastError());

                return true;
            }

            case CommandIDs::toggleFollow:
                timeline.setFollowing (! timeline.isFollowing());
                session.showHint (timeline.isFollowing() ? "The view follows the playhead (Shift+F)" : "The view stays put while playing (Shift+F follows again)");
                commandManager.commandStatusChanged();
                return true;

            case CommandIDs::zoomToFit:
                if (arrangeTakesKeys())
                    timeline.zoomToFit();

                return true;

            case CommandIDs::loopSelection:
                if (! timeline.loopSelection())
                    session.showHint (session.getSelectedClips().isEmpty() ? juce::String ("Select the clips to loop round first") : session.getLastError());
                else
                    session.showHint ("Looping round the selected clips (L turns the loop off)");

                commandManager.commandStatusChanged();
                return true;

            case CommandIDs::tapTempo:
                if (arrangeTakesKeys() && ! session.tapTempo())
                    session.showHint (session.getLastError());

                return true;

            case CommandIDs::fadeClips:
            case CommandIDs::normaliseClips:
            case CommandIDs::reverseClips:
            case CommandIDs::followTempoClips:
            case CommandIDs::transposeClipsUp:
            case CommandIDs::transposeClipsDown:
            {
                if (! arrangeTakesKeys())
                    return true;

                const auto id = info.commandID;
                auto& s = session;

                // Toggles go the same way for every clip: by the first one
                bool turnOn = true;

                for (auto* c : session.getSelectedClips())
                {
                    if (c != nullptr && dynamic_cast<te::AudioClipBase*> (c) != nullptr)
                    {
                        turnOn = id == CommandIDs::reverseClips ? ! session.isClipReversed (*c) : ! session.isClipFollowingTempo (*c);
                        break;
                    }
                }

                const bool ok = session.editSelectedAudioClips ([&s, id, turnOn] (te::Clip& c)
                {
                    switch (id)
                    {
                        case CommandIDs::fadeClips:          return s.setClipFades (c, juce::jmax (0.01, s.getClipFadeIn (c)), juce::jmax (0.01, s.getClipFadeOut (c)));
                        case CommandIDs::normaliseClips:     return s.normaliseClip (c);
                        case CommandIDs::reverseClips:       return s.setClipReversed (c, turnOn);
                        case CommandIDs::followTempoClips:   return s.setClipFollowsTempo (c, turnOn);
                        case CommandIDs::transposeClipsUp:   return s.setClipTranspose (c, s.getClipTranspose (c) + 1.0f);
                        case CommandIDs::transposeClipsDown: return s.setClipTranspose (c, s.getClipTranspose (c) - 1.0f);
                        default:                             return false;
                    }
                });

                if (! ok)
                    session.showHint (session.getLastError());
                else if (id == CommandIDs::fadeClips)
                    session.showHint ("Short fades in and out: no clicks (drag the corner handles for longer)");
                else if (id == CommandIDs::reverseClips)
                    session.showHint (turnOn ? "Reversed (Ctrl+Alt+R again: forwards)" : "Forwards again");
                else if (id == CommandIDs::transposeClipsUp || id == CommandIDs::transposeClipsDown)
                    if (auto* first = session.getSelectedClips().getFirst())
                        session.showHint ("Transposed: " + juce::String ((int) session.getClipTranspose (*first)) + " semitones (Alt+Up / Alt+Down)");

                return true;
            }

            case CommandIDs::toggleProView:
                UiMode::setPro (! UiMode::isPro());
                session.showHint (UiMode::isPro() ? "Pro view: routing, inserts, punch and MIDI sync on screen (Ctrl+Shift+P for Easy)"
                                                  : "Easy view: just what most songs need (Ctrl+Shift+P for Pro)");
                return true;

            case CommandIDs::showShortcuts:
                if (shortcutsWindow != nullptr)
                {
                    shortcutsWindow = nullptr;
                }
                else
                {
                    juce::Component::SafePointer<MainComponent> safe (this);
                    shortcutsWindow = createShortcutsWindow (commandManager, [safe]
                    {
                        if (safe != nullptr)
                        {
                            safe->shortcutsWindow = nullptr;
                            safe->commandManager.commandStatusChanged();
                        }
                    });
                    shortcutsWindow->setVisible (true);
                }

                commandManager.commandStatusChanged();
                return true;

            case CommandIDs::startTour:
                beginTour();
                return true;

            case CommandIDs::showAbout:
                showAboutBox();
                return true;

            case CommandIDs::openLogFolder:
                AppPaths::createFolders();
                if (! AppPaths::getLogsFolder().startAsProcess())
                    showError ("Could not open the log folder", AppPaths::getLogsFolder().getFullPathName());
                return true;

            case CommandIDs::midiLearn:
            {
                auto& learn = session.getMidiLearn();

                if (learn.isLearning())
                {
                    learn.stopLearning();
                    session.showHint ("MIDI learn stopped");
                }
                else
                {
                    learn.startLearning();
                }

                commandManager.commandStatusChanged();
                return true;
            }

            case CommandIDs::toggleMidiClock:
            {
                auto& learn = session.getMidiLearn();
                const bool on = ! learn.isSendingClockAnywhere();
                int count = 0;

                for (const auto& o : learn.getClockOutputs())
                {
                    // On: every MIDI output except the computer's own synth (it has no use for a clock)
                    if (on && o.name.containsIgnoreCase ("GS Wavetable"))
                        continue;

                    if (o.sending != on && learn.setSendingClock (o.id, on))
                        ++count;
                }

                if (on && count == 0)
                    session.showHint ("No MIDI outputs to send a clock to: connect your drum machine or groovebox by USB or MIDI");
                else if (! on)
                    session.showHint ("MIDI clock off");

                commandManager.commandStatusChanged();
                return true;
            }

            case CommandIDs::nextTake:
            case CommandIDs::previousTake:
                if (arrangeTakesKeys() && ! session.stepSelectedTakes (info.commandID == CommandIDs::nextTake ? 1 : -1))
                    session.showHint (session.getLastError());

                return true;

            case CommandIDs::showTuner:
                if (tunerWindow != nullptr)
                    tunerWindow = nullptr;
                else
                    openTuner (-1);

                commandManager.commandStatusChanged();
                return true;

            case CommandIDs::toggleReference:
                session.setReferenceListening (! session.isReferenceListening());
                session.showHint (session.isReferenceListening() ? "B: the reference (Ctrl+Shift+F for your mix)" : "A: your mix");
                commandManager.commandStatusChanged();
                return true;

            case CommandIDs::toggleBrowser:
                if (! browser->isVisible() && ! hasRoomForBrowser())
                {
                    showError ("No room for the browser", "Make the window wider to show the browser.");
                    return true;
                }

                browserVisible = ! browser->isVisible();
                resized();
                return true;
            case CommandIDs::toggleLoop:
                if (! session.setLooping (! session.isLooping()))
                    showError ("Loop", session.getLastError());
                return true;
            case CommandIDs::toggleSnap:    timeline.toggleSnap(); commandManager.commandStatusChanged(); return true;
            case CommandIDs::zoomIn:        if (arrangeTakesKeys()) timeline.zoom (1.5); return true;
            case CommandIDs::zoomOut:       if (arrangeTakesKeys()) timeline.zoom (1.0 / 1.5); return true;

            // While you're working on notes, the editing keys act on them, not on the arrangement
            case CommandIDs::deleteSelection:
                if (auto* editor = focusedEditor())   editor->deleteSelection();
                else if (arrangeTakesKeys())                          timeline.deleteSelection();
                return true;
            case CommandIDs::selectAll:
                if (auto* editor = focusedEditor())   editor->selectAll();
                else if (arrangeTakesKeys())                          timeline.selectAll();
                return true;
            case CommandIDs::deselectAll:
                if (auto* editor = focusedEditor())   editor->deselectAll();
                else if (arrangeTakesKeys())                          timeline.deselectAll();
                return true;
            case CommandIDs::nudgeLeft:
            case CommandIDs::nudgeRight:
            {
                const auto direction = info.commandID == CommandIDs::nudgeLeft ? -1 : 1;

                if (auto* editor = focusedEditor())   editor->nudge (direction);
                else if (mixShown && mixer->hasEditFocus())           mixer->moveFocus (direction);
                else if (arrangeTakesKeys())                          timeline.nudge (direction);

                return true;
            }
            case CommandIDs::copy:
            case CommandIDs::cut:
            case CommandIDs::paste:
            case CommandIDs::duplicate:
            case CommandIDs::splitClips:
                if (auto* editor = focusedEditor())
                {
                    auto& roll = *editor;   // notes, never the clips you can't see are selected

                    if (info.commandID == CommandIDs::copy)            roll.copyNotes();
                    else if (info.commandID == CommandIDs::cut)        roll.cutNotes();
                    else if (info.commandID == CommandIDs::paste)      roll.pasteNotes();
                    else if (info.commandID == CommandIDs::duplicate)  roll.duplicateNotes();

                    return true;   // (splitting clips does nothing while you are editing notes)
                }

                if (! arrangeTakesKeys())
                    return true;   // the arrange is hidden: its clips are not what you're working on

                if (info.commandID == CommandIDs::copy)            timeline.copy();
                else if (info.commandID == CommandIDs::cut)        timeline.cut();
                else if (info.commandID == CommandIDs::paste)      timeline.paste();
                else if (info.commandID == CommandIDs::duplicate)  timeline.duplicate();
                else                                               timeline.splitAtPlayhead();

                return true;
            case CommandIDs::goToEnd:       timeline.goToEnd(); return true;
            case CommandIDs::togglePlay:    session.togglePlay(); return true;
            case CommandIDs::toggleRecord:  session.toggleRecord(); return true;
            case CommandIDs::returnToStart: session.returnToStart(); return true;
            case CommandIDs::toggleClick:   session.setClickEnabled (! session.isClickEnabled()); return true;
            case CommandIDs::toggleCountIn: session.setCountInEnabled (! session.isCountInEnabled()); return true;
            case CommandIDs::addTrack:      session.addAudioTrack(); return true;
            case CommandIDs::importAudio:   chooseAudioFileToImport(); return true;
            case CommandIDs::addInstrumentTrack: showInstrumentMenu (nullptr); return true;
            case CommandIDs::undo:          session.undo(); return true;
            case CommandIDs::redo:          session.redo(); return true;

            case CommandIDs::saveSong:
                if (! session.saveSong())
                    showError ("Save failed", session.getLastError());
                return true;

            case CommandIDs::saveSongAs:
            {
                juce::Component::SafePointer<MainComponent> safe (this);
                TrackMenus::askForName ("Save as new song", "A name for the copy (the original stays as it was last saved):",
                                        session.getSongName() + " 2", [safe] (const juce::String& name)
                                        {
                                            if (safe != nullptr && ! safe->session.saveSongAs (name))
                                                safe->showError ("Save as failed", safe->session.getLastError());
                                        });
                return true;
            }

            case CommandIDs::songMenu:
                appBar->showSongMenu();
                return true;

            default:
                return false;
        }
    }

    //==============================================================================
    void MainComponent::sessionSongClosing()
    {
    }

    void MainComponent::sessionSongChanged()
    {
        updateWindowTitle();
        commandManager.commandStatusChanged();
    }

    void MainComponent::sessionStateChanged()
    {
        commandManager.commandStatusChanged();
    }

    void MainComponent::sessionProblem (const juce::String& message)
    {
        showError ("Problem", message);
    }

    //==============================================================================
    void MainComponent::openSongFromSystem (const juce::File& file)
    {
        if (! file.existsAsFile())
            return;

        if (! startupSongOpened)
        {
            songFromSystem = file;   // openStartupSong (still to run) opens it instead of the last song
            return;
        }

        if (file == session.getSongFile())
            return;

        if (session.isRecording())
        {
            showError ("Still recording", "Stop recording, then open \"" + file.getFileNameWithoutExtension() + "\" again.");
            return;
        }

        openSongWithRecoveryCheck (file);
    }

    void MainComponent::openStartupSong()
    {
        startupSongOpened = true;

        if (songFromSystem.existsAsFile())
        {
            openSongWithRecoveryCheck (std::exchange (songFromSystem, {}), true);
            return;
        }

        const auto last = AppPaths::getLastSongFile();

        if (last.existsAsFile())
            openSongWithRecoveryCheck (last, true);
        else if (! session.createSong ("My First Song"))
            showError ("Could not create a song", session.getLastError());
    }

    void MainComponent::openSongWithRecoveryCheck (const juce::File& file, bool createNewSongIfItFails)
    {
        // The open song's own autosave is not a crash: only ask about songs that aren't open
        if (file == session.getSongFile() || ! Session::hasNewerAutosave (file))
        {
            openSongOrReport (file, false, createNewSongIfItFails);
            return;
        }

        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Restore unsaved work?")
                           .withMessage ("\"" + file.getFileNameWithoutExtension() + "\" has unsaved changes from a session "
                                         "that didn't close properly.\n\nRestore them? Both versions are kept either way "
                                         "(as .bak files in the song folder).")
                           .withButton ("Restore", 1)
                           .withButton ("Use last save", 0)
                           .withAssociatedComponent (this);

        questionBox = juce::AlertWindow::showScopedAsync (options, [this, file, createNewSongIfItFails] (int result)
        {
            openSongOrReport (file, result == 1, createNewSongIfItFails);
        });
    }

    void MainComponent::openSongOrReport (const juce::File& file, bool useAutosave, bool createNewSongIfItFails)
    {
        if (session.openSong (file, useAutosave))
            return;

        showError ("Could not open the song", session.getLastError());

        if (createNewSongIfItFails && ! session.hasSong() && ! session.createSong ("My First Song"))
            showError ("Could not create a song", session.getLastError());
    }

    void MainComponent::askForNewSongName (int templateIndex)
    {
        // Start from: an empty song or a template (built in, then yours)
        const auto templates = session.getPresets (Session::PresetKind::songTemplate);
        juce::StringArray choices { "Empty song (8 tracks)" };

        for (const auto& t : templates)
            choices.add (t.name + (t.isBuiltIn() ? juce::String() : juce::String (" (yours)")));

        auto* window = new juce::AlertWindow ("New Song", "Give your song a name:", juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", templateIndex >= 0 && templateIndex < templates.size() ? "New " + templates[templateIndex].name : juce::String ("New Song"));
        window->addComboBox ("from", choices, "Start from");

        if (auto* box = window->getComboBoxComponent ("from"))
            box->setSelectedItemIndex (juce::jlimit (0, choices.size() - 1, templateIndex + 1), juce::dontSendNotification);

        window->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        window->enterModalState (true, juce::ModalCallbackFunction::create (
            [safeThis = juce::Component::SafePointer<MainComponent> (this), window, templates] (int result)
            {
                if (safeThis == nullptr || result != 1)
                    return;

                const auto name = window->getTextEditorContents ("name").trim().isNotEmpty() ? window->getTextEditorContents ("name").trim()
                                                                                              : juce::String ("New Song");
                const auto from = window->getComboBoxComponent ("from") != nullptr ? window->getComboBoxComponent ("from")->getSelectedItemIndex() - 1 : -1;
                auto& s = safeThis->session;
                const bool ok = juce::isPositiveAndBelow (from, templates.size()) ? s.createSongFromTemplate (name, templates[from])
                                                                                   : s.createSong (name);

                if (! ok)
                    safeThis->showError ("Could not create the song", s.getLastError());
            }), true);
    }

    void MainComponent::chooseSongToOpen()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Open a song", AppPaths::getProjectsFolder(),
                                                           juce::String ("*") + AppPaths::songFileExtension);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
                                  {
                                      auto file = chooser.getResult();

                                      if (file.existsAsFile())
                                          openSongWithRecoveryCheck (file);
                                  });
    }

    namespace
    {
        void addInstrumentItems (juce::PopupMenu& menu, const juce::Array<Session::InstrumentChoice>& choices, int firstId)
        {
            bool shownPluginHeader = false;

            for (int i = 0; i < choices.size(); ++i)
            {
                const auto& choice = choices.getReference (i);

                if (choice.pluginType == te::ExternalPlugin::xmlTypeName && ! shownPluginHeader)
                {
                    menu.addSectionHeader ("Plugin instruments");
                    shownPluginHeader = true;
                }

                menu.addItem (firstId + i, choice.name);
            }

            if (! shownPluginHeader)
                menu.addSectionHeader ("Setup > Plugins scans your VST3 instruments");
        }
    }

    void MainComponent::showAddTrackMenu (juce::Component* anchor)
    {
        enum { audioTrackId = 1, importId = 2, firstInstrumentId = 100 };

        const auto choices = session.getAvailableInstruments();
        juce::PopupMenu instruments;
        addInstrumentItems (instruments, choices, firstInstrumentId);

        juce::PopupMenu menu;
        menu.addItem (audioTrackId, "Audio track (for recording)", session.hasSong());
        menu.addSubMenu ("Instrument track", instruments, session.hasSong() && ! session.isRecording());
        menu.addSeparator();
        menu.addItem (importId, "Import audio file...", session.hasSong());

        auto options = juce::PopupMenu::Options();
        options = anchor != nullptr ? options.withTargetComponent (anchor) : options.withMousePosition();

        menu.showMenuAsync (options, [safeThis = juce::Component::SafePointer<MainComponent> (this), choices] (int result)
        {
            if (safeThis == nullptr || result <= 0)
                return;

            if (result == audioTrackId)
                safeThis->session.addAudioTrack();
            else if (result == importId)
                safeThis->chooseAudioFileToImport();
            else if (juce::isPositiveAndBelow (result - firstInstrumentId, choices.size())
                     && ! safeThis->session.addInstrumentTrack (choices.getReference (result - firstInstrumentId), true))
                safeThis->showError ("Could not add the instrument", safeThis->session.getLastError());
        });
    }

    void MainComponent::showInstrumentMenu (juce::Component* anchor)
    {
        const auto choices = session.getAvailableInstruments();
        juce::PopupMenu menu;
        addInstrumentItems (menu, choices, 1);

        auto options = juce::PopupMenu::Options();
        options = anchor != nullptr ? options.withTargetComponent (anchor) : options.withMousePosition();

        menu.showMenuAsync (options, [safeThis = juce::Component::SafePointer<MainComponent> (this), choices] (int result)
                            {
                                if (safeThis == nullptr || result <= 0)
                                    return;

                                if (! safeThis->session.addInstrumentTrack (choices.getReference (result - 1), true))
                                    safeThis->showError ("Could not add the instrument", safeThis->session.getLastError());
                            });
    }

    void MainComponent::chooseAudioFileToImport()
    {
        auto& formats = audioEngine.getEngine().getAudioFileFormatManager().readFormatManager;

        fileChooser = std::make_unique<juce::FileChooser> ("Import audio",
                                                           juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                           formats.getWildcardForAllFormats());

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
                                  {
                                      auto file = chooser.getResult();

                                      if (file.existsAsFile() && ! session.importAudioFile (file))
                                          showError ("Import failed", session.getLastError());
                                  });
    }

    void MainComponent::showError (const juce::String& title, const juce::String& message)
    {
        pendingErrors.emplace_back (title, message);

        // Shown on the next message loop turn, so it is safe to call from inside any callback
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safeThis != nullptr)
                safeThis->showNextError();
        });
    }

    void MainComponent::showNextError()
    {
        if (errorBoxShowing || pendingErrors.empty())
            return;

        const auto [title, message] = pendingErrors.front();
        pendingErrors.pop_front();
        errorBoxShowing = true;

        errorBox = juce::AlertWindow::showScopedAsync (
            juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon, title, message, "OK", this),
            [safeThis = juce::Component::SafePointer<MainComponent> (this)] (int)
            {
                if (safeThis == nullptr)
                    return;

                safeThis->errorBoxShowing = false;

                // Not from inside this callback: showing the next box replaces errorBox
                juce::MessageManager::callAsync ([safeThis]
                {
                    if (safeThis != nullptr)
                        safeThis->showNextError();
                });
            });
    }

    void MainComponent::updateWindowTitle()
    {
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
            window->setName (session.hasSong() ? "EzDAWsIt - " + session.getSongName() : juce::String ("EzDAWsIt"));
    }

    void MainComponent::openTuner (int trackIndex)
    {
        if (trackIndex >= 0)
            timeline.setFocusedTrack (trackIndex);   // the tuner follows the highlighted track

        if (tunerWindow == nullptr)
        {
            juce::Component::SafePointer<MainComponent> safe (this);
            tunerWindow = createTunerWindow (session, [safe] { return safe != nullptr ? safe->timeline.getFocusedTrack() : -1; },
                                             [safe]
                                             {
                                                 if (safe != nullptr)
                                                 {
                                                     safe->tunerWindow = nullptr;
                                                     safe->commandManager.commandStatusChanged();
                                                 }
                                             });
        }

        tunerWindow->setVisible (true);
        tunerWindow->toFront (true);
    }

    void MainComponent::showAboutBox()
    {
        const auto app = juce::JUCEApplication::getInstance();
        const auto notices = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("THIRD-PARTY-NOTICES.txt");

        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::NoIcon)
                           .withTitle ("About EzDAWsIt")
                           .withMessage (app->getApplicationName() + " " + app->getApplicationVersion() + "\n"
                                         "Published by EzDAW\n\n"
                                         "Free software under the GNU General Public License v3: you may use, share and change it. "
                                         "It comes with no warranty.\n\n"
                                         "Built on JUCE and Tracktion Engine, with ASIO and VST3 support from Steinberg. "
                                         "\"Licences\" lists every part and its licence.")
                           .withButton ("Licences", 1)
                           .withButton ("Log folder", 2)
                           .withButton ("Close", 0)
                           .withAssociatedComponent (this);

        questionBox = juce::AlertWindow::showScopedAsync (options, [safe = juce::Component::SafePointer<MainComponent> (this), notices] (int result)
        {
            if (safe == nullptr)
                return;

            if (result == 1)
            {
                if (! notices.existsAsFile() || ! notices.startAsProcess())
                    safe->showError ("Licences", "The licence list (THIRD-PARTY-NOTICES.txt) is installed next to EzDAWsIt.exe by the installer.");
            }
            else if (result == 2)
            {
                safe->commandManager.invokeDirectly (CommandIDs::openLogFolder, true);
            }
        });
    }

    void MainComponent::beginTour()
    {
        if (view != View::arrange)
            showView (View::arrange);

        juce::Component::SafePointer<MainComponent> safe (this);
        const auto boundsOf = [safe] (auto member)
        {
            return [safe, member]() -> juce::Rectangle<int>
            {
                if (safe == nullptr)
                    return {};

                auto* c = member (*safe);
                return c != nullptr && c->isVisible() ? c->getBounds() : juce::Rectangle<int>();
            };
        };

        std::vector<TourOverlay::Step> steps
        {
            { "Welcome to EzDAWsIt", "Record, make beats, mix and master, all in one place. This tour shows the screen one part at a time "
              "(about a minute). First time? Check your sound in Setup (Ctrl+,) after the tour.", nullptr },
            { "The app bar", "New, Open and Save your songs. Arrange, Mix and Master switch the main view (Tab). "
              "Export (Ctrl+R) makes the finished file. Easy | Pro shows less or more.",
              boundsOf ([] (MainComponent& m) -> juce::Component* { return m.appBar.get(); }) },
            { "The transport", "Play (Space), record (R), back to the start (Home), loop (L) and the click. Drag the tempo up or down. "
              "The meter on the right shows how loud the song is.",
              boundsOf ([] (MainComponent& m) -> juce::Component* { return m.transportBar.get(); }) },
            { "Tracks and clips", "Each track has mute (M), solo (S) and arm (Shift+R). Right-click a track for colours, presets and the Tuner. "
              "Drag clips to move them, Ctrl+E splits, double-click opens the editor.",
              boundsOf ([] (MainComponent& m) -> juce::Component* { return &m.timeline; }) },
            { "The browser", "Song templates, track presets, effect chains, instruments, drum kits and your samples. "
              "Double-click anything to add it (to the highlighted track, or a new one), drag sounds and loops onto a track, "
              "right-click for more. Ctrl+F searches everything. B shows or hides it.",
              boundsOf ([] (MainComponent& m) -> juce::Component* { return m.browser.get(); }) },
            { "The bottom panel", "The highlighted track's instrument and effects, or the clip you're editing: the piano roll "
              "or the drum machine (D opens and closes it).",
              boundsOf ([] (MainComponent& m) -> juce::Component* { return m.detailPanel.get(); }) },
            { "Tips appear here", "What just happened, and what you can do next, shows in the status bar.",
              boundsOf ([] (MainComponent& m) -> juce::Component* { return m.statusBar.get(); }) },
            { "That's it", "Every command and its shortcut is in Help > Keyboard shortcuts (F1). "
              "Tip: Song templates in the browser give you a whole band, beat or podcast set up with a double-click.", nullptr },
        };

        tour = std::make_unique<TourOverlay> (std::move (steps), [safe]
        {
            if (safe != nullptr)
            {
                safe->tour = nullptr;
                safe->grabKeyboardFocus();   // the shortcuts work again
            }
        });

        addAndMakeVisible (*tour);
        tour->setBounds (getLocalBounds());

        if (auto* overlay = dynamic_cast<TourOverlay*> (tour.get()))
            overlay->showStep (0);
    }
}
