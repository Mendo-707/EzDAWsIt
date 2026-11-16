/*
    EzDAWsIt - a powerful but easy DAW.
    Copyright (C) 2026 EzDAWsIt contributors. Licensed under the GNU GPL v3.
    Built on JUCE and Tracktion Engine (both used under GPLv3).

    App start-up and shut-down. Object lifetimes (created top to bottom,
    destroyed bottom to top):
        Diagnostics -> LookAndFeel -> AudioEngine -> Session -> MainWindow
*/

#include "UI/InterfaceScale.h"
#include "UI/UiMode.h"
#include "Core/Common.h"
#include "Core/Diagnostics.h"
#include "Engine/AudioEngine.h"
#include "Engine/Session.h"
#include "Core/AppPaths.h"
#include "Tests/TestRunner.h"
#include "Tests/Screenshots.h"
#include "UI/PluginWindows.h"
#include "UI/Dialogs.h"
#include "UI/MainComponent.h"
#include "UI/Theme.h"

namespace ez
{
    class Application : public juce::JUCEApplication
    {
    public:
        Application() = default;

        const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
        const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }

        bool moreThanOneInstanceAllowed() override
        {
            // Tests and the plugin scanner run alongside the app; the app itself is single-instance
            return getCommandLineParameters().contains (Tests::commandLineFlag)
                || getCommandLineParameters().contains (Screenshots::commandLineFlag)
                || isPluginScanner (getCommandLineParameters());
        }

        /** The app starts a copy of itself to scan plugins (see AudioEngine::Behaviour). */
        static bool isPluginScanner (const juce::String& commandLine)
        {
            return commandLine.contains ("--PluginScan:");
        }

        void initialise (const juce::String& commandLine) override
        {
            // A plugin-scanning copy: no window, no log, no audio. It scans what it's asked to and exits.
            if (isPluginScanner (commandLine))
            {
                if (! te::PluginManager::startChildProcessPluginScan (commandLine))
                    quit();   // started by hand, or the app that asked has gone: never open a second copy

                return;
            }

            if (commandLine.contains (Tests::commandLineFlag))
            {
                runTestsAndQuit (commandLine);
                return;
            }

            if (commandLine.contains (Screenshots::commandLineFlag))
            {
                auto args = juce::StringArray::fromTokens (commandLine, true);
                const auto i = args.indexOf (Screenshots::commandLineFlag);
                const auto folder = i + 1 < args.size() ? juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted())
                                                        : juce::File::getCurrentWorkingDirectory().getChildFile ("screenshots");
                diagnostics = std::make_unique<Diagnostics> (getApplicationName() + " screenshots", getApplicationVersion());
                juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
                setApplicationReturnValue (Screenshots::run (folder) == 0 ? 0 : 1);
                juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
                quit();
                return;
            }

            diagnostics = std::make_unique<Diagnostics> (getApplicationName(), getApplicationVersion());

            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

            audioEngine = std::make_unique<AudioEngine> (getApplicationName(), createUIBehaviour());
            session     = std::make_unique<Session> (*audioEngine);
            InterfaceScale::applySaved (audioEngine->getEngine());   // before the window: it opens at the right size
            UiMode::initialise (audioEngine->getEngine());
            mainWindow  = std::make_unique<MainWindow> (getApplicationName(), *audioEngine, *session);
            mainWindow->openSongFromSystem (AppPaths::findSongInCommandLine (commandLine));

            // The last run ended badly: offer its log, which is what a bug report needs
            if (diagnostics->getUncleanPreviousLog().existsAsFile())
                mainWindow->offerPreviousRunLog (diagnostics->getUncleanPreviousLog(), diagnostics->previousRunCrashed());
        }

        void shutdown() override
        {
            Dialogs::closeAll();     // they reference the AudioEngine
            mainWindow  = nullptr;
            session     = nullptr;   // closes and saves the open song
            audioEngine = nullptr;

            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            diagnostics = nullptr;
        }

        void systemRequestedQuit() override
        {
            Dialogs::closeAll();

            // Save while the window can still show a problem. If the save failed, the song is closed
            // (an emergency copy was attempted) and the error is on screen: closing again quits.
            if (session != nullptr && ! session->closeSong())
                return;

            quit();
        }

        void anotherInstanceStarted (const juce::String& commandLine) override
        {
            // A song double-clicked while EzDAWsIt is already open comes here
            if (mainWindow != nullptr)
            {
                mainWindow->toFront (true);
                mainWindow->openSongFromSystem (AppPaths::findSongInCommandLine (commandLine));
            }
        }

        void unhandledException (const std::exception* e, const juce::String& sourceFile, int lineNumber) override
        {
            log ("Unhandled exception: " + juce::String (e != nullptr ? e->what() : "unknown")
                 + " at " + sourceFile + ":" + juce::String (lineNumber));
        }

    private:
        //==============================================================================
        class MainWindow : public juce::DocumentWindow
        {
        public:
            MainWindow (const juce::String& name, AudioEngine& engine, Session& session)
                : DocumentWindow (name, Theme::background, juce::DocumentWindow::allButtons)
            {
                setUsingNativeTitleBar (true);
                setContentOwned (new MainComponent (engine, session), true);
                setResizable (true, false);
                setResizeLimits (Theme::minWindowWidth, Theme::minWindowHeight, 10000, 10000);
                centreWithSize (getWidth(), getHeight());
                setVisible (true);
                InterfaceScale::fitWindows();   // a big interface size on a small screen: keep the title bar reachable
            }

            void openSongFromSystem (const juce::File& file)
            {
                if (auto* main = dynamic_cast<MainComponent*> (getContentComponent()))
                    main->openSongFromSystem (file);
            }

            void offerPreviousRunLog (const juce::File& log, bool crashed)
            {
                if (auto* main = dynamic_cast<MainComponent*> (getContentComponent()))
                    main->offerPreviousRunLog (log, crashed);
            }

            void closeButtonPressed() override
            {
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }

        private:
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
        };

        //==============================================================================
        void runTestsAndQuit (const juce::String& commandLine)
        {
            const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            auto args = juce::StringArray::fromTokens (commandLine, true);
            auto resultsFile = exe.getSiblingFile ("test-results.txt");

            if (auto i = args.indexOf (Tests::commandLineFlag); i >= 0 && i + 1 < args.size())
                resultsFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted());

            // Everything a test run writes (songs, logs) goes next to the exe, never into Documents
            const auto testRoot = exe.getSiblingFile ("test-run");
            testRoot.deleteRecursively();
            AppPaths::setRootFolderOverride (testRoot);
            diagnostics = std::make_unique<Diagnostics> (getApplicationName() + " tests", getApplicationVersion());

            const auto failures = Tests::runAll (resultsFile);
            setApplicationReturnValue (failures == 0 ? 0 : 1);
            quit();
        }

        std::unique_ptr<Diagnostics> diagnostics;
        Theme::LookAndFeel lookAndFeel;
        std::unique_ptr<AudioEngine> audioEngine;
        std::unique_ptr<Session> session;
        std::unique_ptr<MainWindow> mainWindow;
    };
}

//==============================================================================
START_JUCE_APPLICATION (ez::Application)
