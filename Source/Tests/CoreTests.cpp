/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Each test says WHY the behaviour matters to the user, and is written so it
    fails if that behaviour breaks. Tests use a headless engine with its own
    settings name and a throwaway folder: they never touch real songs, real
    audio settings or the audio device.
*/

#include "../Engine/Mastering.h"
#include "../Engine/SampleLibrary.h"
#include "TestRunner.h"
#include "../Core/AppPaths.h"
#include "../Engine/AudioEngine.h"
#include "../Engine/Effects.h"
#include "../Engine/VocalChain.h"
#include "../Engine/Session.h"
#include "../UI/RecordingSetup.h"
#include "../Engine/MidiLearn.h"
#include "../Core/Version.h"
#include "../Core/Diagnostics.h"
#include "../Engine/PluginPresets.h"
#include "../UI/PluginLayouts.h"
#include "../UI/PluginDisplays.h"
#include "../UI/PluginLook.h"
#include "../UI/ParamEditor.h"

namespace ez::Tests
{
    namespace
    {
        /** Points every app folder at a throwaway temp folder, restoring the previous root afterwards. */
        struct ScopedTestFolder
        {
            ScopedTestFolder()
            {
                folder.createDirectory();
                AppPaths::setRootFolderOverride (folder);
                AppPaths::createFolders();
            }

            ~ScopedTestFolder()
            {
                AppPaths::setRootFolderOverride (previousRoot);
                folder.deleteRecursively();
            }

            const juce::File previousRoot = AppPaths::getRootFolder();
            const juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                          .getNonexistentChildFile ("EzDAWsItTests", {}, false);
        };

        struct HeadlessBehaviour : public AudioEngine::Behaviour   // the app's limits, no audio device
        {
            bool autoInitialiseDeviceManager() override     { return false; }
        };

        /** Runs engine tasks (e.g. offline renders) to completion: the default UIBehaviour does
            nothing, which would make every render silent. Same approach as Tracktion's TestRunner. */
        struct TestUIBehaviour : public te::UIBehaviour
        {
            void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
            {
                TaskThread runner (task);

                while (runner.isThreadRunning())
                    if (! juce::MessageManager::getInstance()->runDispatchLoopUntil (10))
                        break;
            }

            struct TaskThread : public juce::Thread
            {
                explicit TaskThread (te::ThreadPoolJobWithProgress& t) : juce::Thread (t.getJobName()), task (t)
                {
                    startThread();
                }

                ~TaskThread() override
                {
                    task.signalJobShouldExit();
                    waitForThreadToExit (10000);
                }

                void run() override
                {
                    while (! threadShouldExit())
                        if (task.runJob() == juce::ThreadPoolJob::jobHasFinished)
                            break;
                }

                te::ThreadPoolJobWithProgress& task;
            };
        };

        std::unique_ptr<AudioEngine> createTestEngine()
        {
            return std::make_unique<AudioEngine> ("EzDAWsIt-Tests",
                                                  std::make_unique<TestUIBehaviour>(),
                                                  std::make_unique<HeadlessBehaviour>());
        }

        /** Tests run inside initialise(): let queued async work (device rescans etc.) happen. */
        void pumpMessages (int milliseconds)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
        }

        const juce::String testMidiInputName ("EzDAWsIt Test Keys");

        /** Creates the virtual MIDI input and waits until the engine has it. Works whether or not
            an earlier run left it in the test settings (the headless engine never loads saved devices). */
        bool ensureTestMidiInput (te::DeviceManager& dm)
        {
            dm.createVirtualMidiDevice (testMidiInputName);   // "already in use" is fine
            dm.rescanMidiDeviceList();

            for (int i = 0; i < 100 && dm.findInputDeviceWithName (testMidiInputName) == nullptr; ++i)
                pumpMessages (20);

            return dm.findInputDeviceWithName (testMidiInputName) != nullptr;
        }

        /** Leaves nothing behind for the next run. */
        void removeTestMidiInput (te::DeviceManager& dm)
        {
            if (auto* device = dynamic_cast<te::VirtualMidiInputDevice*> (dm.findInputDeviceWithName (testMidiInputName)))
                dm.deleteVirtualMidiDevice (*device);
        }

        bool writeTestWav (const juce::File& file, double seconds)
        {
            juce::WavAudioFormat wav;
            file.getParentDirectory().createDirectory();
            std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

            if (stream == nullptr)
                return false;

            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                           .withSampleRate (44100.0)
                                                           .withNumChannels (2)
                                                           .withBitsPerSample (24));
            if (writer == nullptr)
                return false;

            juce::AudioBuffer<float> buffer (2, (int) (44100.0 * seconds));

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                for (int ch = 0; ch < 2; ++ch)
                    buffer.setSample (ch, i, 0.25f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / 44100.0f));

            return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }

        /** Offline render of the whole song for `seconds`: the loudest sample, 0..1+. */
        float renderPeak (te::Edit& edit, double seconds)
        {
            const te::TimeRange range (te::TimePosition(), te::TimePosition::fromSeconds (seconds));
            return te::Renderer::measureStatistics ("EzDAWsIt test", edit, range,
                                                     te::toBitSet (te::getAllTracks (edit)), 512).peak;
        }

        int countFiles (const juce::File& folder, const juce::String& pattern)
        {
            return folder.findChildFiles (juce::File::findFiles, false, pattern).size();
        }
    }

    //==============================================================================
    class AppPathsTests : public juce::UnitTest
    {
    public:
        AppPathsTests() : juce::UnitTest ("AppPaths", ez::Tests::category) {}

        void runTest() override
        {
            ScopedTestFolder testFolder;

            beginTest ("Two songs with the same name never overwrite each other");
            const auto a = AppPaths::createSongFile ("Beat");
            const auto b = AppPaths::createSongFile ("Beat");
            expect (a.getParentDirectory().isDirectory());
            expect (a != b);
            expectEquals (a.getFileExtension(), juce::String (AppPaths::songFileExtension));

            beginTest ("Song names with illegal characters still get a valid folder");
            const auto c = AppPaths::createSongFile ("a/b:c*?");
            expect (c != juce::File());
            expect (c.getParentDirectory().getParentDirectory() == AppPaths::getProjectsFolder());

            beginTest ("A blank song name falls back to 'New Song'");
            expect (AppPaths::createSongFile ("   ").getFileNameWithoutExtension().startsWith ("New Song"));

            beginTest ("The last song is remembered so it reopens on start-up");
            AppPaths::rememberLastSongFile (a);
            expect (AppPaths::getLastSongFile() == a);

            beginTest ("A song double-clicked in Explorer is found on the command line");
            const auto spaced = AppPaths::createSongFile ("Song With Spaces");
            expect (spaced.replaceWithText ("x"));
            expect (AppPaths::findSongInCommandLine ("\"" + spaced.getFullPathName() + "\"") == spaced, "quoted");
            expect (AppPaths::findSongInCommandLine (spaced.getFullPathName()) == spaced, "unquoted, with spaces");
            expect (AppPaths::findSongInCommandLine ("--something \"" + spaced.getFullPathName() + "\"") == spaced, "after another argument");
            expect (AppPaths::findSongInCommandLine ({}) == juce::File(), "nothing");
            expect (AppPaths::findSongInCommandLine ("\"" + spaced.getSiblingFile ("missing.tracktionedit").getFullPathName() + "\"") == juce::File(),
                    "a song that isn't there");
            expect (AppPaths::findSongInCommandLine ("\"" + spaced.getParentDirectory().getFullPathName() + "\"") == juce::File(), "a folder");

            beginTest ("Tests write only inside their own folder");
            expect (AppPaths::getRootFolder() == testFolder.folder);
        }
    };

    //==============================================================================
    class SessionTests : public juce::UnitTest
    {
    public:
        SessionTests() : juce::UnitTest ("Session", ez::Tests::category) {}

        void runTest() override
        {
            ScopedTestFolder testFolder;
            auto engine = createTestEngine();
            Session session (*engine);

            beginTest ("A new song is written to disk straight away, with ready-to-use tracks");
            expect (session.createSong ("Test Song"), session.getLastError());
            const auto songFile = session.getSongFile();
            expect (songFile.existsAsFile());
            expectEquals (session.getNumAudioTracks(), Session::defaultTrackCount);

            beginTest ("Tempo can't be set to values the engine can't play");
            session.setBpm (1000.0);
            expectEquals (session.getBpm(), Session::maxBpm);
            session.setBpm (5.0);
            expectEquals (session.getBpm(), Session::minBpm);

            beginTest ("Save really writes to disk (checked by a second, independent session)");
            session.setBpm (97.0);
            session.addAudioTrack();
            expect (session.saveSong(), session.getLastError());
            {
                Session reader (*engine);
                expect (reader.openSong (songFile), reader.getLastError());
                expectEquals (reader.getBpm(), 97.0);
                expectEquals (reader.getNumAudioTracks(), Session::defaultTrackCount + 1);
                reader.closeSong();
            }

            beginTest ("Closing and reopening keeps everything");
            expect (session.closeSong());
            expect (! session.hasSong());
            expect (session.openSong (songFile), session.getLastError());
            expectEquals (session.getBpm(), 97.0);

            runCrashRecoveryTests (session, songFile);
            runBadInputTests (session, songFile, testFolder.folder);
            runImportTests (session, songFile, testFolder.folder);
            runInstrumentTests (session, songFile);
            runRoutingTests (session, songFile, *engine);
            runTimelineTests (session, songFile);
            runEditingTests (session, songFile, testFolder.folder);
            runNoteTests (session, songFile);
            runDrumTests (session, songFile, testFolder.folder);
            runTempoTests (session, songFile);
            runSectionTests (session, songFile);
            runMeterTests (session, songFile);
            runMixTests (session, songFile);
            runAutomationTests (session, songFile);
            runEffectTests (session, songFile);
            runMasterTests (session, testFolder.folder);
            runSampleLibraryTests (*engine, testFolder.folder);
            runPresetTests (session, testFolder.folder);
            runGuitarTests (session, songFile);
            runRecordingTests (session, songFile);
            runMidiLearnTests (session, songFile);
            runAssistantTests (session, songFile);
            runExportPresetTests (session);
            runAudioEditTests (session, songFile, testFolder.folder);
            runExportTests (session, testFolder.folder);
            runReleaseTests (session, songFile, testFolder.folder, engine->getEngine());
            runPluginEditorTests (session, songFile);

            session.closeSong();
            removeTestMidiInput (engine->getEngine().getDeviceManager());
        }

    private:
        /** What a public release needs beyond the features: updates, crash reports, file safety
            across versions, plugin safety, and a big song that still saves and loads quickly. */
        void runPluginEditorTests (Session& session, const juce::File& songFile)
        {
            expect (session.openSong (songFile), session.getLastError());

            beginTest ("Knob values read cleanly: no '0.000dB', centre pan is C, big numbers in kHz and s");
            expectEquals (PluginLayouts::tidyValueText ("0.000dB"), juce::String ("0 dB"));
            expectEquals (PluginLayouts::tidyValueText ("-6.000 dB"), juce::String ("-6 dB"));
            expectEquals (PluginLayouts::tidyValueText ("+1.50 dB"), juce::String ("+1.5 dB"));
            expectEquals (PluginLayouts::tidyValueText ("0R"), juce::String ("C"));
            expectEquals (PluginLayouts::tidyValueText ("34L"), juce::String ("34L"));
            expectEquals (PluginLayouts::tidyValueText ("17000 Hz"), juce::String ("17 kHz"));
            expectEquals (PluginLayouts::tidyValueText ("1500 ms"), juce::String ("1.5 s"));
            expectEquals (PluginLayouts::tidyValueText ("123.456 %"), juce::String ("123 %"));
            expectEquals (PluginLayouts::tidyValueText ("Sine"), juce::String ("Sine"));

            beginTest ("The response curves the editors draw are the real filter shapes");
            expectWithinAbsoluteError (PluginDisplays::Response::peak (1000.0f, 1000.0f, 6.0f, 1.0f), 6.0f, 0.1f);
            expectWithinAbsoluteError (PluginDisplays::Response::peak (50.0f, 1000.0f, 6.0f, 1.0f), 0.0f, 0.3f);
            expectWithinAbsoluteError (PluginDisplays::Response::lowPass (1000.0f, 1000.0f, 0.7071f), -3.0f, 0.2f);
            expect (PluginDisplays::Response::lowPass (8000.0f, 1000.0f, 0.7071f) < -30.0f, "a low-pass cuts the highs");
            expect (PluginDisplays::Response::highPass (100.0f, 1000.0f, 0.7071f) < -30.0f, "a high-pass cuts the lows");
            expectWithinAbsoluteError (PluginDisplays::Response::lowShelf (20.0f, 200.0f, -9.0f, 0.7071f), -9.0f, 0.5f);
            expectWithinAbsoluteError (PluginDisplays::Response::highShelf (15000.0f, 2000.0f, 4.0f, 0.7071f), 4.0f, 0.5f);

            // Every built-in plug-in, effects and the synth, on a fresh track
            const auto track = session.getNumAudioTracks();
            session.addAudioTrack();
            std::vector<te::Plugin::Ptr> plugins;

            for (const auto& choice : session.getAvailableEffects())
                if (choice.pluginType != te::ExternalPlugin::xmlTypeName && session.addEffect (track, choice, false))
                    if (auto* audioTrack = te::getAudioTracks (*session.getEdit())[track])
                        for (auto* p : audioTrack->pluginList.getPlugins())
                            if (p->getPluginType() == choice.pluginType)
                                plugins.push_back (p);

            const auto instruments = session.getAvailableInstruments();
            expect (! instruments.isEmpty() && session.addInstrumentTrack (instruments[0], false), session.getLastError());

            if (auto* synthTrack = te::getAudioTracks (*session.getEdit()).getLast())
                for (auto* p : synthTrack->pluginList.getPlugins())
                    if (dynamic_cast<te::FourOscPlugin*> (p) != nullptr)
                        plugins.push_back (p);

            beginTest ("Every control, display and factory preset in a plug-in window points at something real");
            {
                int laidOut = 0;

                for (auto& plugin : plugins)
                {
                    const auto layout = PluginLayouts::forPlugin (*plugin);

                    if (! layout.has_value())
                        continue;   // the editor shows every parameter instead

                    ++laidOut;
                    const auto settings = PluginLayouts::settingIds (*layout);
                    const auto resolves = [&] (const juce::String& id)
                    {
                        return plugin->getAutomatableParameterByID (id) != nullptr || settings.contains (id);
                    };

                    for (const auto& page : layout->pages)
                        for (const auto& row : page.rows)
                            for (const auto& section : row)
                            {
                                for (const auto& item : section.items)
                                    expect (resolves (item.id), plugin->getName() + ": control " + item.id);

                                for (const auto& id : section.displayIds)
                                    expect (resolves (id), plugin->getName() + ": display " + id);

                                if (section.powerId.isNotEmpty())
                                    expect (resolves (section.powerId), plugin->getName() + ": power " + section.powerId);

                                if (section.titleToggle.isNotEmpty())
                                    expect (resolves (section.titleToggle), plugin->getName() + ": title switch " + section.titleToggle);

                                for (const auto& item : section.items)
                                    if (item.enabledBy.isNotEmpty())
                                        expect (resolves (item.enabledBy), plugin->getName() + ": " + item.id + " depends on " + item.enabledBy);
                            }

                    for (const auto& preset : PluginPresets::getFactory (plugin->getPluginType()))
                        for (const auto& value : preset.values)
                            expect (resolves (value.id), plugin->getName() + " preset " + preset.name + ": " + value.id);
                }

                expect (laidOut >= 10, "the built-in plug-ins have designed windows, not a wall of knobs");
                expect (! PluginPresets::getFactory (te::FourOscPlugin::xmlTypeName).empty(), "the synth comes with sounds");
            }

            te::FourOscPlugin* synth = nullptr;
            for (auto& p : plugins)
                if (auto* s = dynamic_cast<te::FourOscPlugin*> (p.get()))
                    synth = s;

            expect (synth != nullptr);

            if (synth != nullptr)
            {
                const auto layout = PluginLayouts::forPlugin (*synth);
                expect (layout.has_value());
                const auto settings = layout.has_value() ? PluginLayouts::settingIds (*layout) : juce::StringArray();
                auto level = synth->getAutomatableParameterByID ("level1");
                expect (level != nullptr && settings.contains ("waveShape1"), "the synth's level and waveform are in its presets");

                if (level != nullptr)
                {
                    const auto readWave = [&] { return (int) synth->state.getProperty ("waveShape1", -1); };

                    beginTest ("A preset brings back the exact sound, including settings that aren't parameters");
                    synth->edit.getUndoManager().beginNewTransaction();
                    level->setParameter (-12.0f, juce::sendNotificationSync);
                    synth->state.setProperty ("waveShape1", 3, &synth->edit.getUndoManager());
                    const auto saved = PluginPresets::capture (*synth, settings);

                    synth->edit.getUndoManager().beginNewTransaction();
                    PluginPresets::resetToDefaults (*synth, settings);
                    expectWithinAbsoluteError (level->getCurrentValue(), level->getDefaultValue().value_or (0.0f), 0.01f);
                    expect (readWave() != 3, "reset puts the waveform back");

                    synth->edit.getUndoManager().beginNewTransaction();
                    PluginPresets::apply (*synth, saved, settings, true);
                    expectWithinAbsoluteError (level->getCurrentValue(), -12.0f, 0.01f);
                    expectEquals (readWave(), 3);

                    beginTest ("Loading a preset is one undo step: one Undo gets the previous sound back");
                    synth->edit.getUndoManager().beginNewTransaction();
                    PluginPresets::resetToDefaults (*synth, settings);
                    synth->edit.getUndoManager().beginNewTransaction();
                    PluginPresets::apply (*synth, saved, settings, true);
                    synth->edit.getUndoManager().undo();
                    // the song's state is what undo restores (the knob catches up on the next message loop turn)
                    expectWithinAbsoluteError ((float) synth->state.getProperty ("level1", 0.0f), level->getDefaultValue().value_or (0.0f), 0.01f);
                    expect (readWave() != 3);
                    synth->edit.getUndoManager().redo();
                    expectEquals (readWave(), 3);

                    beginTest ("Your own presets save to your presets folder, load back by name, and delete to the bin");
                    juce::String error;
                    const auto presetName = "Test sound " + juce::String (juce::Random::getSystemRandom().nextInt (100000));
                    expect (PluginPresets::saveUser (*synth, presetName, settings, error), error);
                    expect (PluginPresets::getUserFolder (synth->getPluginType()).isAChildOf (AppPaths::getRootFolder()),
                            "presets live under the app's folder (the test folder here)");

                    const auto user = PluginPresets::getUser (synth->getPluginType());
                    const auto found = std::find_if (user.begin(), user.end(), [&] (const auto& p) { return p.name == presetName; });
                    expect (found != user.end());

                    if (found != user.end())
                    {
                        PluginPresets::resetToDefaults (*synth, settings);
                        PluginPresets::apply (*synth, found->values, settings, true);
                        expectWithinAbsoluteError (level->getCurrentValue(), -12.0f, 0.01f);
                        expectEquals (readWave(), 3);

                        expect (PluginPresets::deleteUser (*found));
                        const auto after = PluginPresets::getUser (synth->getPluginType());
                        expect (std::none_of (after.begin(), after.end(), [&] (const auto& p) { return p.name == presetName; }));
                    }

                    beginTest ("A preset with a bad name is refused with a reason, and nothing is written");
                    const auto before = PluginPresets::getUser (synth->getPluginType()).size();
                    expect (! PluginPresets::saveUser (*synth, "   ", settings, error));
                    expect (error.isNotEmpty());
                    expectEquals (PluginPresets::getUser (synth->getPluginType()).size(), before);

                    beginTest ("The preset name shown in the window is kept with the plug-in");
                    PluginPresets::setCurrentName (*synth, "Warm pad");
                    expectEquals (PluginPresets::getCurrentName (*synth), juce::String ("Warm pad"));
                }
            }

            beginTest ("Knob arcs grow from the middle for pan, tune and gain, from the left for everything else");
            {
                juce::Slider pan, cutoff;
                pan.setRange (-1.0, 1.0);
                cutoff.setRange (20.0, 20000.0);
                expectWithinAbsoluteError (PluginLook::arcOrigin (pan), 0.5f, 0.001f);
                expectWithinAbsoluteError (PluginLook::arcOrigin (cutoff), 0.0f, 0.001f);
                cutoff.getProperties().set ("ezDefault", 10010.0);
                expectWithinAbsoluteError (PluginLook::arcOrigin (cutoff), 0.5f, 0.01f);
            }

            beginTest ("Our effects publish input and output peaks for the meters, and taking them clears them");
            {
                UtilityPlugin* utility = nullptr;

                for (auto& p : plugins)
                    if (auto* u = dynamic_cast<UtilityPlugin*> (p.get()))
                        utility = u;

                expect (utility != nullptr);

                if (utility != nullptr)
                {
                    utility->getControl (0)->setParameter (6.0f, juce::sendNotificationSync);   // +6 dB gain
                    utility->baseClassInitialise ({ te::TimePosition(), 44100.0, 512 });
                    utility->takeInputPeak();
                    utility->takeOutputPeak();
                    juce::AudioBuffer<float> buffer (2, 512);

                    for (int i = 0; i < 512; ++i)
                        for (int c = 0; c < 2; ++c)
                            buffer.setSample (c, i, 0.25f * (float) std::sin (i * 0.1));

                    utility->applyToBuffer (te::PluginRenderContext (&buffer, 0, 512, nullptr, 0.0, {}, true, false, false, false));
                    const auto in = utility->takeInputPeak(), out = utility->takeOutputPeak();
                    expectWithinAbsoluteError (in, 0.25f, 0.01f);
                    expect (out > in * 1.8f && out < in * 2.2f, "the output meter sees the +6 dB: " + juce::String (out));
                    expectEquals (utility->takeInputPeak(), 0.0f, "taken: the next block starts from silence");
                    utility->getControl (0)->setParameter (0.0f, juce::sendNotificationSync);
                }
            }

            beginTest ("The synth's window keeps one height on every page, so it never jumps when you change tabs");
            if (synth != nullptr)
            {
                auto editor = ParamEditor::createContent (*synth);
                const auto height = editor->getHeight();
                expect (height > 300);

                for (int page = 2; page <= 4; ++page)
                {
                    expect (editor->keyPressed (juce::KeyPress ('0' + page, juce::ModifierKeys::commandModifier, 0)), "Ctrl+" + juce::String (page) + " shows a page");
                    expectEquals (editor->getHeight(), height);
                }
            }

            plugins.clear();
            session.closeSong();
        }

        void runReleaseTests (Session& session, const juce::File& songFile, const juce::File& folder, te::Engine& teEngine)
        {
            beginTest ("Check for updates compares versions the way people read them");
            expect (Version::compare ("1.0.1", "1.0.0") > 0);
            expect (Version::compare ("v1.10.0", "1.9.9") > 0, "numbers, not text");
            expect (Version::compare ("1.0.0", "v1.0.0") == 0, "a leading v is ignored");
            expect (Version::compare ("1.0.0", "1.0.0-beta.1") > 0, "the release is newer than its beta");
            expect (Version::compare ("1.0.0-beta.2", "1.0.0-beta.1") > 0);
            expect (Version::compare ("1.0.0-beta.10", "1.0.0-beta.9") > 0, "beta numbers compare as numbers");
            expect (Version::compare ("1.0", "1.0.0") == 0, "a missing .0 doesn't matter");
            expect (Version::compare ("0.9.0", "1.0.0-beta.1") < 0);

            beginTest ("A run that didn't close cleanly is noticed (so its log can be sent)");
            {
                const auto write = [&folder] (const juce::String& fileName, const juce::String& text)
                {
                    const auto f = folder.getChildFile (fileName);
                    f.replaceWithText (text);
                    return f;
                };
                const auto header = juce::String ("**********\nEzDAWsIt 1.0.0 | Windows 11 | today\n");
                const auto clean = Diagnostics::summarise (write ("clean.txt", header + "work\n" + Diagnostics::cleanShutdownLine + "\n"), "EzDAWsIt");
                const auto crashed = Diagnostics::summarise (write ("crash.txt", header + "work\n" + Diagnostics::crashLine + "\nstack\n"), "EzDAWsIt");
                const auto killed = Diagnostics::summarise (write ("killed.txt", header + "work\nmore work\n"), "EzDAWsIt");
                const auto tests = Diagnostics::summarise (write ("tests.txt", "**********\nEzDAWsIt tests 1.0.0 | Windows | today\nrunning\n"), "EzDAWsIt");

                expect (clean.isAppRun && clean.endedCleanly && ! clean.crashed, "a clean run");
                expect (crashed.isAppRun && ! crashed.endedCleanly && crashed.crashed, "a crash");
                expect (killed.isAppRun && ! killed.endedCleanly && ! killed.crashed, "ended without a crash report (killed, power cut)");
                expect (! tests.isAppRun, "test runs never trigger the offer");
            }

            beginTest ("Plugins are scanned in a separate process, so a bad plugin can't crash EzDAWsIt");
            expect (AudioEngine::Behaviour().canScanPluginsOutOfProcess());

            beginTest ("Every save stamps the song format; a song from a newer EzDAWsIt is refused, untouched");
            ensureSongIsOpen (session, songFile);
            session.setBpm (101.0);
            expect (session.saveSong(), session.getLastError());
            expectEquals (Session::readSongFormatVersion (teEngine, songFile), Session::songFormatVersion);

            {
                auto state = te::loadEditFromFile (teEngine, songFile, te::ProjectItemID {});
                expect (state.hasType (te::IDs::EDIT));
                state.setProperty (Session::formatVersionProp, Session::songFormatVersion + 1, nullptr);
                state.setProperty (Session::savedWithProp, "EzDAWsIt 9.0.0", nullptr);
                const auto future = folder.getChildFile ("From the future.tracktionedit");
                expect (future.replaceWithText (state.toXmlString()));
                const auto before = future.loadFileAsString();

                expectEquals (Session::readSongFormatVersion (teEngine, future), Session::songFormatVersion + 1);
                expect (! session.openSong (future), "refused");
                expect (session.getLastError().contains ("newer EzDAWsIt") && session.getLastError().contains ("9.0.0"), session.getLastError());
                expect (future.loadFileAsString() == before, "the newer song is not touched");
                expect (session.getSongFile() == songFile, "the open song stays open");

                // A song from before the stamp (no version) still opens: it's the same format
                state.removeProperty (Session::formatVersionProp, nullptr);
                const auto older = folder.getChildFile ("From before the stamp.tracktionedit");
                expect (older.replaceWithText (state.toXmlString()));
                expectEquals (Session::readSongFormatVersion (teEngine, older), 0);
                expect (session.openSong (older), session.getLastError());
                expect (session.closeSong());
            }

            beginTest ("A big song (48 tracks, 96 effects, 3000 notes) saves and opens in reasonable time");
            {
                expect (session.createSong ("Stress Test"), session.getLastError());
                const auto file = session.getSongFile();

                juce::Array<Session::PluginChoice> effects;

                for (const auto& e : session.getAvailableEffects())
                    if (e.pluginType.isNotEmpty() && e.description.fileOrIdentifier.isEmpty() && effects.size() < 2)
                        effects.add (e);

                expectEquals (effects.size(), 2, "two built-in effects to use");

                while (session.getNumAudioTracks() < 48)
                    session.addAudioTrack();

                const auto& tempo = session.getEdit()->tempoSequence;
                int notes = 0;

                for (int t = 0; t < 48; ++t)
                {
                    for (const auto& e : effects)
                        session.addEffect (t, e, false);

                    if (session.createMidiClip (t, te::TimePosition(), tempo.toTime (te::BeatPosition::fromBeats (64.0)) - te::TimePosition(), "Part " + juce::String (t + 1)))
                        if (auto* clip = dynamic_cast<te::MidiClip*> (session.getSelectedClip()))
                            for (int n = 0; n < 64; ++n, ++notes)
                                session.addMidiNote (*clip, 36 + (n * 7 + t) % 48, n, 0.5, 100);
                }

                expectEquals (session.getNumAudioTracks(), 48);
                expectEquals (notes, 48 * 64);

                const auto timeIt = [] (auto&& action)
                {
                    const auto start = juce::Time::getMillisecondCounterHiRes();
                    action();
                    return juce::Time::getMillisecondCounterHiRes() - start;
                };

                bool ok = false;
                const auto saveMs = timeIt ([&] { ok = session.saveSong(); });
                expect (ok, session.getLastError());
                const auto closeMs = timeIt ([&] { ok = session.closeSong(); });
                expect (ok, session.getLastError());
                const auto openMs = timeIt ([&] { ok = session.openSong (file); });
                expect (ok, session.getLastError());
                logMessage ("Big song: save " + juce::String (saveMs, 0) + " ms, close " + juce::String (closeMs, 0) + " ms, open " + juce::String (openMs, 0) + " ms, "
                     + juce::String (file.getSize() / 1024) + " KB");

                expectEquals (session.getNumAudioTracks(), 48, "every track came back");
                int clips = 0;

                for (auto* track : te::getAudioTracks (*session.getEdit()))
                    clips += track->getClips().size();

                expectEquals (clips, 48, "every clip came back");
                expect (saveMs < 15000.0 && openMs < 20000.0 && closeMs < 15000.0,
                        "save " + juce::String (saveMs, 0) + " ms, open " + juce::String (openMs, 0) + " ms");

                expect (session.closeSong());
            }

            ensureSongIsOpen (session, songFile);
        }

        /** Each test block starts from the song being open, so one failure can't cascade into the next. */
        void ensureSongIsOpen (Session& session, const juce::File& songFile)
        {
            if (session.getSongFile() != songFile)
                expect (session.openSong (songFile), "setup: could not reopen the test song: " + session.getLastError());
        }

        /** Leaves the files a crash leaves behind: a saved song (savedBpm) + a newer autosave (unsavedBpm). */
        void simulateCrash (Session& session, const juce::File& songFile, double savedBpm, double unsavedBpm)
        {
            ensureSongIsOpen (session, songFile);
            session.setBpm (savedBpm);
            expect (session.saveSong(), session.getLastError());
            const auto savedCopy = songFile.getSiblingFile ("saved-copy");
            expect (songFile.copyFileTo (savedCopy));

            session.setBpm (unsavedBpm);
            session.autosaveNow();
            const auto autosave = Session::getAutosaveFile (songFile);
            expect (autosave.existsAsFile(), "autosave was not written");

            session.autosaveNow();
            const auto autosaveTime = autosave.getLastModificationTime();
            session.autosaveNow();
            expect (autosave.getLastModificationTime() == autosaveTime, "an unchanged song must not be autosaved again");
            const auto autosaveCopy = songFile.getSiblingFile ("autosave-copy");
            expect (autosave.copyFileTo (autosaveCopy));

            session.closeSong();   // a real crash skips this; put the crash files back afterwards

            expect (savedCopy.moveFileTo (songFile));
            expect (autosaveCopy.moveFileTo (autosave));
            expect (songFile.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::minutes (2)));

            if (autosave.existsAsFile())   // don't let a missing autosave be created as an empty file
                expect (autosave.setLastModificationTime (juce::Time::getCurrentTime()));
        }

        void runCrashRecoveryTests (Session& session, const juce::File& songFile)
        {
            const auto folder = songFile.getParentDirectory();

            beginTest ("After a crash, Restore brings back the unsaved work and keeps the old save");
            simulateCrash (session, songFile, 100.0, 133.0);
            expect (Session::hasNewerAutosave (songFile));
            const int backupsBefore = countFiles (folder, "*.bak");
            expect (session.openSong (songFile, true), session.getLastError());
            expectEquals (session.getBpm(), 133.0);
            expect (countFiles (folder, "*before restore*.bak") >= 1, "the previous save must be kept");
            expect (countFiles (folder, "*.bak") > backupsBefore);

            beginTest ("After a crash, 'Use last save' keeps the saved version, and the autosave is kept aside, not deleted");
            simulateCrash (session, songFile, 101.0, 144.0);
            const int autosaveBackupsBefore = countFiles (folder, "*(autosave *).bak");
            expect (session.openSong (songFile, false), session.getLastError());
            expectEquals (session.getBpm(), 101.0);
            expectEquals (countFiles (folder, "*(autosave *).bak"), autosaveBackupsBefore + 1);
            expect (! Session::hasNewerAutosave (songFile), "must not ask again next time");

            beginTest ("A damaged song with a good autosave: plain open fails WITHOUT losing the autosave, Restore recovers it");
            simulateCrash (session, songFile, 102.0, 155.0);
            expect (songFile.replaceWithText ("damaged by a crash"));
            expect (! session.openSong (songFile, false));
            expect (Session::getAutosaveFile (songFile).existsAsFile(), "the only good copy must survive a failed open");
            expect (session.openSong (songFile, true), session.getLastError());
            expectEquals (session.getBpm(), 155.0);

            beginTest ("An empty autosave (crash mid-write) is refused, so Restore can't blank the song");
            ensureSongIsOpen (session, songFile);
            session.setBpm (166.0);
            expect (session.saveSong(), session.getLastError());
            const auto songBefore = songFile.loadFileAsString();
            session.closeSong();
            const auto autosave = Session::getAutosaveFile (songFile);
            expect (autosave.replaceWithText ({}));
            expect (autosave.getSize() == 0);
            expect (! session.openSong (songFile, true));
            expect (songFile.loadFileAsString() == songBefore, "the saved song must be untouched");
            expect (session.openSong (songFile, false), session.getLastError());
            expectEquals (session.getBpm(), 166.0);
        }

        void runBadInputTests (Session& session, const juce::File& songFile, const juce::File& tempFolder)
        {
            beginTest ("A damaged song file is refused, never overwritten, and the open song stays open");
            ensureSongIsOpen (session, songFile);
            const auto damaged = AppPaths::getProjectsFolder().getChildFile ("Damaged.tracktionedit");
            expect (damaged.replaceWithText ("this is not a song"));
            expect (! session.openSong (damaged));
            expect (session.getLastError().isNotEmpty());
            expect (session.hasSong());
            expect (session.getSongFile() == songFile);
            expect (damaged.loadFileAsString() == "this is not a song");

            beginTest ("Opening a song that no longer exists fails with a message");
            expect (! session.openSong (tempFolder.getChildFile ("missing.tracktionedit")));
            expect (session.getLastError().isNotEmpty());

            beginTest ("Importing something that isn't audio fails with a message");
            ensureSongIsOpen (session, songFile);
            expect (! session.importAudioFile (damaged));
            expect (session.getLastError().isNotEmpty());
        }

        void runInstrumentTests (Session& session, const juce::File& songFile)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong())
                return;

            beginTest ("Built-in instruments are always offered first, even with no plugins scanned");
            const auto choices = session.getAvailableInstruments();
            expect (choices.size() >= 2);
            expectEquals (choices[0].pluginType, juce::String (te::FourOscPlugin::xmlTypeName));
            expectEquals (choices[1].pluginType, juce::String (te::SamplerPlugin::xmlTypeName));

            beginTest ("A broken instrument adds nothing to the song");
            const int tracksBefore = session.getNumAudioTracks();
            expect (! session.addInstrumentTrack ({ "Broken", "no-such-plugin-type", {} }, false));
            expect (session.getLastError().isNotEmpty());
            expectEquals (session.getNumAudioTracks(), tracksBefore);

            beginTest ("Adding the synth gives a new track with the synth on it");
            expect (session.addInstrumentTrack (choices[0], false), session.getLastError());
            expectEquals (session.getNumAudioTracks(), tracksBefore + 1);
            const int synthTrackIndex = session.getNumAudioTracks() - 1;
            expect (session.getPluginNamesOnTrack (synthTrackIndex).contains ("4OSC"));

            beginTest ("The synth actually makes sound when a note plays, and is silent otherwise");
            {
                // Scoped: a clip pointer must be released before its song closes (it holds a ref
                // to the clip, whose destructor touches the Edit)
                auto& edit = *session.getEdit();
                auto* synthTrack = te::getAudioTracks (edit)[synthTrackIndex];

                for (auto* t : te::getAudioTracks (edit))       // only the synth should be audible
                    if (t != synthTrack)
                        t->setMute (true);

                const te::TimeRange clipRange (te::TimePosition(), te::TimePosition::fromSeconds (2.0));
                auto clip = synthTrack->insertMIDIClip (clipRange, nullptr);
                expect (clip != nullptr);

                if (clip == nullptr)
                    return;

                expect (renderPeak (edit, 2.0) < 0.001f, "no notes yet: must be silent");
                auto* note = clip->getSequence().addNote (60, te::BeatPosition(), te::BeatDuration::fromBeats (2.0), 100, 0, nullptr);
                const auto peakWithNote = renderPeak (edit, 2.0);
                expect (peakWithNote > 0.01f, "a note on the synth must be audible (peak " + juce::String (peakWithNote) + ")");
                clip->getSequence().removeNote (*note, nullptr);
                expect (renderPeak (edit, 2.0) < 0.001f, "removing the note must make it silent again");
            }

            beginTest ("Instrument tracks survive saving and reopening");
            expect (session.saveSong(), session.getLastError());
            expect (session.closeSong());
            expect (session.openSong (songFile), session.getLastError());
            expect (session.getPluginNamesOnTrack (synthTrackIndex).contains ("4OSC"));

            beginTest ("A new instrument track gets the MIDI input, armed, so the keyboard plays it");
            {
                const auto& midiName = testMidiInputName;
                expect (ensureTestMidiInput (session.getEdit()->engine.getDeviceManager()),
                        "setup: virtual MIDI input was not created");

                expect (session.addInstrumentTrack (choices[0], false), session.getLastError());
                pumpMessages (100);
                auto* newTrack = te::getAudioTracks (*session.getEdit()).getLast();
                bool routedAndArmed = false;

                for (auto* input : session.getEdit()->getAllInputDevices())
                    if (input->getInputDevice().getName() == midiName)
                        routedAndArmed = input->getTargets().contains (newTrack->itemID)
                                          && input->isRecordingEnabled (newTrack->itemID);

                expect (routedAndArmed, "the MIDI input must target and arm the newest instrument track");
            }

            beginTest ("The sampler loads too");
            expect (session.addInstrumentTrack (choices[1], false), session.getLastError());
            expect (session.getPluginNamesOnTrack (session.getNumAudioTracks() - 1).contains ("Sampler"));
        }

        void runRoutingTests (Session& session, const juce::File& songFile, AudioEngine& engine)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong() || session.getNumAudioTracks() == 0)
                return;

            beginTest ("Every track can always go to the main mix, and does by default");
            const auto outputs = session.getTrackOutputChoices();
            expect (! outputs.isEmpty() && outputs[0].id == Session::mainMixID);
            expectEquals (session.getTrackOutput (0), juce::String (Session::mainMixID));

            beginTest ("Switching the main output to a device that doesn't exist is refused, with a reason");
            {
                const auto mainBefore = engine.getMainOutputID();
                expect (! session.setMainOutput ("no-such-device"), "an unknown output is refused");
                expect (session.getLastError().isNotEmpty(), "and says why");
                expectEquals (engine.getMainOutputID(), mainBefore, "the main output is unchanged");
            }

            beginTest ("An output that doesn't exist is refused and nothing changes");
            expect (! session.setTrackOutput (0, "no-such-output"));
            expect (session.getLastError().isNotEmpty());
            expectEquals (session.getTrackOutput (0), juce::String (Session::mainMixID));
            expect (session.setTrackOutput (0, Session::mainMixID), session.getLastError());

            beginTest ("A track's input can be chosen, cleared, and survives save + reopen");
            juce::String midiName;   // device ID of the virtual input created in runInstrumentTests

            for (auto& choice : session.getTrackInputChoices())
                if (choice.name.contains (testMidiInputName))
                    midiName = choice.id;

            expect (midiName.isNotEmpty(), "the virtual MIDI input must be offered as a track input");
            expect (session.setTrackInput (0, midiName), session.getLastError());
            expectEquals (session.getTrackInput (0), midiName);
            expect (session.saveSong(), session.getLastError());
            expect (session.closeSong());
            expect (session.openSong (songFile), session.getLastError());
            expectEquals (session.getTrackInput (0), midiName);
            expect (session.setTrackInput (0, {}), session.getLastError());
            expectEquals (session.getTrackInput (0), juce::String());

            beginTest ("An input that doesn't exist is refused and the track keeps its input");
            expect (session.setTrackInput (0, midiName));
            expect (! session.setTrackInput (0, "no-such-input"));
            expectEquals (session.getTrackInput (0), midiName);

            beginTest ("Changing a track's input can be undone in one step");
            expect (session.setTrackInput (0, {}), session.getLastError());
            expectEquals (session.getTrackInput (0), juce::String());
            session.undo();
            expectEquals (session.getTrackInput (0), midiName);

            // Not testable headless (no audio device): direct track outputs, main output switching and
            // test-tone channels. These are checked by hand on real hardware (see docs/LESSONS.md).
            juce::ignoreUnused (engine);
        }

        void runTimelineTests (Session& session, const juce::File& songFile)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong() || session.getNumAudioTracks() < 2)
                return;

            auto& edit = *session.getEdit();
            session.setBpm (120.0);   // 1 beat = 0.5 s
            const auto at = [] (double seconds) { return te::TimePosition::fromSeconds (seconds); };

            beginTest ("Snap rounds to the nearest grid line, following the tempo");
            expectWithinAbsoluteError (session.snapToGrid (at (0.74), 1.0).inSeconds(), 0.5, 1.0e-6);
            expectWithinAbsoluteError (session.snapToGrid (at (0.76), 1.0).inSeconds(), 1.0, 1.0e-6);
            expectWithinAbsoluteError (session.snapToGrid (at (2.9), 4.0).inSeconds(), 2.0, 1.0e-6);    // 5.8 beats -> bar at 4 beats
            expectWithinAbsoluteError (session.snapToGrid (at (3.1), 4.0).inSeconds(), 4.0, 1.0e-6);    // 6.2 beats -> bar at 8 beats
            expectWithinAbsoluteError (session.snapToGrid (at (0.1), 4.0).inSeconds(), 0.0, 1.0e-6);    // never negative

            beginTest ("A clip can be moved in time and to another track, and one Undo puts it back");
            auto* firstTrack = te::getAudioTracks (edit)[0];
            te::EditItemID clipID;

            {   // don't hold the clip past closeSong() below
                auto clip = firstTrack->insertMIDIClip ({ at (0.0), at (1.0) }, nullptr);
                expect (clip != nullptr);

                if (clip == nullptr)
                    return;

                clipID = clip->itemID;
                expect (session.moveClip (*clip, at (2.0), 1), session.getLastError());
            }

            const auto findClip = [&edit, clipID] { return te::findClipForID (edit, clipID); };
            expect (findClip() != nullptr && findClip()->getClipTrack() == te::getAudioTracks (edit)[1]);
            expectWithinAbsoluteError (findClip()->getPosition().getStart().inSeconds(), 2.0, 1.0e-6);

            session.undo();
            expect (findClip() != nullptr && findClip()->getClipTrack() == firstTrack, "undo must return the clip to its track");
            expectWithinAbsoluteError (findClip()->getPosition().getStart().inSeconds(), 0.0, 1.0e-6);

            beginTest ("A clip can't be moved before the start of the song or onto a missing track");
            expect (! session.moveClip (*findClip(), at (-1.0), 0));
            expect (! session.moveClip (*findClip(), at (1.0), 999));
            expectWithinAbsoluteError (findClip()->getPosition().getStart().inSeconds(), 0.0, 1.0e-6);

            beginTest ("Delete removes the selected clip, and Undo brings it back");
            session.selectClip (findClip());
            expect (session.getSelectedClip() == findClip());
            expect (session.deleteSelectedClip(), session.getLastError());
            expect (findClip() == nullptr);
            expect (session.getSelectedClip() == nullptr);
            expect (! session.deleteSelectedClip(), "nothing selected: must fail, not crash");
            session.undo();
            expect (findClip() != nullptr);

            beginTest ("The loop region is stored in order, and looping can be switched on and off");
            session.setLoopRange (at (4.0), at (2.0));
            expectWithinAbsoluteError (session.getLoopRange().getStart().inSeconds(), 2.0, 1.0e-6);
            expectWithinAbsoluteError (session.getLoopRange().getEnd().inSeconds(), 4.0, 1.0e-6);
            session.setLooping (true);
            expect (session.isLooping());
            session.setLooping (false);
            expect (! session.isLooping());

            beginTest ("Track volume is limited to a safe range, pan to left..right, and they're saved");
            session.setTrackVolumeDb (0, 100.0f);
            expectWithinAbsoluteError (session.getTrackVolumeDb (0), Session::maxVolumeDb, 0.01f);
            session.setTrackVolumeDb (0, -6.0f);
            session.setTrackPan (0, -5.0f);
            expectWithinAbsoluteError (session.getTrackPan (0), -1.0f, 0.001f);
            session.setTrackPan (0, 0.25f);
            session.setTrackMuted (1, true);
            expect (session.saveSong() && session.closeSong() && session.openSong (songFile), session.getLastError());
            expectWithinAbsoluteError (session.getTrackVolumeDb (0), -6.0f, 0.05f);
            expectWithinAbsoluteError (session.getTrackPan (0), 0.25f, 0.01f);
            expect (session.isTrackMuted (1));
            session.setTrackMuted (1, false);

            beginTest ("Undo right after muting un-mutes (and doesn't undo an earlier edit instead)");
            {
                // Start from a known state: an earlier test mutes every track but the synth
                session.setTrackMuted (0, false);
                session.setTrackSoloed (0, false);
                expect (! session.isTrackMuted (0) && ! session.isTrackSoloed (0), "precondition: track starts un-muted");

                auto* track = te::getAudioTracks (*session.getEdit())[0];
                session.beginUndoStep();   // the marker clip is its own step, separate from the mute
                auto marker = track->insertMIDIClip ({ at (8.0), at (9.0) }, nullptr);
                const auto markerID = marker->itemID;
                session.setTrackMuted (0, true);
                expect (session.isTrackMuted (0));
                session.undo();
                expect (! session.isTrackMuted (0));
                expect (te::findClipForID (*session.getEdit(), markerID) != nullptr, "undoing the mute must not remove the clip");

                session.setTrackSoloed (0, true);
                session.undo();
                expect (! session.isTrackSoloed (0), "solo is undoable too");
            }

            beginTest ("A deleted clip can't be moved back into the song by a stale view");
            {
                auto* track = te::getAudioTracks (*session.getEdit())[0];
                te::Clip::Ptr doomed = track->insertMIDIClip ({ at (12.0), at (13.0) }, nullptr).get();
                session.selectClip (doomed.get());
                expect (session.deleteSelectedClip());
                expect (! session.moveClip (*doomed, at (1.0), 0));
                expect (te::findClipForID (*session.getEdit(), doomed->itemID) == nullptr);
            }

            beginTest ("Arming a track with no input explains what to do instead of silently failing");
            expect (session.setTrackInput (1, {}), session.getLastError());
            expect (! session.setTrackArmed (1, true));
            expect (session.getLastError().contains ("input"));

            beginTest ("Effects go on the track before its volume fader, and can be removed");
            const auto effects = session.getAvailableEffects();
            expect (effects.size() >= 7 && effects[2].name == "Reverb");
            expect (session.addEffect (0, effects[2], false), session.getLastError());
            expect (session.getEffectNames (0).contains ("Reverb"));

            auto& plugins = te::getAudioTracks (*session.getEdit())[0]->pluginList;
            int reverbIndex = -1, volumeIndex = -1;

            for (int i = 0; i < plugins.size(); ++i)
            {
                if (dynamic_cast<te::ReverbPlugin*> (plugins[i]) != nullptr)        reverbIndex = i;
                if (dynamic_cast<te::VolumeAndPanPlugin*> (plugins[i]) != nullptr)  volumeIndex = i;
            }

            expect (reverbIndex >= 0 && (volumeIndex < 0 || reverbIndex < volumeIndex), "the effect must come before the fader");
            expect (! session.addEffect (0, { "Broken", "no-such-effect", {} }, false));
            const auto before = session.getEffectNames (0).size();
            expect (session.removeEffect (0, before - 1), session.getLastError());
            expectEquals (session.getEffectNames (0).size(), before - 1);
            expect (! session.removeEffect (0, 99));

            beginTest ("The devices panel can bypass and reorder effects, each one Ctrl+Z");
            {
                // A known chain on a fresh track: EQ, Compressor, Delay (counts come from here, not earlier tests)
                session.addAudioTrack();
                const auto t = session.getNumAudioTracks() - 1;

                for (int i : { 0, 1, 3 })
                    expect (session.addEffect (t, effects[i], false), session.getLastError());

                const auto start = session.getEffectNames (t);   // whatever Tracktion calls them
                const auto orderIs = [&] (std::initializer_list<int> order)
                {
                    juce::StringArray expected;

                    for (auto i : order)
                        expected.add (start[i]);

                    return session.getEffectNames (t) == expected;
                };

                expectEquals (start.size(), 3);
                auto info = session.getEffectInfo (t);
                expect (info.size() == 3 && info[0].isBuiltIn && info[0].isEnabled && info[0].maker == "Built-in");
                expect (info[0].name == start[0], "info and names agree");
                expect (! session.getInstrumentInfo (t).has_value(), "an audio track has no instrument");

                expect (session.setEffectEnabled (t, 1, false), session.getLastError());
                expect (! session.getEffectInfo (t)[1].isEnabled, "the middle effect is bypassed");
                session.undo();
                expect (session.getEffectInfo (t)[1].isEnabled, "undo switches it back on");

                // Tracktion doesn't rebuild playback for a reorder by itself: without it you'd hear the old order
                const auto rebuilds = session.getPlaybackRebuildsForReorder();
                expect (session.moveEffect (t, 2, 0), session.getLastError());
                expect (orderIs ({ 2, 0, 1 }), "the last effect is now first");
                expect (session.getPlaybackRebuildsForReorder() > rebuilds, "moving an effect rebuilds playback, so you hear it");
                const auto rebuildsAfterMove = session.getPlaybackRebuildsForReorder();
                session.undo();
                expect (orderIs ({ 0, 1, 2 }), "undo puts it back");
                expect (session.getPlaybackRebuildsForReorder() > rebuildsAfterMove, "...and undo rebuilds playback too");
                expect (session.moveEffect (t, 0, 2));
                expect (orderIs ({ 1, 2, 0 }), "the first effect moved to the end");

                // The volume fader must still come after every effect
                auto& chain = te::getAudioTracks (*session.getEdit())[t]->pluginList;
                int lastEffect = -1, volume = -1;

                for (int i = 0; i < chain.size(); ++i)
                {
                    if (dynamic_cast<te::VolumeAndPanPlugin*> (chain[i]) != nullptr)  volume = i;
                    else if (dynamic_cast<te::LevelMeterPlugin*> (chain[i]) == nullptr) lastEffect = i;
                }

                expect (volume >= 0 && lastEffect < volume, "reordering never moves an effect past the fader");
                expect (! session.moveEffect (t, 0, 7) && ! session.setEffectEnabled (t, 9, false), "bad indexes are refused");

                expect (session.deleteTrack (t));
            }
        }

        /** The piano roll's note commands. */
        void runNoteTests (Session& session, const juce::File& songFile)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong() || session.getNumAudioTracks() < 1)
                return;

            session.setBpm (120.0);
            beginTest ("Notes can be added, and bad ones are refused");
            // Starts half a beat off the bar, so the clip's beat 0 isn't on the song's grid
            expect (session.createMidiClip (0, te::TimePosition::fromSeconds (300.25), te::TimeDuration::fromSeconds (4.0)), session.getLastError());
            auto* clip = dynamic_cast<te::MidiClip*> (session.getSelectedClip());
            expect (clip != nullptr);

            if (clip == nullptr)
                return;

            const auto count = [clip] { return clip->getSequence().getNotes().size(); };
            auto* a = session.addMidiNote (*clip, 60, 0.0, 1.0, 100);
            auto* b = session.addMidiNote (*clip, 64, 1.1, 0.5, 300);
            expect (a != nullptr && b != nullptr, session.getLastError());
            expectEquals (count(), 2);
            expectEquals (b->getVelocity(), 127, "velocity is kept in range");
            expect (session.addMidiNote (*clip, 128, 0.0, 1.0, 100) == nullptr, "too high");
            expect (session.addMidiNote (*clip, 60, -1.0, 1.0, 100) == nullptr, "before the content");
            expectEquals (count(), 2, "refused notes add nothing");
            session.undo();
            expectEquals (count(), 1, "one undo removes one added note");
            session.redo();
            b = clip->getSequence().getNotes().getLast();   // redo makes a new object

            beginTest ("Moving notes: together, undoable, all-or-nothing");
            expect (session.moveMidiNotes (*clip, { a, b }, 0.5, 2), session.getLastError());
            expectEquals (a->getNoteNumber(), 62);
            expectWithinAbsoluteError (a->getStartBeat().inBeats(), 0.5, 1.0e-9);
            expectWithinAbsoluteError (b->getStartBeat().inBeats(), 1.6, 1.0e-9);
            session.undo();
            expectEquals (a->getNoteNumber(), 60);
            expectWithinAbsoluteError (b->getStartBeat().inBeats(), 1.1, 1.0e-9);
            expect (! session.moveMidiNotes (*clip, { a, b }, -0.5, 0), "one note would start before 0: nothing moves");
            expectWithinAbsoluteError (a->getStartBeat().inBeats(), 0.0, 1.0e-9);
            expectWithinAbsoluteError (b->getStartBeat().inBeats(), 1.1, 1.0e-9);
            expect (! session.moveMidiNotes (*clip, { a, b }, 0.0, 70), "off the top of the keyboard");
            expectEquals (a->getNoteNumber(), 60);
            expectEquals (b->getNoteNumber(), 64);
            expectEquals (count(), 2, "a refused move adds or loses nothing");

            beginTest ("Resizing never makes a note vanish");
            expect (session.resizeMidiNotes (*clip, { a }, 1.0));
            expectWithinAbsoluteError (a->getLengthBeats().inBeats(), 2.0, 1.0e-9);
            expect (session.resizeMidiNotes (*clip, { a }, -50.0));
            expectWithinAbsoluteError (a->getLengthBeats().inBeats(), Session::minNoteBeats, 1.0e-9);
            session.undo();
            session.undo();
            expectWithinAbsoluteError (a->getLengthBeats().inBeats(), 1.0, 1.0e-9);

            beginTest ("A velocity drag is one undo step, and starts its own");
            expect (session.resizeMidiNotes (*clip, { b }, 0.25));   // the action before the drag
            expect (session.setMidiNoteVelocity (*clip, { a }, 40));
            expect (session.setMidiNoteVelocity (*clip, { a }, 0, true));
            expectEquals (a->getVelocity(), 1);
            session.undo();
            expectEquals (a->getVelocity(), 100);
            expectWithinAbsoluteError (b->getLengthBeats().inBeats(), 0.75, 1.0e-9, "the earlier resize is its own step");
            session.undo();
            expectWithinAbsoluteError (b->getLengthBeats().inBeats(), 0.5, 1.0e-9);

            beginTest ("Resizing several notes: each stops at the shortest length");
            expect (session.resizeMidiNotes (*clip, { a, b }, -0.75));
            expectWithinAbsoluteError (a->getLengthBeats().inBeats(), 0.25, 1.0e-9);
            expectWithinAbsoluteError (b->getLengthBeats().inBeats(), Session::minNoteBeats, 1.0e-9);
            session.undo();
            expectWithinAbsoluteError (a->getLengthBeats().inBeats(), 1.0, 1.0e-9);
            expectWithinAbsoluteError (b->getLengthBeats().inBeats(), 0.5, 1.0e-9);

            beginTest ("Quantise snaps to the song's grid, not the clip's edge");
            // The clip starts at song beat 600.5; note b at content 1.1 = song 601.6 -> 601.5 = content 1.0
            const auto contentStart = session.getContentStartInSongBeats (*clip);
            expectWithinAbsoluteError (contentStart, 600.5, 1.0e-6);
            expect (session.quantiseMidiNotes (*clip, { b }, 0.5), session.getLastError());
            expectWithinAbsoluteError (b->getStartBeat().inBeats(), 1.0, 1.0e-6, "a listed note moves");
            session.undo();
            expectWithinAbsoluteError (b->getStartBeat().inBeats(), 1.1, 1.0e-9);
            expect (session.quantiseMidiNotes (*clip, {}, 0.5), session.getLastError());
            expectWithinAbsoluteError (b->getStartBeat().inBeats(), 1.0, 1.0e-6);
            expectWithinAbsoluteError (a->getStartBeat().inBeats(), 0.0, 1.0e-6, "a note already on the grid stays (song beat 600.5)");
            session.undo();
            expectWithinAbsoluteError (b->getStartBeat().inBeats(), 1.1, 1.0e-9, "one undo restores every note");
            expect (! session.quantiseMidiNotes (*clip, {}, 0.0));

            beginTest ("Notes from elsewhere are ignored; deleting is undoable");
            te::MidiNote* stranger = nullptr;
            expect (session.createMidiClip (0, te::TimePosition::fromSeconds (310.0), te::TimeDuration::fromSeconds (2.0)));

            if (auto* other = dynamic_cast<te::MidiClip*> (session.getSelectedClip()))
                stranger = session.addMidiNote (*other, 50, 0.0, 1.0, 90);

            expect (stranger != nullptr);
            auto* otherClip = dynamic_cast<te::MidiClip*> (session.getSelectedClip());
            expect (! session.deleteMidiNotes (*clip, { stranger }), "a note from another clip isn't touched");
            expect (otherClip != nullptr && otherClip->getSequence().getNotes().contains (stranger), "and it still exists");
            expect (! session.moveMidiNotes (*clip, { stranger }, 1.0, 0));
            expect (session.deleteMidiNotes (*clip, { a, b, stranger }));
            expectEquals (count(), 0);
            session.undo();
            expectEquals (count(), 2);

            beginTest ("Notes survive saving and reopening");
            const auto clipID = clip->itemID;
            expect (session.saveSong() && session.closeSong() && session.openSong (songFile), session.getLastError());
            auto* reopened = dynamic_cast<te::MidiClip*> (te::findClipForID (*session.getEdit(), clipID));
            expect (reopened != nullptr && reopened->getSequence().getNotes().size() == 2);

            beginTest ("Quantise uses where the content starts, even after the clip's start is trimmed");
            // Content starts at song beat 640; the clip is trimmed to start at 640.25
            expect (session.createMidiClip (0, te::TimePosition::fromSeconds (320.0), te::TimeDuration::fromSeconds (4.0)), session.getLastError());
            te::MidiClip::Ptr trimmed = dynamic_cast<te::MidiClip*> (session.getSelectedClip());
            expect (trimmed != nullptr);

            if (trimmed == nullptr)
                return;

            auto* late = session.addMidiNote (*trimmed, 62, 1.6, 0.25, 100);
            expect (late != nullptr);
            expect (session.trimClipStart (*trimmed, te::TimePosition::fromSeconds (320.125)), session.getLastError());
            expectWithinAbsoluteError (session.getContentStartInSongBeats (*trimmed), 640.0, 1.0e-6);
            expect (session.quantiseMidiNotes (*trimmed, {}, 0.5));
            expectWithinAbsoluteError (late->getStartBeat().inBeats(), 1.5, 1.0e-6, "song beat 641.6 snaps to 641.5");

            beginTest ("Quantise strength and swing");
            // Content starts on song beat 640, so content beats and grid lines agree on odd/even
            auto* soft = session.addMidiNote (*trimmed, 64, 2.1, 0.25, 90);
            expect (soft != nullptr);

            if (soft != nullptr)
            {
                expect (session.quantiseMidiNotes (*trimmed, { soft }, 0.5, 0.5), session.getLastError());
                expectWithinAbsoluteError (soft->getStartBeat().inBeats(), 2.05, 1.0e-6, "half strength moves half way to the line");
                expect (session.quantiseMidiNotes (*trimmed, { soft }, 0.5, 1.0, 0.0), session.getLastError());
                expectWithinAbsoluteError (soft->getStartBeat().inBeats(), 2.0, 1.0e-6);
                expect (session.moveMidiNotes (*trimmed, { soft }, 0.5, 0));   // now on an odd line (2.5)
                expect (session.quantiseMidiNotes (*trimmed, { soft }, 0.5, 1.0, 0.5), session.getLastError());
                expectWithinAbsoluteError (soft->getStartBeat().inBeats(), 2.625, 1.0e-6, "swing delays every second line by swing x half a grid step");
            }

            beginTest ("Resizing from the start, setting lengths, joining notes up");
            auto notesNow = [&trimmed] { return trimmed->getSequence().getNotes().size(); };
            expect (session.resizeMidiNotesFromStart (*trimmed, { late }, -0.5), session.getLastError());
            expectWithinAbsoluteError (late->getStartBeat().inBeats(), 1.0, 1.0e-6);
            expectWithinAbsoluteError (late->getLengthBeats().inBeats(), 0.75, 1.0e-6, "the end stays where it was");
            expect (session.resizeMidiNotesFromStart (*trimmed, { late }, 50.0), "a huge drag is clamped, not refused");
            expectWithinAbsoluteError (late->getLengthBeats().inBeats(), Session::minNoteBeats, 1.0e-6, "it keeps the shortest length");
            session.undo();
            session.undo();
            expectWithinAbsoluteError (late->getStartBeat().inBeats(), 1.5, 1.0e-6);
            expectWithinAbsoluteError (late->getLengthBeats().inBeats(), 0.25, 1.0e-6);
            expect (session.resizeMidiNotesFromStart (*trimmed, { late }, 0.0), "no change is fine");

            expect (session.setMidiNoteLength (*trimmed, { late }, 0.5));
            expect (session.setMidiNoteLength (*trimmed, { late }, 2.0, true));
            expectWithinAbsoluteError (late->getLengthBeats().inBeats(), 2.0, 1.0e-6);
            session.undo();
            expectWithinAbsoluteError (late->getLengthBeats().inBeats(), 0.25, 1.0e-6, "a typed or dragged length is one undo step");

            auto* first = session.addMidiNote (*trimmed, 60, 0.25, 0.25, 100);
            expect (first != nullptr);

            if (first != nullptr)
            {
                expect (session.legatoMidiNotes (*trimmed, { first, late }), session.getLastError());
                expectWithinAbsoluteError (first->getLengthBeats().inBeats(), 1.25, 1.0e-6, "runs up to the next note");
                expect (session.legatoMidiNotes (*trimmed, { first, late }), "already joined up");
                session.undo();   // the first call was the only step
                expectWithinAbsoluteError (first->getLengthBeats().inBeats(), 0.25, 1.0e-6);
                expect (session.deleteMidiNotes (*trimmed, { first }));
            }

            beginTest ("Copy, cut, paste, duplicate and copy-move");
            auto* partner = session.addMidiNote (*trimmed, 67, 3.0, 0.5, 80);
            expect (partner != nullptr);

            if (partner != nullptr)
            {
                const auto before = notesNow();
                expect (session.copyMidiNotes (*trimmed, { late, partner }), session.getLastError());
                expect (session.hasNoteClipboard());
                expectEquals (notesNow(), before, "copying changes nothing");

                const auto pasted = session.pasteMidiNotes (*trimmed, 8.0);
                expectEquals (pasted.size(), 2);
                expectEquals (notesNow(), before + 2);

                if (pasted.size() == 2)
                {
                    expectWithinAbsoluteError (pasted[0]->getStartBeat().inBeats(), 8.0, 1.0e-6, "the first copied note lands where you paste");
                    expectWithinAbsoluteError (pasted[1]->getStartBeat().inBeats(), 9.5, 1.0e-6, "and the gap between them is kept");
                }

                session.undo();
                expectEquals (notesNow(), before, "one undo removes the whole paste");

                const auto dup = session.duplicateMidiNotes (*trimmed, { late, partner });
                expectEquals (dup.size(), 2);

                if (dup.size() == 2)
                    expectWithinAbsoluteError (dup[0]->getStartBeat().inBeats(), 1.5 + 2.0, 1.0e-6, "straight after the selection's own span");

                session.undo();
                expectEquals (notesNow(), before);

                const auto moved = session.copyMoveMidiNotes (*trimmed, { late }, 1.0, 2);
                expectEquals (moved.size(), 1);

                if (moved.size() == 1)
                {
                    expectEquals (moved.getFirst()->getNoteNumber(), 64);
                    expectWithinAbsoluteError (moved.getFirst()->getStartBeat().inBeats(), 2.5, 1.0e-6);
                }

                expectEquals (notesNow(), before + 1, "the original stays");
                session.undo();
                expect (session.copyMoveMidiNotes (*trimmed, { late }, -10.0, 0).isEmpty(), "a copy that would start before 0 is refused");
                expect (session.copyMoveMidiNotes (*trimmed, { late }, 0.0, 100).isEmpty(), "and one off the keyboard");
                expectEquals (notesNow(), before);

                expect (session.cutMidiNotes (*trimmed, { late }), session.getLastError());
                expectEquals (notesNow(), before - 1);
                const auto back = session.pasteMidiNotes (*trimmed, 1.5);
                expectEquals (back.size(), 1, "a cut note can be pasted back");
                expectEquals (notesNow(), before);
            }

            late = nullptr;   // it was cut and pasted back as a new note

            beginTest ("A clip can loop its content");
            expect (! session.isClipLooping (*trimmed));
            expect (session.setClipLooping (*trimmed, true), session.getLastError());
            expect (session.isClipLooping (*trimmed));
            expect (session.setClipLooping (*trimmed, true), "already looping is fine");
            expect (session.setClipLooping (*trimmed, false));
            expect (! session.isClipLooping (*trimmed));
            session.undo();
            expect (session.isClipLooping (*trimmed), "undo puts looping back");
            session.undo();
            expect (! session.isClipLooping (*trimmed));

            beginTest ("Every note command refuses a deleted clip");
            auto* leftover = trimmed->getSequence().getNotes().getFirst();
            session.selectClip (trimmed.get());
            expect (session.deleteSelectedClips(), session.getLastError());
            expect (trimmed->getClipTrack() == nullptr);
            expect (session.addMidiNote (*trimmed, 60, 0.0, 1.0, 100) == nullptr);
            expect (! session.deleteMidiNotes (*trimmed, { leftover }));
            expect (! session.moveMidiNotes (*trimmed, { leftover }, 1.0, 0));
            expect (! session.resizeMidiNotes (*trimmed, { leftover }, 1.0));
            expect (! session.setMidiNoteVelocity (*trimmed, { leftover }, 50));
            expect (! session.quantiseMidiNotes (*trimmed, {}, 0.5));
            expect (! session.legatoMidiNotes (*trimmed, { leftover }));
            expect (! session.resizeMidiNotesFromStart (*trimmed, { leftover }, 0.5));
            expect (! session.setMidiNoteLength (*trimmed, { leftover }, 1.0));
            expect (! session.cutMidiNotes (*trimmed, { leftover }));
            expect (session.pasteMidiNotes (*trimmed, 0.0).isEmpty());
            expect (session.duplicateMidiNotes (*trimmed, { leftover }).isEmpty());
            expect (session.copyMoveMidiNotes (*trimmed, { leftover }, 1.0, 0).isEmpty());
            expect (! session.setClipLooping (*trimmed, true));
            trimmed = nullptr;   // release the clip while its song is still open
        }

        /** Renders one block of the drum machine directly (the test engine has no audio thread). */
        float renderDrums (DrumMachinePlugin& drums, std::initializer_list<std::pair<int, int>> notes, bool allOff = false)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();
            te::MidiMessageArray midi;
            midi.isAllNotesOff = allOff;

            for (auto [note, velocity] : notes)
                midi.addMidiMessage (juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity), 0.0, te::createUniqueMPESourceID());

            drums.applyToBuffer (te::PluginRenderContext (&buffer, 0, buffer.getNumSamples(), &midi, 0.0, {}, true, false, false, false));
            return buffer.getMagnitude (0, buffer.getNumSamples());
        }

        void runDrumTests (Session& session, const juce::File& songFile, const juce::File& tempFolder)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong())
                return;

            session.setBpm (120.0);

            beginTest ("The Drum Machine is a built-in instrument, and a kit makes all its sounds");
            bool offered = false;

            for (auto& choice : session.getAvailableInstruments())
                offered = offered || choice.pluginType == DrumMachinePlugin::xmlTypeName;

            expect (offered);
            const auto tracksBefore = session.getNumAudioTracks();
            expect (! session.addDrumMachineTrack (99), "a kit that doesn't exist is refused");
            expectEquals (session.getNumAudioTracks(), tracksBefore, "and adds nothing");
            expect (session.addDrumMachineTrack (1), session.getLastError());
            const auto trackIndex = session.getNumAudioTracks() - 1;
            expectEquals (trackIndex, tracksBefore);
            expectEquals (session.getTrackName (trackIndex), juce::String (Session::drumTrackName));
            auto* drums = session.getDrumMachine (trackIndex);
            expect (drums != nullptr);

            if (drums == nullptr)
                return;

            expectEquals (drums->getKitName(), juce::String ("Trap 808"));
            drums->flushPendingUpdates();

            for (int pad = 0; pad < DrumMachinePlugin::numPads; ++pad)
            {
                expect (drums->getPadAudio (pad) != nullptr, "pad " + juce::String (pad + 1) + ": " + drums->getPadLoadError (pad));
                expectEquals (drums->getPad (pad).name, DrumKits::getPadName (pad));
            }

            expectEquals (drums->getPad (4).choke, 1, "the hats choke each other");
            expectEquals (drums->getPad (5).choke, 1);

            beginTest ("Pad values: clamped, undoable, a knob drag is one step");
            expect (session.setDrumPadValue (*drums, 1, DrumMachinePlugin::Value::tune, 99.0));
            expectWithinAbsoluteError (drums->getPad (1).tune, DrumMachinePlugin::maxTune, 1.0e-9);
            expect (session.setDrumPadValue (*drums, 1, DrumMachinePlugin::Value::level, -3.0));
            expect (session.setDrumPadValue (*drums, 1, DrumMachinePlugin::Value::level, -9.0, true));
            expect (session.setDrumPadValue (*drums, 1, DrumMachinePlugin::Value::level, -9.0), "no change is fine");
            session.undo();
            expectWithinAbsoluteError (drums->getPad (1).level, 0.0, 1.0e-9, "the whole drag undoes at once");
            expectWithinAbsoluteError (drums->getPad (1).tune, DrumMachinePlugin::maxTune, 1.0e-9, "the step before is separate");
            session.undo();
            expectWithinAbsoluteError (drums->getPad (1).tune, 0.0, 1.0e-9);
            expect (! session.setDrumPadValue (*drums, 16, DrumMachinePlugin::Value::tune, 1.0), "no pad 17");
            expect (! session.setDrumPadMuted (*drums, -1, true));

            expect (session.setDrumPadChoke (*drums, 2, 9));
            expectEquals (drums->getPad (2).choke, DrumMachinePlugin::maxChokeGroup);
            expect (session.setDrumPadMuted (*drums, 2, true) && session.setDrumPadSoloed (*drums, 3, true) && session.setDrumPadReversed (*drums, 3, true));
            expect (drums->getPad (2).muted && drums->getPad (3).soloed && drums->getPad (3).reversed);
            expect (session.renameDrumPad (*drums, 3, "  Rimshot  "));
            expectEquals (drums->getPad (3).name, juce::String ("Rimshot"));
            expect (! session.renameDrumPad (*drums, 3, "   "), "an empty name is refused");

            beginTest ("Loading a kit resets the sounds but keeps mute and solo; it undoes in one step");
            expect (session.setDrumPadValue (*drums, 0, DrumMachinePlugin::Value::decay, 0.5));
            expect (session.loadDrumKit (*drums, 5), session.getLastError());
            expectEquals (drums->getKitName(), juce::String ("Tape Kit"));
            expectWithinAbsoluteError (drums->getPad (0).decay, 1.0, 1.0e-9);
            expectEquals (drums->getPad (3).name, juce::String ("Rim"));
            expect (drums->getPad (2).muted && drums->getPad (3).soloed, "mute and solo are kept");
            expect (! drums->getPad (3).reversed);
            session.undo();
            expectEquals (drums->getKitName(), juce::String ("Trap 808"));
            expectWithinAbsoluteError (drums->getPad (0).decay, 0.5, 1.0e-9);
            expectEquals (drums->getPad (3).name, juce::String ("Rimshot"));
            expect (! session.loadDrumKit (*drums, -1));
            expect (session.setDrumPadMuted (*drums, 2, false) && session.setDrumPadSoloed (*drums, 3, false));

            beginTest ("Your own sample is copied into the song and stored relative to it");
            const auto sample = tempFolder.getChildFile ("my snare.wav");
            expect (writeTestWav (sample, 0.2));
            expect (session.loadDrumPadSample (*drums, 1, sample), session.getLastError());
            const auto stored = drums->getPad (1).file;
            expect (stored.startsWith (juce::String (Session::importedAudioFolderName) + "/"), stored);
            expect (songFile.getParentDirectory().getChildFile (stored).existsAsFile());
            expectEquals (drums->getPad (1).name, juce::String ("my snare"));
            drums->flushPendingUpdates();
            expect (drums->getPadAudio (1) != nullptr, drums->getPadLoadError (1));

            const auto notAudio = tempFolder.getChildFile ("notes.txt");
            expect (notAudio.replaceWithText ("not audio"));
            expect (! session.loadDrumPadSample (*drums, 1, notAudio));
            expectEquals (drums->getPad (1).file, stored, "a refused file changes nothing");

            expect (session.copyDrumPad (*drums, 1, 14));
            expectEquals (drums->getPad (14).file, stored);
            expect (session.resetDrumPad (*drums, 14));
            expectEquals (drums->getPad (14).file, DrumMachinePlugin::makeKitSource (1, 14));
            expectEquals (drums->getPad (14).name, juce::String ("Snap"));

            beginTest ("The drum machine plays, and mute and choke work");
            drums->flushPendingUpdates();
            drums->baseClassInitialise ({ te::TimePosition(), 44100.0, 512 });
            expect (renderDrums (*drums, { { 36, 127 } }) > 0.01f, "the kick sounds");
            renderDrums (*drums, {}, true);
            expect (renderDrums (*drums, {}) < 1.0e-6f, "all notes off stops it");
            expect (renderDrums (*drums, { { 35, 127 }, { 52, 127 } }) < 1.0e-6f, "notes outside the pads are ignored");

            expect (session.setDrumPadMuted (*drums, 0, true));
            drums->flushPendingUpdates();
            expect (renderDrums (*drums, { { 36, 127 } }) < 1.0e-6f, "a muted pad is silent");
            expect (session.setDrumPadMuted (*drums, 0, false));

            // The closed hat (made silent here) cuts the open hat off
            expect (session.setDrumPadValue (*drums, 4, DrumMachinePlugin::Value::level, DrumMachinePlugin::minLevel));
            drums->flushPendingUpdates();
            renderDrums (*drums, {}, true);
            renderDrums (*drums, { { 41, 127 } });
            expect (renderDrums (*drums, {}) > 0.001f, "the open hat rings");
            renderDrums (*drums, { { 40, 127 } });
            expect (renderDrums (*drums, {}) < 1.0e-4f, "the closed hat chokes it");

            expect (session.setDrumPadChoke (*drums, 5, 0));
            drums->flushPendingUpdates();
            renderDrums (*drums, { { 41, 127 } });
            renderDrums (*drums, { { 40, 127 } });
            expect (renderDrums (*drums, {}) > 0.001f, "without a choke group it keeps ringing");
            renderDrums (*drums, {}, true);
            drums->baseClassDeinitialise();

            beginTest ("Steps: on, off, paint-drag in one undo step, no doubles");
            expect (session.createMidiClip (trackIndex, te::TimePosition::fromSeconds (400.0), te::TimeDuration::fromSeconds (8.0)), session.getLastError());
            te::MidiClip::Ptr clip = dynamic_cast<te::MidiClip*> (session.getSelectedClip());
            expect (clip != nullptr && session.getDrumMachineFor (*clip) == drums);

            if (clip == nullptr)
                return;

            const auto count = [&clip] (int pitch)
            {
                int n = 0;

                for (auto* x : clip->getSequence().getNotes())
                    n += (pitch < 0 || x->getNoteNumber() == pitch) ? 1 : 0;

                return n;
            };

            bool changed = false;
            expect (session.setDrumStep (*clip, 36, 0.0, 0.25, true, 100, false, &changed) && changed);
            expect (session.setDrumStep (*clip, 36, 0.0, 0.25, true, 100, false, &changed) && ! changed, "already on");
            expectEquals (count (36), 1);
            expect (session.setDrumStep (*clip, 38, 1.0, 0.25, true, 90, false, &changed) && changed);
            expect (session.setDrumStep (*clip, 38, 1.25, 0.25, true, 90, true, &changed) && changed);
            expect (session.setDrumStep (*clip, 38, 1.5, 0.25, true, 90, true, &changed) && changed);
            expectEquals (count (38), 3);
            session.undo();
            expectEquals (count (38), 0, "the painted steps go in one undo");
            expectEquals (count (36), 1, "and the step before stays");
            expect (session.setDrumStep (*clip, 36, 0.0, 0.25, false, 100, false, &changed) && changed);
            expectEquals (count (36), 0);
            expect (! session.setDrumStep (*clip, 200, 0.0, 0.25, true, 100), "not a note");

            beginTest ("Rolls: even hits in one step, one undo, back to a single hit");
            expect (session.setDrumStep (*clip, 46, 2.0, 0.25, true, 77));
            expect (session.setDrumRoll (*clip, 46, 2.0, 0.25, 4, 100), session.getLastError());
            {
                const auto roll = session.getNotesInRange (*clip, 46, 2.0, 2.25);
                expectEquals (roll.size(), 4, "four hits");

                for (int i = 0; i < roll.size(); ++i)
                {
                    expectWithinAbsoluteError (roll[i]->getStartBeat().inBeats(), 2.0 + i * 0.0625, 1.0e-6, "evenly spaced");
                    expectEquals (roll[i]->getVelocity(), 77, "keeps the step's velocity");
                }
            }
            session.undo();
            expectEquals (session.getNotesInRange (*clip, 46, 2.0, 2.25).size(), 1, "one undo: back to the single hit");
            expect (session.setDrumRoll (*clip, 46, 2.0, 0.25, 3, 100));
            expect (session.setDrumRoll (*clip, 46, 2.0, 0.25, 1, 100));
            expectEquals (session.getNotesInRange (*clip, 46, 2.0, 2.25).size(), 1, "1: a single hit again");
            expect (! session.setDrumRoll (*clip, 46, 2.0, 0.25, 9, 100), "8 at most");
            session.clearDrumRow (*clip, 46, 0.0, 4.0);

            beginTest ("Fill, shift (wrapping), clear, copy and repeat a pattern");
            expect (session.fillDrumRow (*clip, 42, 0.0, 4.0, 0.25, 2, 80));
            expectEquals (count (42), 8, "every second sixteenth of a bar");
            expect (session.fillDrumRow (*clip, 42, 0.0, 4.0, 0.25, 2, 80));
            expectEquals (count (42), 8, "filling again doesn't double up");
            expect (session.fillDrumRow (*clip, 36, 0.0, 4.0, 1.0, 1, 110));
            expect (session.shiftDrumRow (*clip, 36, 0.0, 4.0, -1.0));
            expectEquals (session.getNotesInRange (*clip, 36, 3.0, 4.0).size(), 1, "the first hit wrapped round to the end");
            expectEquals (count (36), 4);
            session.undo();
            expectEquals (session.getNotesInRange (*clip, 36, 0.0, 0.5).size(), 1);

            expect (session.copyDrumPattern (*clip, 0.0, 4.0, 4.0));
            expectEquals (session.getNotesInRange (*clip, -1, 4.0, 8.0).size(), 12);
            expect (! session.copyDrumPattern (*clip, 0.0, 4.0, 2.0), "not over itself");

            const auto lifted = session.getDrumHits (*clip, 0.0, 4.0);   // cut, then paste back
            expectEquals ((int) lifted.size(), 12);
            expect (session.clearDrumRow (*clip, -1, 0.0, 4.0));
            expectEquals (session.getNotesInRange (*clip, -1, 0.0, 4.0).size(), 0);
            expect (session.replaceDrumRange (*clip, 0.0, 4.0, lifted));
            expectEquals (session.getNotesInRange (*clip, -1, 0.0, 4.0).size(), 12, "a cut pattern pastes back whole");
            expect (! session.replaceDrumRange (*clip, 4.0, 4.0, lifted), "an empty range is refused");
            expect (session.repeatDrumPatternToEnd (*clip, 0.0, 4.0), session.getLastError());
            expectEquals (session.getNotesInRange (*clip, -1, 0.0, 16.0).size(), 48, "four bars of the same bar");
            expect (! session.repeatDrumPatternToEnd (*clip, 0.0, 16.0), "nothing after the end of the clip");
            expect (session.clearDrumRow (*clip, 42, 0.0, 16.0));
            expectEquals (count (42), 0);
            expect (session.clearDrumRow (*clip, -1, 0.0, 16.0));
            expectEquals (count (-1), 0);
            session.undo();
            expectEquals (count (36), 16, "one undo brings every pad back");

            beginTest ("Swing at 75% and back keeps hits on their own step; nothing past the clip's end");
            expect (session.clearDrumRow (*clip, -1, 0.0, 16.0));
            // Odd sixteenth (clip content starts on a bar line here), fully swung: half a step late
            expect (session.setDrumStep (*clip, 38, 0.25, 0.25, true, 100, false, nullptr, 0.375));
            auto swung = session.getNotesInRange (*clip, 38, 0.25, 0.5);
            expectEquals (swung.size(), 1, "a swung hit stays inside its own step");
            expect (session.quantiseMidiNotes (*clip, swung, 0.25, 1.0, 0.0));
            expectWithinAbsoluteError (swung.getFirst()->getStartBeat().inBeats(), 0.25, 1.0e-6, "straightened back to its own step, not the next one");
            expect (! session.setDrumStep (*clip, 38, 16.0, 0.25, true, 100), "past the end of the clip");
            expectEquals (session.getNotesInRange (*clip, 38, 16.0, 32.0).size(), 0);
            expect (session.clearDrumRow (*clip, -1, 0.0, 16.0));
            session.undo();
            session.undo();
            session.undo();
            session.undo();   // back to the 16 kicks from before this block
            expectEquals (count (36), 16);

            const auto clipsBefore = clip->getClipTrack()->getClips().size();
            expect (session.createMidiClip (trackIndex, te::TimePosition::fromSeconds (420.0), te::TimeDuration::fromSeconds (2.0), "Pattern"));
            expectEquals (session.getSelectedClip() != nullptr ? session.getSelectedClip()->getName() : juce::String(), juce::String ("Pattern"));
            session.undo();
            expectEquals (clip->getClipTrack()->getClips().size(), clipsBefore, "a named clip goes in one undo");
            session.selectClip (clip.get());

            beginTest ("Drum commands refuse a deleted clip or track");
            session.selectClip (clip.get());
            expect (session.deleteSelectedClips());
            expect (! session.setDrumStep (*clip, 36, 0.0, 0.25, true, 100));
            expect (! session.fillDrumRow (*clip, 36, 0.0, 4.0, 0.25, 1, 100));
            expect (! session.clearDrumRow (*clip, 36, 0.0, 4.0));
            expect (! session.shiftDrumRow (*clip, 36, 0.0, 4.0, 1.0));
            expect (! session.copyDrumPattern (*clip, 0.0, 4.0, 4.0));
            expect (! session.repeatDrumPatternToEnd (*clip, 0.0, 4.0));
            clip = nullptr;

            expect (session.deleteTrack (trackIndex));
            expectEquals (session.getNumAudioTracks(), tracksBefore);
            // `drums` belonged to that track: only its absence is checked from here on
            expect (session.getDrumMachine (trackIndex) == nullptr);
        }

        /** Tempo, time signature and key changes over the song (2E). */
        void runTempoTests (Session& session, const juce::File& songFile)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong() || session.getNumAudioTracks() < 1)
                return;

            auto& edit = *session.getEdit();
            session.setBpm (120.0);
            const auto beatOf = [&edit] (te::Clip& c) { return edit.tempoSequence.toBeats (c.getPosition().getStart()).inBeats(); };

            // A clip at beat 64 (bar 17 in 4/4): tempo and time signature changes must keep it there
            expect (session.createMidiClip (0, te::TimePosition::fromSeconds (32.0), te::TimeDuration::fromSeconds (2.0)), session.getLastError());
            te::Clip::Ptr clip = session.getSelectedClip();
            expect (clip != nullptr);

            if (clip == nullptr)
                return;

            expectWithinAbsoluteError (beatOf (*clip), 64.0, 1.0e-6);

            beginTest ("Tempo changes: add, change, move, ramp, delete; clips stay on their beats");
            expectEquals ((int) session.getTempoMarkers().size(), 1);
            expect (! session.addTempoChange (0.0, 90.0), "the first tempo is already at the start");
            expect (session.addTempoChange (32.0, 60.0), session.getLastError());
            auto markers = session.getTempoMarkers();
            expectEquals ((int) markers.size(), 2);
            expectWithinAbsoluteError (markers[1].bpm, 60.0, 1.0e-9);
            expectWithinAbsoluteError (beatOf (*clip), 64.0, 1.0e-4, "the clip keeps its beat");
            expectWithinAbsoluteError (clip->getPosition().getStart().inSeconds(), 16.0 + 32.0, 1.0e-3, "and so moves later in time");
            expectWithinAbsoluteError (session.getBpmAt (te::TimePosition::fromSeconds (40.0)), 60.0, 1.0e-6);
            expect (! session.addTempoChange (32.0, 70.0), "two changes in one place are refused");

            expect (session.setTempoChangeBpm (1, 100.0));
            expect (session.setTempoChangeBpm (1, 110.0, true));
            session.undo();
            expectWithinAbsoluteError (session.getTempoMarkers()[1].bpm, 60.0, 1.0e-9, "a drag of the value is one undo step");

            expect (session.moveTempoChange (1, 48.0));
            expectWithinAbsoluteError (session.getTempoMarkers()[1].beat, 48.0, 1.0e-9);
            expect (! session.moveTempoChange (0, 8.0), "the first stays put");
            expect (! session.moveTempoChange (1, 0.0), "not onto the first");

            expect (session.setTempoRamp (0, true));
            expect (session.getTempoMarkers()[0].ramp);
            const auto middle = session.getBpmAt (edit.tempoSequence.toTime (te::BeatPosition::fromBeats (24.0)));
            expect (middle < 119.0 && middle > 61.0, "a ramp glides between the two tempos: " + juce::String (middle));
            expect (session.setTempoRamp (0, false));
            expectWithinAbsoluteError (beatOf (*clip), 64.0, 1.0e-4);

            expect (! session.deleteTempoChange (0));
            expect (session.deleteTempoChange (1));
            expectEquals ((int) session.getTempoMarkers().size(), 1);
            expectWithinAbsoluteError (clip->getPosition().getStart().inSeconds(), 32.0, 1.0e-3, "back where it started");
            session.undo();
            expectEquals ((int) session.getTempoMarkers().size(), 2, "delete undoes");
            session.undo();
            session.undo();
            session.undo();
            session.undo();
            expectEquals ((int) session.getTempoMarkers().size(), 1, "and so does the add");
            expectWithinAbsoluteError (clip->getPosition().getStart().inSeconds(), 32.0, 1.0e-3);

            expect (session.setBpmAt (te::TimePosition::fromSeconds (10.0), 100.0));
            expectWithinAbsoluteError (session.getBpm(), 100.0, 1.0e-9, "the transport edits the tempo in effect");
            expectWithinAbsoluteError (beatOf (*clip), 64.0, 1.0e-4);
            session.setBpm (120.0);

            beginTest ("Time signatures: bars follow them, clips keep their beats");
            expect (! session.addTimeSigChange (0, 3, 4));
            expect (! session.addTimeSigChange (4, 0, 4), "bad numerator");
            expect (! session.addTimeSigChange (4, 7, 3), "bad denominator");
            expect (! session.addTimeSigChange (4, 6, 8), "x/8 isn't offered yet");
            expect (session.addTimeSigChange (4, 3, 4), session.getLastError());   // bar 5 onwards is 3/4
            expectEquals ((int) session.getTimeSigMarkers().size(), 2);
            expectEquals (session.getTimeSigMarkers()[1].bar, 4);
            expectWithinAbsoluteError (session.getBarStartBeat (4), 16.0, 1.0e-6);
            expectWithinAbsoluteError (session.getBarStartBeat (5), 19.0, 1.0e-6, "3/4 bars are 3 beats");
            expectEquals (session.getBarAt (20.0).first, 5);
            expectWithinAbsoluteError (session.getBeatsPerBarAt (te::TimePosition::fromSeconds (10.0)), 3.0, 1.0e-9);
            expectWithinAbsoluteError (beatOf (*clip), 64.0, 1.0e-4);
            expect (session.moveTimeSigChange (1, 8));
            expectWithinAbsoluteError (session.getTimeSigMarkers()[1].beat, 32.0, 1.0e-6);
            expect (! session.setTimeSigAt (te::TimePosition::fromSeconds (1.0), 6, 8), "x/8 isn't offered yet");
            expect (session.setTimeSigAt (te::TimePosition::fromSeconds (1.0), 6, 4));
            expectEquals (session.getTimeSigMarkers()[1].bar, 8, "the later change stays on its bar");
            expectWithinAbsoluteError (session.getTimeSigMarkers()[1].beat, 48.0, 1.0e-6, "8 bars of 6");
            expectEquals (session.getTimeSigAt (te::TimePosition()).first, 6);
            expect (! session.deleteTimeSigChange (0));
            expect (session.deleteTimeSigChange (1));
            expectEquals ((int) session.getTimeSigMarkers().size(), 1);
            session.undo();
            session.undo();
            session.undo();
            session.undo();
            expectEquals ((int) session.getTimeSigMarkers().size(), 1);
            expectEquals (session.getTimeSigAt (te::TimePosition()).first, 4, "all undone");

            beginTest ("Keys: unset until chosen, changes over the song");
            expectEquals (session.getKeyAt (te::TimePosition()).root, -1, "a new song has no key");
            expectEquals (Session::describeKey (-1, false), juce::String ("No key"));
            expect (session.setKeyAt (te::TimePosition(), 7, true));
            expectEquals (session.getKeyAt (te::TimePosition::fromSeconds (5.0)).root, 7);
            expect (session.getKeyAt (te::TimePosition::fromSeconds (5.0)).minor);
            expectEquals (Session::describeKey (7, true), juce::String ("G minor"));
            expect (session.addKeyChange (32.0, 3, false));
            expectEquals (session.getKeyAt (te::TimePosition::fromSeconds (20.0)).root, 3, "Eb major from beat 32");
            expectEquals (session.getKeyAt (te::TimePosition::fromSeconds (10.0)).root, 7);
            expect (! session.addKeyChange (32.0, 0, false));
            expect (! session.addKeyChange (8.0, 12, false), "no such key");
            expect (session.moveKeyChange (1, 16.0));
            expectEquals (session.getKeyAt (te::TimePosition::fromSeconds (10.0)).root, 3);
            expect (! session.deleteKeyChange (0));
            expect (session.deleteKeyChange (1));
            expect (session.setKeyAt (te::TimePosition(), -1, false));
            expectEquals (session.getKeyAt (te::TimePosition()).root, -1, "the key can be cleared");
            session.undo();
            expectEquals (session.getKeyAt (te::TimePosition()).root, 7);

            session.selectClip (clip.get());
            expect (session.deleteSelectedClips());
            clip = nullptr;
        }

        /** Sections: add, refuse overlaps, repeat (copies the music), delete with or without the music. */
        void runSectionTests (Session& session, const juce::File& songFile)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong() || session.getNumAudioTracks() < 1)
                return;

            auto& edit = *session.getEdit();
            session.setBpm (120.0);
            auto* track = te::getAudioTracks (edit)[0];
            const auto toBeat = [&edit] (te::TimePosition t) { return edit.tempoSequence.toBeats (t).inBeats(); };
            const auto clipStartsOnTrack = [&] (double fromBeat, double toBeatExclusive)
            {
                juce::Array<double> starts;

                for (auto* c : track->getClips())
                {
                    const auto b = toBeat (c->getPosition().getStart());

                    if (b >= fromBeat - 1.0e-4 && b < toBeatExclusive - 1.0e-4)
                        starts.add (std::round (b * 1000.0) / 1000.0);
                }

                starts.sort();
                return starts;
            };
            const auto findSection = [&session] (const juce::String& sectionName) -> std::optional<Session::Section>
            {
                for (auto& s : session.getSections())
                    if (s.name == sectionName)
                        return s;

                return std::nullopt;
            };

            const auto before = (int) session.getSections().size();

            beginTest ("Sections can be added, never overlap, and are cut back to the next one");
            expect (session.addSection (1000.0, 1016.0, "Verse"), session.getLastError());
            expect (! session.addSection (1008.0, 1012.0, "Inside"), "can't start inside another");
            expect (session.addSection (1016.0, 1032.0, "Hook"));
            expect (session.addSection (990.0, 1020.0, "Intro"));
            expectWithinAbsoluteError (findSection ("Intro")->endBeat, 1000.0, 1.0e-6, "cut back to where the Verse starts");
            expect (! session.addSection (1100.0, 1100.5, "Tiny"), "at least a beat");
            expectEquals ((int) session.getSections().size(), before + 3);
            expect (session.getSections()[(size_t) before].name == "Intro", "in song order");

            const auto verse = findSection ("Verse")->id;
            const auto hook = findSection ("Hook")->id;
            expect (! session.moveSection (hook, 1010.0), "not onto the Verse");
            expect (session.resizeSection (hook, 1016.0, 1040.0));
            expectWithinAbsoluteError (findSection ("Hook")->endBeat, 1040.0, 1.0e-6);
            expect (session.renameSection (hook, "Chorus"));
            expect (! session.renameSection (hook, "  "));
            expect (findSection ("Chorus").has_value());
            expect (session.setSectionColour (hook, juce::Colours::red));
            expect (findSection ("Chorus")->colour == juce::Colours::red);

            // Music: one clip inside the Verse, one after everything
            expect (session.createMidiClip (0, session.getEdit()->tempoSequence.toTime (te::BeatPosition::fromBeats (1004.0)), te::TimeDuration::fromSeconds (2.0)));
            expect (session.createMidiClip (0, session.getEdit()->tempoSequence.toTime (te::BeatPosition::fromBeats (1060.0)), te::TimeDuration::fromSeconds (2.0)));
            expectEquals (clipStartsOnTrack (990.0, 2000.0).size(), 2);

            beginTest ("Repeating a section copies its music and moves the rest along, in one undo");
            expect (session.repeatSection (verse, 2), session.getLastError());
            expectEquals ((int) session.getSections().size(), before + 5, "two more Verse markers");
            expectWithinAbsoluteError (findSection ("Chorus")->startBeat, 1048.0, 1.0e-4, "the Chorus moved along by two verses");
            const auto starts = clipStartsOnTrack (990.0, 2000.0);
            expectEquals (starts.size(), 4);

            if (starts.size() == 4)
            {
                expectWithinAbsoluteError (starts[0], 1004.0, 1.0e-3);
                expectWithinAbsoluteError (starts[1], 1020.0, 1.0e-3, "the first copy");
                expectWithinAbsoluteError (starts[2], 1036.0, 1.0e-3, "the second copy");
                expectWithinAbsoluteError (starts[3], 1092.0, 1.0e-3, "the later clip moved by 32 beats");
            }

            expect (! session.repeatSection (verse, 0));
            session.undo();
            expectEquals ((int) session.getSections().size(), before + 3, "one undo takes it all back");
            expectWithinAbsoluteError (findSection ("Chorus")->startBeat, 1016.0, 1.0e-4);
            expectEquals (clipStartsOnTrack (990.0, 2000.0).size(), 2);

            beginTest ("Selecting a section's clips; deleting with or without its music");
            expect (session.selectSectionClips (verse));
            expectEquals (session.getSelectedClips().size(), 1);

            expect (session.deleteSectionAndMusic (verse), session.getLastError());
            expect (! findSection ("Verse").has_value());
            expectWithinAbsoluteError (findSection ("Chorus")->startBeat, 1000.0, 1.0e-4, "the gap closed");
            const auto afterDelete = clipStartsOnTrack (990.0, 2000.0);
            expectEquals (afterDelete.size(), 1, "the Verse's clip went with it");

            if (afterDelete.size() == 1)
                expectWithinAbsoluteError (afterDelete[0], 1044.0, 1.0e-3, "the later clip moved back 16 beats");

            session.undo();
            expect (findSection ("Verse").has_value(), "undo brings it back");
            expectEquals (clipStartsOnTrack (990.0, 2000.0).size(), 2);

            const auto intro = findSection ("Intro")->id;
            expect (session.deleteSection (intro));
            expect (! findSection ("Intro").has_value());
            expectEquals (clipStartsOnTrack (990.0, 2000.0).size(), 2, "deleting just the section keeps the music");
            expect (! session.renameSection (intro, "Gone"), "a deleted section is refused");

            // Tidy up: no sections or clips left in this part of the song
            for (auto& s : session.getSections())
                if (s.startBeat >= 980.0)
                    session.deleteSection (s.id);

            juce::Array<te::Clip*> leftovers;

            for (auto* c : track->getClips())
                if (toBeat (c->getPosition().getStart()) >= 980.0)
                    leftovers.add (c);

            session.selectClips (leftovers);
            session.deleteSelectedClips();

            beginTest ("Deleting a section with its music keeps the tempo the later music was at");
            expect (session.addSection (1000.0, 1016.0, "Slow"));
            expect (session.addTempoChange (1000.0, 90.0));
            const auto slow = findSection ("Slow")->id;
            expect (session.addTempoChange (1008.0, 100.0));
            expect (! session.repeatSection (slow, 1), "a tempo change inside the section: refused, nothing changes");
            expect (session.deleteTempoChange ((int) session.getTempoMarkers().size() - 1));
            expect (session.deleteSectionAndMusic (slow), session.getLastError());
            expectWithinAbsoluteError (session.getBpmAt (edit.tempoSequence.toTime (te::BeatPosition::fromBeats (1001.0))), 90.0, 1.0e-6,
                                       "the change at the section's start was put back");
            for (int i = 0; i < 5; ++i)
                session.undo();

            expectEquals ((int) session.getTempoMarkers().size(), 1);
            expect (! findSection ("Slow").has_value());

            beginTest ("Time signature changes keep their order and their bars");
            expect (session.setTimeSigAt (te::TimePosition(), 2, 4));
            expect (session.addTimeSigChange (4, 6, 4));    // beat 8
            expect (session.addTimeSigChange (6, 4, 4));    // beat 8 + 2 x 6 = 20
            expectWithinAbsoluteError (session.getTimeSigMarkers()[2].beat, 20.0, 1.0e-6);
            expect (! session.moveTimeSigChange (1, 8), "can't jump past the next change");
            expect (session.addTimeSigChange (2, 3, 4));    // beat 4: the later ones stay on bars 4 and 6
            const auto sigs = session.getTimeSigMarkers();
            expectEquals ((int) sigs.size(), 4);

            if (sigs.size() == 4)
            {
                expectEquals (sigs[2].bar, 4);
                expectWithinAbsoluteError (sigs[2].beat, 4.0 + 2 * 3.0, 1.0e-6, "two 3/4 bars after bar 2");
                expectEquals (sigs[3].bar, 6);
                expect (sigs[1].beat < sigs[2].beat && sigs[2].beat < sigs[3].beat, "still in order");
            }

            for (int i = 0; i < 4; ++i)
                session.undo();

            expectEquals ((int) session.getTimeSigMarkers().size(), 1);
            expectEquals (session.getTimeSigAt (te::TimePosition()).first, 4);
        }

        /** Our built-in effects: every one adds, has saved controls, renders cleanly; the limiter holds its ceiling. */
        void runEffectTests (Session& session, const juce::File& songFile)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong())
                return;

            beginTest ("Every built-in effect has a category, adds to a track and has controls");
            session.addAudioTrack();
            const auto t = session.getNumAudioTracks() - 1;
            const auto choices = session.getAvailableEffects();
            int ours = 0;

            for (const auto& c : choices)
            {
                if (c.pluginType == te::ExternalPlugin::xmlTypeName)
                    continue;

                expect (c.category.isNotEmpty(), c.name + " has no category");
                const auto before = session.getEffectInfo (t).size();
                expect (session.addEffect (t, c, false), c.name + ": " + session.getLastError());
                expectEquals (session.getEffectInfo (t).size(), before + 1, c.name);

                if (juce::String (c.pluginType).startsWith ("ez"))
                    ++ours;
            }

            expectEquals (ours, 11, "gate, de-esser, limiter, cut, saturator, tune, tremolo, utility, vocal chain, amp, tuner");

            for (auto* plugin : te::getAudioTracks (*session.getEdit())[t]->pluginList)
                if (auto* fx = dynamic_cast<EffectPlugin*> (plugin))
                    expect (fx->getNumControls() > 0 && (int) fx->getAutomatableParameters().size() == fx->getNumControls(), fx->getName());

            beginTest ("A full track says so, instead of dropping the effect");
            {
                session.addAudioTrack();
                const auto full = session.getNumAudioTracks() - 1;
                bool refused = false;

                for (int i = 0; i < 60 && ! refused; ++i)
                {
                    const auto before = session.getEffectInfo (full).size();
                    refused = ! session.addEffect (full, choices[0], false);
                    expectEquals (session.getEffectInfo (full).size(), before + (refused ? 0 : 1), "added means added");
                }

                expect (refused && session.getLastError().contains ("full"), session.getLastError());
                expect (session.getEffectInfo (full).size() >= 30, "room for plenty: " + juce::String (session.getEffectInfo (full).size()));
                session.deleteTrack (full);
            }

            beginTest ("A control set on our effect is one undo step, and survives reopening");
            GatePlugin* gate = nullptr;

            for (auto* plugin : te::getAudioTracks (*session.getEdit())[t]->pluginList)
                if (auto* g = dynamic_cast<GatePlugin*> (plugin))
                    gate = g;

            if (gate == nullptr)
            {
                expect (false, "setup: no gate");
                return;
            }

            session.beginUndoStep();
            gate->set (GatePlugin::threshold, -20.0f);
            expectWithinAbsoluteError (gate->getStored (GatePlugin::threshold), -20.0f, 0.01f);
            session.undo();
            expectWithinAbsoluteError (gate->getStored (GatePlugin::threshold), -50.0f, 0.01f, "undo puts the default back");
            expectWithinAbsoluteError (gate->get (GatePlugin::threshold), -50.0f, 0.01f, "and the sound follows (Tracktion leaves the parameter behind)");
            session.redo();
            const auto gateID = gate->itemID;
            expect (session.saveSong() && session.closeSong() && session.openSong (songFile), session.getLastError());
            auto reopenedPlugin = te::findPluginForID (*session.getEdit(), gateID);
            auto* reopened = dynamic_cast<GatePlugin*> (reopenedPlugin.get());
            expect (reopened != nullptr && std::abs (reopened->getStored (GatePlugin::threshold) + 20.0f) < 0.01f, "kept after reopening");

            beginTest ("Our effects make clean sound; the limiter never goes over its ceiling");
            if (! session.addInstrumentTrack (session.getAvailableInstruments()[0], false))
            {
                expect (false, "setup: no synth: " + session.getLastError());
                return;
            }

            auto& edit = *session.getEdit();
            const auto s = session.getNumAudioTracks() - 1;
            auto* synth = te::getAudioTracks (edit)[s];

            for (auto* track : te::getAudioTracks (edit))
            {
                track->setMute (track != synth);
                track->setSolo (false);
            }

            session.setAutomationRead (true);
            auto clip = synth->insertMIDIClip (te::TimeRange (te::TimePosition(), te::TimePosition::fromSeconds (1.0)), nullptr);

            if (clip == nullptr)
            {
                expect (false, "setup: no clip");
                return;
            }

            for (int note : { 48, 55, 60, 64 })
                clip->getSequence().addNote (note, te::BeatPosition(), te::BeatDuration::fromBeats (2.0), 127, 0, nullptr);

            const auto dry = renderPeak (edit, 1.0);
            expect (dry > 0.01f, "the synth plays");

            for (const auto& c : choices)
                if (juce::String (c.pluginType).startsWith ("ez") && c.pluginType != juce::String (GatePlugin::xmlTypeName))
                {
                    expect (session.addEffect (s, c, false), c.name);
                    const auto peak = renderPeak (edit, 1.0);
                    expect (std::isfinite (peak) && peak > 0.001f && peak < 8.0f, c.name + " peak " + juce::String (peak));
                    session.undo();
                }

            // Utility +24 dB into a limiter at -6: the limiter is the last word
            const auto utility = std::find_if (choices.begin(), choices.end(), [] (auto& c) { return c.pluginType == juce::String (UtilityPlugin::xmlTypeName); });
            const auto limiter = std::find_if (choices.begin(), choices.end(), [] (auto& c) { return c.pluginType == juce::String (LimiterPlugin::xmlTypeName); });
            expect (utility != choices.end() && limiter != choices.end());
            expect (session.addEffect (s, *utility, false) && session.addEffect (s, *limiter, false));

            for (auto* plugin : synth->pluginList)
            {
                if (auto* u = dynamic_cast<UtilityPlugin*> (plugin))   u->set (UtilityPlugin::gain, 24.0f);
                if (auto* l = dynamic_cast<LimiterPlugin*> (plugin))   l->set (LimiterPlugin::ceiling, -6.0f);
            }

            session.setTrackVolumeDb (s, 0.0f);
            const auto limited = renderPeak (edit, 1.0);
            const auto ceilingAtOutput = juce::Decibels::decibelsToGain (-6.0f) * 1.05f;   // (test renders skip the master fader)
            expect (limited <= ceilingAtOutput && limited > ceilingAtOutput * 0.5f,
                    "at the ceiling: " + juce::String (limited) + " vs " + juce::String (ceilingAtOutput));
            clip = nullptr;

            runVocalChainTests (session, s);
        }

        /** The Vocal Chain: presets set macros and switches in one step, a hand-moved macro says "(edited)". */
        void runVocalChainTests (Session& session, int t)
        {
            beginTest ("Vocal Chain presets: one undo step, macros and switches, the name follows");
            const auto choices = session.getAvailableEffects();
            const auto chainChoice = std::find_if (choices.begin(), choices.end(), [] (auto& c) { return c.pluginType == juce::String (VocalChainPlugin::xmlTypeName); });

            if (chainChoice == choices.end() || ! session.addEffect (t, *chainChoice, false))
            {
                expect (false, "setup: no Vocal Chain: " + session.getLastError());
                return;
            }

            const auto e = session.getEffectInfo (t).size() - 1;
            expectEquals (session.getEffectInfo (t)[e].type, juce::String (VocalChainPlugin::xmlTypeName));
            expectEquals (session.getVocalChainPresetName (t, e), juce::String (VocalChainPlugin::getPresets().front().name), "starts on the first preset");

            const auto presets = Session::getVocalChainPresets();
            int phonePreset = -1;

            for (size_t i = 0; i < presets.size(); ++i)
                if (presets[i].name.startsWith ("Phone"))
                    phonePreset = (int) i;

            expect (phonePreset >= 0 && session.applyVocalChainPreset (t, e, phonePreset), session.getLastError());
            expectEquals (session.getEffectControl (t, e, VocalChainPlugin::phone), 1.0f, "phone tone on");
            expectEquals (session.getEffectControl (t, e, VocalChainPlugin::widthOn), 0.0f, "width off in this preset");
            expectEquals (session.getEffectControl (t, e, VocalChainPlugin::tune), 0.0f);
            session.undo();
            expectEquals (session.getEffectControl (t, e, VocalChainPlugin::phone), 0.0f, "one undo takes the whole preset back");
            expectEquals (session.getVocalChainPresetName (t, e), juce::String (VocalChainPlugin::getPresets().front().name));

            beginTest ("Moving a macro by hand: one step per drag, and the preset says (edited)");
            expect (session.setEffectControl (t, e, VocalChainPlugin::space, 70.0f));
            expect (session.setEffectControl (t, e, VocalChainPlugin::space, 80.0f, true));
            expectEquals (session.getEffectControl (t, e, VocalChainPlugin::space), 80.0f);
            expect (session.getVocalChainPresetName (t, e).endsWith ("(edited)"), session.getVocalChainPresetName (t, e));
            session.undo();
            expectWithinAbsoluteError (session.getEffectControl (t, e, VocalChainPlugin::space), VocalChainPlugin::getPresets().front().macros[VocalChainPlugin::space], 0.01f);
            expect (! session.getVocalChainPresetName (t, e).endsWith ("(edited)"), "undo takes the (edited) back too");

            beginTest ("The chain renders clean sound with every preset");
            auto& edit = *session.getEdit();

            for (size_t i = 0; i < presets.size(); ++i)
            {
                expect (session.applyVocalChainPreset (t, e, (int) i));
                const auto peak = renderPeak (edit, 1.0);
                expect (std::isfinite (peak) && peak > 0.001f && peak < 1.5f, presets[i].name + ": " + juce::String (peak));   // (the limiter before it holds -6 dB)
            }
        }

        /** Automation lanes: points, curves, undo steps, locking, lanes shown, saving, and the sound. */
        void runAutomationTests (Session& session, const juce::File& songFile)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong())
                return;

            session.addAudioTrack();
            const auto t = session.getNumAudioTracks() - 1;
            const auto volume = session.getVolumeParam (t);

            beginTest ("Every control a lane can draw: volume, pan, sends, then each effect's");
            {
                const auto params = session.getAutomatableParams (t);
                expect (params.size() >= 2 && params[0].ref == volume && params[1].ref == session.getPanParam (t));
                expect (session.addEffect (t, session.getAvailableEffects()[0], false), session.getLastError());
                const auto withEq = session.getAutomatableParams (t);
                expect (withEq.size() > params.size(), "the EQ's controls are there too");
                expect (session.formatParamValue (t, volume, 1.0f).containsIgnoreCase ("dB"), session.formatParamValue (t, volume, 1.0f));
                session.undo();
            }

            beginTest ("A shows the volume lane (A again hides it); lanes are kept by control, never twice");
            expect (session.getShownAutomationLanes (t).empty());
            expect (session.toggleAutomationLanes (t));
            expect (session.getShownAutomationLanes (t).size() == 1 && session.getShownAutomationLanes (t)[0] == volume);
            expect (session.setAutomationLaneShown (t, session.getPanParam (t), true));
            expect (session.setAutomationLaneShown (t, session.getPanParam (t), true));
            expectEquals ((int) session.getShownAutomationLanes (t).size(), 2, "pan only once");
            expect (session.replaceAutomationLane (t, 1, volume));
            expectEquals ((int) session.getShownAutomationLanes (t).size(), 1, "changing a lane to one already shown merges them");
            expect (! session.toggleAutomationLanes (t));
            expect (session.getShownAutomationLanes (t).empty());
            session.toggleAutomationLanes (t);

            beginTest ("The first point on an empty lane also gets one at the start with today's value");
            {
                const auto pan = session.getPanParam (t);
                const auto before = session.getParamValue (t, pan);
                const auto first = session.addAutomationPoint (t, pan, 2.0, 1.0f);
                const auto panPoints = session.getAutomationPoints (t, pan);
                expect (first == 1 && panPoints.size() == 2, juce::String (first));
                expect (panPoints.size() == 2 && panPoints[0].seconds == 0.0, "the anchor is at the start");
                expectWithinAbsoluteError (panPoints[0].value, before, 0.01f, "with the value the control had");
                session.undo();
                expect (! session.isAutomated (t, pan), "and one undo takes both");
            }

            beginTest ("Points: add, read back between them, one undo step each");
            expect (! session.isAutomated (t, volume));
            const auto a = session.addAutomationPoint (t, volume, 0.0, 0.2f);   // at the start: no anchor needed
            const auto b = session.addAutomationPoint (t, volume, 3.0, 0.8f);
            expect (a == 0 && b == 1, juce::String (a) + "," + juce::String (b));
            expect (session.isAutomated (t, volume));
            expectWithinAbsoluteError (session.getAutomationValueAt (t, volume, 1.5), 0.5f, 0.02f, "a straight line between them");
            expectWithinAbsoluteError (session.getAutomationValueAt (t, volume, 9.0), 0.8f, 0.02f, "after the last: its value");
            session.undo();
            expectEquals ((int) session.getAutomationPoints (t, volume).size(), 1);
            session.redo();
            expectEquals ((int) session.getAutomationPoints (t, volume).size(), 2);

            beginTest ("Dragging a point: never past its neighbours, one undo step for the whole drag");
            session.addAutomationPoint (t, volume, 5.0, 0.5f);
            expect (session.moveAutomationPoint (t, volume, 1, 3.5, 0.9f));
            expect (session.moveAutomationPoint (t, volume, 1, 9.0, 0.7f, true));   // past the next point: stops before it
            auto pts = session.getAutomationPoints (t, volume);
            expect (pts.size() == 3 && pts[1].seconds < pts[2].seconds && pts[1].seconds > 4.9, juce::String (pts[1].seconds));
            expectWithinAbsoluteError (pts[1].value, 0.7f, 0.01f);
            session.undo();
            pts = session.getAutomationPoints (t, volume);
            expectWithinAbsoluteError (pts[1].seconds, 3.0, 0.001, "one undo puts the drag back");
            expectWithinAbsoluteError (pts[1].value, 0.8f, 0.01f);

            beginTest ("Curves: bend a line, a bend-drag is one step");
            expect (session.setAutomationCurve (t, volume, 0, 0.3f));
            expect (session.setAutomationCurve (t, volume, 0, 0.6f, true));
            expectWithinAbsoluteError (session.getAutomationPoints (t, volume)[0].curve, 0.6f, 0.01f);
            expect (std::abs (session.getAutomationValueAt (t, volume, 1.5) - 0.5f) > 0.02f, "a bent line isn't straight any more");
            session.undo();
            expectWithinAbsoluteError (session.getAutomationPoints (t, volume)[0].curve, 0.0f, 0.01f);

            beginTest ("An automated fader follows its lane: moving it by hand is refused, with a hint");
            session.setAutomationRead (true);
            expect (session.isParamLockedByAutomation (t, volume));
            expect (! session.setTrackVolumeDb (t, -20.0f), "refused");
            expect (session.getHint().containsIgnoreCase ("automated"), session.getHint());
            session.setAutomationRead (false);
            expect (! session.isParamLockedByAutomation (t, volume));
            expect (session.setTrackVolumeDb (t, -20.0f), "Read off: it moves");
            expect (session.getAutomationPoints (t, volume).size() == 3, "and moving it by hand leaves the lane alone");
            session.setAutomationRead (false);
            session.setAutomationWrite (true);
            expect (session.isAutomationReadOn() && session.isAutomationWriteOn(), "Write turns Read on");
            session.setAutomationWrite (false);

            beginTest ("Delete, clear, paste: each one undo step");
            expect (session.deleteAutomationPoints (t, volume, { 0, 1, 1 }));
            expectEquals ((int) session.getAutomationPoints (t, volume).size(), 1, "duplicates ignored");
            session.undo();
            expectEquals ((int) session.getAutomationPoints (t, volume).size(), 3);
            expect (session.clearAutomation (t, volume));
            expect (! session.isAutomated (t, volume));
            session.undo();
            expect (session.addAutomationPoints (t, volume, { { 10.0, 0.1f, 0.0f }, { 11.0, 0.9f, 0.5f } }));
            expectEquals ((int) session.getAutomationPoints (t, volume).size(), 5);
            session.undo();
            expectEquals ((int) session.getAutomationPoints (t, volume).size(), 3, "a paste undoes in one go");

            beginTest ("A lane for an effect goes when the effect does; lanes and points survive reopening");
            expect (session.addEffect (t, session.getAvailableEffects()[0], false));
            Session::ParamRef eqParam;

            for (const auto& p : session.getAutomatableParams (t))
                if (p.group != "Track" && p.group != "Sends")
                    eqParam = p.ref;

            expect (eqParam.isValid() && session.setAutomationLaneShown (t, eqParam, true));
            expectEquals ((int) session.getShownAutomationLanes (t).size(), 2);
            expect (session.removeEffect (t, (int) session.getEffectNames (t).size() - 1));
            expectEquals ((int) session.getShownAutomationLanes (t).size(), 1, "its lane is gone with it");
            const auto trackID = te::getAudioTracks (*session.getEdit())[t]->itemID;
            expect (session.saveSong() && session.closeSong() && session.openSong (songFile), session.getLastError());
            {
                auto tracks = te::getAudioTracks (*session.getEdit());
                const auto reopened = tracks.indexOf (te::findAudioTrackForID (*session.getEdit(), trackID));
                expect (reopened >= 0 && session.getShownAutomationLanes (reopened).size() == 1);
                expectEquals ((int) session.getAutomationPoints (reopened, session.getVolumeParam (reopened)).size(), 3);
            }

            runAutomationSoundTest (session);
        }

        /** Rendered: a volume lane held at the bottom silences the track; with Read off it doesn't. */
        void runAutomationSoundTest (Session& session)
        {
            beginTest ("Volume automation really changes the sound");

            if (! session.addInstrumentTrack (session.getAvailableInstruments()[0], false))
            {
                expect (false, "setup: no synth: " + session.getLastError());
                return;
            }

            auto& edit = *session.getEdit();
            const auto t = session.getNumAudioTracks() - 1;
            auto* synth = te::getAudioTracks (edit)[t];

            for (auto* track : te::getAudioTracks (edit))
            {
                track->setMute (track != synth);
                track->setSolo (false);
            }

            auto clip = synth->insertMIDIClip (te::TimeRange (te::TimePosition(), te::TimePosition::fromSeconds (1.0)), nullptr);

            if (clip == nullptr)
            {
                expect (false, "setup: no clip");
                return;
            }

            // The note starts a little in: each render begins with a short fade from the fader's last value
            clip->getSequence().addNote (60, te::BeatPosition::fromBeats (0.5), te::BeatDuration::fromBeats (1.5), 100, 0, nullptr);
            session.setAutomationRead (true);
            const auto dry = renderPeak (edit, 1.0);
            expect (dry > 0.01f, "the synth plays");

            const auto volume = session.getVolumeParam (t);
            expect (session.addAutomationPoints (t, volume, { { 0.0, 0.0f, 0.0f }, { 1.0, 0.0f, 0.0f } }));
            const auto automated = renderPeak (edit, 1.0);
            expect (automated < dry * 0.05f, "the lane at the bottom silences it (" + juce::String (automated) + ")");

            session.setAutomationRead (false);
            expect (session.setTrackVolumeDb (t, 0.0f), "Read off: the fader moves by hand again");
            const auto readOff = renderPeak (edit, 1.0);
            expect (readOff > dry * 0.5f, "and the lane is ignored (" + juce::String (readOff) + ")");
            session.setAutomationRead (true);
            clip = nullptr;
        }

        /** FX returns, sends and buses: what each is, and that routing survives every edit. */
        /** Waits for a background export (and its loudness measurement) to finish. */
        const Session::ExportStatus& waitForExport (Session& session)
        {
            for (int i = 0; i < 6000 && session.isExporting(); ++i)
            {
                pumpMessages (5);
                session.updateExport();
            }

            session.updateExport();
            return session.getExportStatus();
        }

        void runExportTests (Session& session, const juce::File& tempFolder)
        {
            using State = Session::ExportStatus::State;

            beginTest ("Export: an empty song says so instead of writing a silent file");
            expect (session.createSong ("Export Test"), session.getLastError());
            auto settings = session.getDefaultExportSettings();
            settings.folder = tempFolder.getChildFile ("Exports");
            settings.sampleRate = 48000.0;
            expect (! session.startExport (settings), "nothing to export yet");
            expect (session.getLastError().contains ("no clips"), session.getLastError());

            settings.range = Session::ExportRange::selection;
            expect (! session.startExport (settings) && session.getLastError().contains ("Select"), "no selection: says what to do");
            settings.range = Session::ExportRange::wholeSong;

            const auto wav = tempFolder.getChildFile ("export-source.wav");
            expect (writeTestWav (wav, 2.0));
            expect (session.importAudioFileAt (wav, 0, te::TimePosition()), session.getLastError());
            expect (session.describeExportRange (Session::ExportRange::wholeSong).startsWith ("1.1 to "),
                    session.describeExportRange (Session::ExportRange::wholeSong));
            expect (session.estimateExport (settings).startsWith ("About "), session.estimateExport (settings));

            beginTest ("Export: the full mix is written as asked and lands on the loudness target");
            settings.matchLoudness = true;
            settings.targetLufs = -16.0f;
            expect (session.startExport (settings), session.getLastError());
            expect (session.isExporting(), "it runs in the background");
            expect (! session.startExport (settings), "one export at a time");
            auto status = waitForExport (session);
            expect (status.state == State::done, "finished: " + status.message);
            expectEquals (status.files.size(), 1);
            const auto mixFile = settings.folder.getChildFile (settings.fileName + ".wav");
            expect (status.files.getFirst() == mixFile, status.files.getFirst().getFullPathName());

            {
                juce::WavAudioFormat format;
                std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (mixFile.createInputStream().release(), true));
                expect (reader != nullptr, "the file reads back");

                if (reader != nullptr)
                {
                    expectEquals (reader->sampleRate, 48000.0);
                    expectEquals ((int) reader->bitsPerSample, 24);
                    expectEquals ((int) reader->numChannels, 2);
                    const auto seconds = (double) reader->lengthInSamples / reader->sampleRate;
                    expect (seconds >= 1.99 && seconds < 12.5, "the song plus at most the ring-out (" + juce::String (seconds) + " s)");
                }
            }

            expect (status.loudnessValid, "the result is measured from the file");
            expectWithinAbsoluteError (status.integratedLufs, -16.0f, 1.0f, "on the loudness target");
            expect (status.truePeakDb <= -0.8f, "true peak under the -1 dB ceiling (" + juce::String (status.truePeakDb) + ")");
            const auto onTarget = status.integratedLufs;
            session.clearExportStatus();
            expect (session.getExportStatus().state == State::idle);

            beginTest ("Export: without the loudness target the master fader decides the level");
            settings.matchLoudness = false;
            session.setMasterVolumeDb (0.0f);
            expect (session.startExport (settings), session.getLastError());
            const auto loud = waitForExport (session).integratedLufs;
            session.clearExportStatus();
            session.setMasterVolumeDb (-6.0f);
            expect (session.startExport (settings), session.getLastError());
            const auto quiet = waitForExport (session).integratedLufs;
            session.clearExportStatus();
            expectWithinAbsoluteError (loud - quiet, 6.0f, 0.5f, "the master fader is in the export (" + juce::String (loud) + " vs " + juce::String (quiet) + ")");
            expect (std::abs (onTarget - loud) > 0.5f, "and matching really changed the level");
            session.setMasterVolumeDb (-3.0f);

            beginTest ("Export: stems are one file per track with clips, named after the tracks");
            expect (session.importAudioFileAt (wav, 1, te::TimePosition::fromSeconds (0.5)), session.getLastError());
            settings.what = Session::ExportWhat::stems;
            settings.fileName = "Export Test";
            const auto stemFiles = session.getExportFiles (settings);
            expectEquals (stemFiles.size(), 2);
            expect (session.startExport (settings), session.getLastError());
            status = waitForExport (session);
            expect (status.state == State::done, status.message);
            expectEquals (status.files.size(), 2);

            for (auto& f : stemFiles)
                expect (f.existsAsFile() && f.getFileName().startsWith ("Export Test - "), f.getFullPathName());

            session.clearExportStatus();

            beginTest ("Export: MIDI needs a MIDI clip, then writes one file per MIDI track");
            settings.what = Session::ExportWhat::midi;
            expect (! session.startExport (settings) && session.getLastError().contains ("MIDI"), session.getLastError());

            if (session.addInstrumentTrack (session.getAvailableInstruments()[0], false))
            {
                auto* synth = te::getAudioTracks (*session.getEdit()).getLast();

                if (auto clip = synth->insertMIDIClip (te::TimeRange (te::TimePosition(), te::TimePosition::fromSeconds (1.0)), nullptr))
                    clip->getSequence().addNote (60, te::BeatPosition(), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);

                expectEquals (session.getExportFiles (settings).size(), 1);
                expect (session.startExport (settings), session.getLastError());
                status = waitForExport (session);
                expect (status.state == State::done, status.message);
                expect (status.files.size() == 1 && status.files.getFirst().hasFileExtension (".mid") && status.files.getFirst().getSize() > 0,
                        "a .mid file");
                session.clearExportStatus();
            }
            else
            {
                expect (false, "setup: no synth: " + session.getLastError());
            }

            beginTest ("Export: MP3 without an encoder is refused with what to do");
            settings.what = Session::ExportWhat::fullMix;
            settings.format = Session::ExportFormat::mp3;

            if (! session.isExportFormatAvailable (Session::ExportFormat::mp3))
                expect (! session.startExport (settings) && session.getLastError().contains ("lame"), session.getLastError());

            settings.format = Session::ExportFormat::flac;
            settings.bitDepth = 32;
            expect (session.startExport (settings), session.getLastError());
            status = waitForExport (session);
            expect (status.state == State::done && status.files.getFirst().hasFileExtension (".flac"), "FLAC (24-bit at most): " + status.message);
            session.clearExportStatus();

            beginTest ("Export: cancelling stops it, and closing the song during an export is safe");
            settings.format = Session::ExportFormat::wav;
            expect (session.startExport (settings), session.getLastError());
            session.cancelExport();
            status = waitForExport (session);
            expect (status.state == State::cancelled || status.state == State::done, "stopped: " + status.message);
            expect (! session.isExporting());
            session.clearExportStatus();

            expect (session.startExport (settings), session.getLastError());
            expect (session.closeSong());
            expect (! session.isExporting(), "closing the song cancels the export");
        }

        static std::vector<float> sine (float hz, double rate, int length, float gain)
        {
            std::vector<float> v ((size_t) length);

            for (int i = 0; i < length; ++i)
                v[(size_t) i] = gain * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate);

            return v;
        }

        /** A plucked-string-like tone: the note plus its overtones, the 2nd as loud as the 1st (as a real string is). */
        static std::vector<float> string (float hz, double rate, int length)
        {
            std::vector<float> v ((size_t) length);

            for (int i = 0; i < length; ++i)
            {
                const auto t = juce::MathConstants<double>::twoPi * hz * i / rate;
                v[(size_t) i] = (float) (0.3 * std::sin (t) + 0.3 * std::sin (2 * t + 0.4) + 0.15 * std::sin (3 * t + 1.1) + 0.08 * std::sin (4 * t));
            }

            return v;
        }

        void runGuitarTests (Session& session, const juce::File& songFile)
        {
            beginTest ("The tuner finds guitar and bass notes, to within a cent");
            {
                constexpr double rate = TunerPlugin::analysisRate;
                // Low B of a 5-string bass, drop-A, low E, A, D, G, B, high E, 12th fret high E
                for (float hz : { 30.87f, 55.0f, 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f, 659.26f })
                {
                    const auto tone = string (hz, rate, 2048);
                    const auto r = TunerPlugin::analyse (tone.data(), 2048, rate, 440.0f);
                    expect (r.hasNote, juce::String (hz) + " Hz: no note");
                    expectWithinAbsoluteError (r.hz, hz, hz * 0.0006f, "the pitch, not an overtone");
                    expect (std::abs (r.cents) < 1.0f, juce::String (hz) + " Hz is in tune, not " + juce::String (r.cents) + " cents");
                }

                const auto e = string (82.41f, rate, 2048);
                const auto low = TunerPlugin::analyse (e.data(), 2048, rate, 440.0f);
                expectEquals (low.noteName, juce::String ("E"));
                expectEquals (low.octave, 2);

                const auto flat = string (82.41f * std::pow (2.0f, -0.2f / 12.0f), rate, 2048);   // 20 cents flat
                expectWithinAbsoluteError (TunerPlugin::analyse (flat.data(), 2048, rate, 440.0f).cents, -20.0f, 1.0f);

                const auto a432 = sine (432.0f, rate, 2048, 0.5f);
                expectWithinAbsoluteError (TunerPlugin::analyse (a432.data(), 2048, rate, 432.0f).cents, 0.0f, 1.0f, "tuned to A = 432");

                std::vector<float> quiet (2048, 0.0f);
                juce::Random random (7);

                for (auto& q : quiet)
                    q = (random.nextFloat() - 0.5f) * 0.0005f;

                expect (! TunerPlugin::analyse (quiet.data(), 2048, rate, 440.0f).hasNote, "silence (a little hiss) shows no note");
            }

            beginTest ("The amp: every mode makes sound at a sensible level, never runaway, never NaN");
            {
                constexpr double rate = 48000.0;
                constexpr int length = 48000;

                for (int mode = 0; mode <= 5; ++mode)
                {
                    for (float gain : { 0.0f, 5.0f, 10.0f })
                    {
                        dsp::Amp amp;
                        amp.mode = (dsp::Amp::Mode) mode;
                        amp.cab = mode == 5 ? dsp::Amp::Cab::bass8x10 : dsp::Amp::Cab::closed4x12;
                        amp.gain = gain;
                        amp.prepare (rate);

                        auto left = string (mode == 5 ? 55.0f : 110.0f, rate, length);
                        auto right = left;
                        float* ch[] = { left.data(), right.data() };

                        for (int start = 0; start < length; start += 480)
                        {
                            float* block[] = { ch[0] + start, ch[1] + start };
                            amp.process (block, 2, juce::jmin (480, length - start));
                        }

                        float peak = 0;
                        bool finite = true;

                        for (int i = length / 2; i < length; ++i)
                        {
                            finite = finite && std::isfinite (left[(size_t) i]);
                            peak = juce::jmax (peak, std::abs (left[(size_t) i]));
                        }

                        const auto label = AmpPlugin::modeNames()[mode] + " at gain " + juce::String (gain);
                        expect (finite, label + ": NaN or infinity");
                        expect (peak < 1.5f, label + ": too loud, " + juce::String (dsp::gainToDb (peak), 1) + " dB");
                        expect (peak > dsp::dbToGain (-36.0f), label + ": too quiet, " + juce::String (dsp::gainToDb (peak), 1) + " dB");
                    }
                }
            }

            beginTest ("High gain distorts; the gate silences the hiss between chugs");
            {
                constexpr double rate = 48000.0;

                const auto harmonics = [rate] (dsp::Amp::Mode mode, float gain)
                {
                    dsp::Amp amp;
                    amp.mode = mode;
                    amp.cab = dsp::Amp::Cab::off;
                    amp.gain = gain;
                    amp.prepare (rate);
                    auto x = sine (220.0f, rate, 9600, 0.25f);
                    float* ch[] = { x.data() };
                    amp.process (ch, 1, 9600);

                    // How much of the output isn't the 220 Hz sine: the distortion
                    double fund = 0, fundQ = 0, total = 0;

                    for (int i = 4800; i < 9600; ++i)
                    {
                        const auto t = juce::MathConstants<double>::twoPi * 220.0 * i / rate;
                        fund += x[(size_t) i] * std::sin (t);
                        fundQ += x[(size_t) i] * std::cos (t);
                        total += (double) x[(size_t) i] * x[(size_t) i];
                    }

                    const auto fundamentalPower = 2.0 * (fund * fund + fundQ * fundQ) / 4800.0 / 4800.0 * 2.0;
                    return 1.0 - fundamentalPower / juce::jmax (1.0e-12, total / 4800.0 * 2.0);
                };

                const auto clean = harmonics (dsp::Amp::Mode::clean, 0.0f);
                const auto metal = harmonics (dsp::Amp::Mode::metal, 10.0f);
                expect (clean < 0.02, "clean at gain 0 stays clean: " + juce::String (clean, 4));
                expect (metal > 0.1, "metal at full gain distorts: " + juce::String (metal, 4));

                dsp::Amp gated;
                gated.mode = dsp::Amp::Mode::metal;
                gated.gain = 10;
                gated.gateDb = -50;
                gated.prepare (rate);
                std::vector<float> hiss (24000);
                juce::Random random (3);

                for (auto& h : hiss)
                    h = (random.nextFloat() - 0.5f) * 0.002f;   // about -60 dB of hiss

                float* ch[] = { hiss.data() };
                gated.process (ch, 1, 24000);
                float peak = 0;

                for (int i = 12000; i < 24000; ++i)
                    peak = juce::jmax (peak, std::abs (hiss[(size_t) i]));

                expect (peak < dsp::dbToGain (-60.0f), "the gate shuts the hiss out: " + juce::String (dsp::gainToDb (peak), 1) + " dB");
            }

            beginTest ("The amp shows its choices by name, and Gate says Off at the bottom");
            {
                ensureSongIsOpen (session, songFile);

                if (! session.hasSong())
                    return;

                session.addAudioTrack();
                const auto t = session.getNumAudioTracks() - 1;
                const auto choices = session.getAvailableEffects();

                for (const auto& c : choices)
                    if (juce::String (c.pluginType) == AmpPlugin::xmlTypeName)
                        expect (session.addEffect (t, c, false), session.getLastError());

                AmpPlugin* amp = nullptr;

                for (auto* plugin : te::getAudioTracks (*session.getEdit())[t]->pluginList)
                    if (auto* found = dynamic_cast<AmpPlugin*> (plugin))
                        amp = found;
                expect (amp != nullptr);

                if (amp != nullptr)
                {
                    expectEquals (amp->getControl (AmpPlugin::mode)->getCurrentValueAsString(), juce::String ("Crunch"));
                    expectEquals (amp->getControl (AmpPlugin::gate)->getCurrentValueAsString(), juce::String ("Off"));
                    expectEquals (amp->getControl (AmpPlugin::cab)->getCurrentValueAsString(), juce::String ("4x12 closed"));
                    expectEquals (Effects::categoryOf (AmpPlugin::xmlTypeName), juce::String ("Guitar & bass"));
                }

                beginTest ("A tuner goes first on the track, once, in one undo step");
                expect (session.getTuner (t) == nullptr);
                auto* tuner = session.ensureTuner (t);
                expect (tuner != nullptr, session.getLastError());
                expect (session.ensureTuner (t) == tuner, "a second ask finds the same one");
                expectEquals (session.getEffectNames (t)[0], juce::String ("Tuner"), "before the amp");
                expectEquals (session.getEffectNames (t).size(), 2);
                session.undo();
                expect (session.getTuner (t) == nullptr, "one undo removes it");
                expectEquals (session.getEffectNames (t).size(), 1, "and leaves the amp");

                const auto bus = session.addBusTrack();

                if (bus >= 0)
                {
                    expect (session.ensureTuner (bus) == nullptr, "no tuner on a bus");
                    session.deleteTrack (bus);
                }

                session.deleteTrack (t);
            }
        }

        void runRecordingTests (Session& session, const juce::File& songFile)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong())
                return;

            beginTest ("Punch needs a loop region, and switches on and off");
            session.setLoopRange (te::TimePosition(), te::TimePosition());
            expect (! session.setPunch (true), "no loop region yet");
            expect (session.getLastError().contains ("loop"), session.getLastError());
            session.setLoopRange (te::TimePosition::fromSeconds (2.0), te::TimePosition::fromSeconds (4.0));
            expect (session.setPunch (true), session.getLastError());
            expect (session.isPunchOn());
            expect (session.setPunch (false));
            expect (! session.isPunchOn());

            beginTest ("Takes: a clip with only one take says how to make more");
            expect (! session.stepSelectedTakes (1), "nothing selected with takes");
            expect (session.getLastError().contains ("loop"), session.getLastError());

            beginTest ("The input level check gives the right advice");
            expectEquals (RecordingSetup::describeLevel (-12.0f, false), juce::String ("Good"));
            expect (RecordingSetup::describeLevel (-3.0f, false).startsWith ("Loud"));
            expect (RecordingSetup::describeLevel (-40.0f, false).startsWith ("Too quiet"));
            expect (RecordingSetup::describeLevel (-12.0f, true).startsWith ("Clipping"), "a recent clip wins");
            expect (RecordingSetup::describeLevel (-90.0f, false).startsWith ("Nothing"));
        }

        void runMidiLearnTests (Session& session, const juce::File& songFile)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong())
                return;

            auto& learn = session.getMidiLearn();
            learn.clearMappings();

            beginTest ("MIDI learn: touch a control, move a knob, and the knob moves it");
            const auto t = session.getNumAudioTracks() - 1;
            learn.startLearning();
            expect (learn.isLearning());
            learn.handleForTest (juce::MidiMessage::controllerEvent (1, 21, 64));
            expect (learn.getMappings().isEmpty(), "nothing linked before a control is touched");
            session.setTrackVolumeDb (t, -6.0f);   // touching the fader
            learn.handleForTest (juce::MidiMessage::controllerEvent (1, 21, 64));
            expectEquals (learn.getMappings().size(), 1, learn.getLearnStatus());
            expect (! learn.isLearning(), "learning stops once linked");

            if (learn.getMappings().size() == 1)
            {
                const auto m = learn.getMappings()[0];
                expectEquals (m.number, 21);
                expectEquals (m.channel, 1);
                expect (! m.isNote);
                expect (m.target.containsIgnoreCase ("vol"), m.target);
            }

            learn.handleForTest (juce::MidiMessage::controllerEvent (1, 21, 0));
            expect (session.getTrackVolumeDb (t) < -60.0f, "knob down: fader down, " + juce::String (session.getTrackVolumeDb (t)));
            learn.handleForTest (juce::MidiMessage::controllerEvent (1, 21, 127));
            expect (session.getTrackVolumeDb (t) > 0.0f, "knob up: fader up, " + juce::String (session.getTrackVolumeDb (t)));
            learn.handleForTest (juce::MidiMessage::controllerEvent (2, 21, 0));
            expect (session.getTrackVolumeDb (t) > 0.0f, "another channel's knob 21 isn't linked");

            beginTest ("MIDI learn: a pad runs an action once per press");
            juce::StringArray done;
            auto oldAction = learn.performAction;
            learn.performAction = [&done] (const juce::String& a) { done.add (a); };
            learn.startLearningAction ("Play / stop");
            learn.handleForTest (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100));
            expectEquals (learn.getMappings().size(), 2);
            learn.handleForTest (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100));
            learn.handleForTest (juce::MidiMessage::noteOff (10, 36));
            expectEquals (done.size(), 1);
            expectEquals (done[0], juce::String ("Play / stop"));

            learn.startLearningAction ("Record");
            learn.handleForTest (juce::MidiMessage::controllerEvent (1, 80, 127));
            learn.handleForTest (juce::MidiMessage::controllerEvent (1, 80, 127));
            learn.handleForTest (juce::MidiMessage::controllerEvent (1, 80, 0));
            learn.handleForTest (juce::MidiMessage::controllerEvent (1, 80, 127));
            expectEquals (done.size(), 3, "a button sent as a controller: once per press (127, 127, 0, 127 = 2 presses)");
            learn.performAction = oldAction;

            beginTest ("MIDI learn: a new link on the same knob replaces the old one; links can be removed");
            learn.startLearning();
            session.setTrackPan (t, 0.3f);
            learn.handleForTest (juce::MidiMessage::controllerEvent (1, 21, 64));
            expectEquals (learn.getMappings().size(), 3, "knob 21 moved from volume to pan");
            int knob21 = 0;

            for (const auto& m : learn.getMappings())
                knob21 += (m.number == 21 && ! m.isNote) ? 1 : 0;

            expectEquals (knob21, 1);
            expect (learn.removeMapping (0));
            expectEquals (learn.getMappings().size(), 2);
            learn.clearMappings();
            expect (learn.getMappings().isEmpty());
            expect (MidiLearn::getActionNames().contains ("Record"));
        }

        void runAssistantTests (Session& session, const juce::File& songFile)
        {
            using Fix = Session::MasterAdvice::Fix;

            beginTest ("Master assistant: one fix at a time, in the right order");
            Session::MasterState m;
            expect (Session::adviseMaster (m).fix == Fix::none && Session::adviseMaster (m).title.startsWith ("Listening"), "nothing heard yet");

            m.measured = true;
            m.integratedLufs = -20.0f;
            m.truePeakDb = 1.4f;
            const auto clip = Session::adviseMaster (m);
            expect (clip.fix == Fix::lowerFader, clip.title);
            expectEquals (clip.amount, 3.0f, "peak +1.4: down 3 dB (to -1.6)");

            m.truePeakDb = -3.0f;
            expect (Session::adviseMaster (m).fix == Fix::chainOn);
            m.chainOn = true;
            m.rangeLu = 16.0f;
            expect (Session::adviseMaster (m).fix == Fix::moreGlue);
            m.glueOn = true;
            m.glueAmount = 60.0f;
            expect (Session::adviseMaster (m).fix == Fix::matchTarget, Session::adviseMaster (m).title);
            m.clubTarget = true;
            expect (Session::adviseMaster (m).fix == Fix::monoBass, "the club wants mono bass first");
            m.monoBassOn = true;
            m.integratedLufs = -14.3f;
            m.referenceMeasured = true;
            m.referenceLufs = -9.0f;
            expect (Session::adviseMaster (m).fix == Fix::matchReference);
            m.referenceMatching = true;
            const auto done = Session::adviseMaster (m);
            expect (done.fix == Fix::none && done.title == "On target", done.title);
            expect (done.button.isEmpty());

            beginTest ("Master assistant: applying a fix is one undo step");
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong())
                return;

            const auto before = session.getMasterVolumeDb();
            Session::MasterAdvice lower;
            lower.fix = Fix::lowerFader;
            lower.amount = 3.0f;
            expect (session.applyMasterAdvice (lower), session.getLastError());
            expectWithinAbsoluteError (session.getMasterVolumeDb(), before - 3.0f, 0.05f);
            session.undo();
            expectWithinAbsoluteError (session.getMasterVolumeDb(), before, 0.05f);

            const bool wasOn = session.isMasterChainOn();
            Session::MasterAdvice chain;
            chain.fix = Fix::chainOn;
            expect (session.applyMasterAdvice (chain));
            expect (session.isMasterChainOn());
            session.undo();
            expect (session.isMasterChainOn() == wasOn, "undo puts the chain back");
        }

        void runExportPresetTests (Session& session)
        {
            beginTest ("Export presets of your own: saved, read back, deleted");
            Session::ExportSettings s;
            s.format = Session::ExportFormat::flac;
            s.bitDepth = 16;
            s.sampleRate = 44100.0;
            s.targetLufs = -9.0f;
            s.ceilingDb = -0.3f;
            expect (session.saveExportPreset ("Test Export Preset", s), session.getLastError());
            bool found = false;

            for (const auto& p : Session::getUserExportPresets())
            {
                if (p.name != "Test Export Preset")
                    continue;

                found = true;
                expect (p.settings.format == Session::ExportFormat::flac);
                expectEquals (p.settings.bitDepth, 16);
                expectEquals (p.settings.sampleRate, 44100.0);
                expectWithinAbsoluteError (p.settings.targetLufs, -9.0f, 0.01f);
                expect (p.description.startsWith ("FLAC 16-bit 44.1 kHz"), p.description);
            }

            expect (found);
            expect (session.deleteExportPreset ("Test Export Preset"));

            for (const auto& p : Session::getUserExportPresets())
                expect (p.name != "Test Export Preset", "deleted");

            expect (! session.saveExportPreset ("   ", s), "a name is needed");
        }

        void runAudioEditTests (Session& session, const juce::File& songFile, const juce::File& tempFolder)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong())
                return;

            beginTest ("Audio clips: fades, gain, normalise, reverse, transpose and tempo, each one undo step");
            const auto file = tempFolder.getChildFile ("Edit Me 2 bars.wav");
            expect (writeTestWav (file, 4.0));
            session.addAudioTrack();
            const auto t = session.getNumAudioTracks() - 1;
            expect (session.importAudioFileAt (file, t, te::TimePosition()), session.getLastError());
            auto* track = te::getAudioTracks (*session.getEdit())[t];
            auto* clip = track != nullptr && ! track->getClips().isEmpty() ? track->getClips().getFirst() : nullptr;
            expect (clip != nullptr);

            if (clip == nullptr)
                return;

            expect (session.setClipFades (*clip, 0.5, 0.25), session.getLastError());
            expectWithinAbsoluteError (session.getClipFadeIn (*clip), 0.5, 0.001);
            expectWithinAbsoluteError (session.getClipFadeOut (*clip), 0.25, 0.001);
            expect (session.setClipFades (*clip, 10.0, 10.0));
            expect (session.getClipFadeIn (*clip) + session.getClipFadeOut (*clip) <= clip->getPosition().getLength().inSeconds() + 0.001,
                    "the fades fit in the clip together");
            session.undo();
            expectWithinAbsoluteError (session.getClipFadeIn (*clip), 0.5, 0.001, "one undo: the fades before");

            expect (session.normaliseClip (*clip), session.getLastError());
            expectWithinAbsoluteError (session.getClipGainDb (*clip), 11.04f, 0.1f, "0.25 peak (-12 dB) to -1 dB");
            session.undo();
            expectWithinAbsoluteError (session.getClipGainDb (*clip), 0.0f, 0.01f);
            expect (session.setClipGainDb (*clip, -6.0f));
            expectWithinAbsoluteError (session.getClipGainDb (*clip), -6.0f, 0.01f);

            expect (session.setClipReversed (*clip, true));
            expect (session.isClipReversed (*clip));
            session.undo();
            expect (! session.isClipReversed (*clip));

            expect (session.setClipTranspose (*clip, 3.0f));
            expectWithinAbsoluteError (session.getClipTranspose (*clip), 3.0f, 0.001f);
            expect (session.setClipTranspose (*clip, 40.0f));
            expectWithinAbsoluteError (session.getClipTranspose (*clip), 24.0f, 0.001f, "two octaves at most");

            expect (session.setClipFollowsTempo (*clip, true), session.getLastError());
            expect (session.isClipFollowingTempo (*clip));
            expect (session.setClipFollowsTempo (*clip, false));

            beginTest ("Several selected clips: one edit, one undo step");
            session.selectClip (clip);
            expect (session.editSelectedAudioClips ([&session] (te::Clip& c) { return session.setClipGainDb (c, 3.0f) && session.setClipReversed (c, true); }));
            expectWithinAbsoluteError (session.getClipGainDb (*clip), 3.0f, 0.01f);
            session.undo();
            expectWithinAbsoluteError (session.getClipGainDb (*clip), -6.0f, 0.01f, "both changes undone together");
            expect (! session.isClipReversed (*clip));

            beginTest ("A loop's own tempo: from its name, or its length in whole bars");
            expectWithinAbsoluteError (Session::guessLoopBpm (4.0, 120.0, "Drum Loop 90bpm"), 90.0, 0.01);
            expectWithinAbsoluteError (Session::guessLoopBpm (4.0, 120.0, "loop_174_BPM_dnb"), 174.0, 0.01);
            expectWithinAbsoluteError (Session::guessLoopBpm (4.0, 120.0, "Groove"), 120.0, 0.01, "2 bars in 4 s");
            expectWithinAbsoluteError (Session::guessLoopBpm (8.0, 85.0, "Groove"), 120.0, 0.01, "4 bars in 8 s (2 bars would be 60)");

            beginTest ("Ducking: a track ducks under another, and stops");
            session.addAudioTrack();
            const auto trigger = session.getNumAudioTracks() - 1;
            expectEquals (session.getDuckingTrigger (t), -1);
            expect (session.duckUnder (t, trigger), session.getLastError());
            expectEquals (session.getDuckingTrigger (t), trigger);
            expect (session.getEffectNames (t).size() >= 1);
            expect (session.duckUnder (t, trigger), "again: retunes the same compressor");
            int compressors = 0;

            for (auto* p : te::getAudioTracks (*session.getEdit())[t]->pluginList)
                compressors += dynamic_cast<te::CompressorPlugin*> (p) != nullptr ? 1 : 0;

            expectEquals (compressors, 1, "not a second one");
            expect (! session.duckUnder (t, t), "not under itself");
            expect (! session.canDuckUnder (trigger, t), "the other way round would be a loop");
            expect (! session.duckUnder (trigger, t), "a loop is refused (the audio graph couldn't be built)");
            expectEquals (session.getDuckingTrigger (trigger), -1);
            expect (session.stopDucking (t));
            expectEquals (session.getDuckingTrigger (t), -1);
            expect (! session.stopDucking (t), "nothing to stop");

            beginTest ("Tap tempo follows the taps");
            const auto before = session.getBpm();

            for (int i = 0; i < 4; ++i)
            {
                expect (session.tapTempo());
                juce::Thread::sleep (500);
            }

            expectWithinAbsoluteError (session.getBpm(), 120.0, 12.0, "taps every 0.5 s: about 120 BPM");
            session.setBpm (before);

            session.deleteTrack (trigger);
            session.deleteTrack (t);
        }

        void runPresetTests (Session& session, const juce::File& tempFolder)
        {
            using Kind = Session::PresetKind;

            const auto find = [&session] (Kind kind, const juce::String& wanted)
            {
                for (const auto& p : session.getPresets (kind))
                    if (p.name == wanted)
                        return p;

                return Session::PresetInfo();
            };

            const auto countClips = [&session]
            {
                int n = 0;

                for (auto* t : te::getAudioTracks (*session.getEdit()))
                    n += t->getClips().size();

                return n;
            };

            beginTest ("Presets: built-in chains, track presets and templates are there");
            expect (session.createSong ("Preset Test"), session.getLastError());
            expect (session.getPresets (Kind::effectChain).size() >= 5);
            expect (session.getPresets (Kind::trackPreset).size() >= 10);
            expect (session.getPresets (Kind::songTemplate).size() >= 5);

            beginTest ("Presets: a built-in chain goes on a track in one undo step");
            const auto before = session.getEffectNames (0).size();
            const auto vocal = session.getPresets (Kind::effectChain)[0];
            expect (session.applyEffectChain (0, vocal), session.getLastError());
            const auto added = session.getEffectNames (0).size() - before;
            expect (added >= 3, "the chain's effects: " + juce::String (added));
            session.undo();
            expectEquals (session.getEffectNames (0).size(), before, "one undo takes the whole chain off");
            expect (! session.applyEffectChain (99, vocal), "no such track");

            beginTest ("Presets: your own chain is saved and goes on another track the same");
            expect (session.applyEffectChain (0, vocal));
            session.setEffectControl (0, 0, CutFilterPlugin::lowCut, 123.0f);   // a setting of its own, saved with the chain
            expect (session.saveEffectChain (0, "My Chain"), session.getLastError());
            const auto mine = find (Kind::effectChain, "My Chain");
            expect (mine.file.existsAsFile() && ! mine.isBuiltIn(), "listed with its file");
            expect (session.applyEffectChain (1, mine), session.getLastError());
            expect (session.getEffectNames (1) == session.getEffectNames (0), "the same effects, in order");
            expect (! session.saveEffectChain (2, "Empty"), "a track without effects has nothing to save");

            beginTest ("Presets: a track preset makes a new track with its instrument, effects and level");
            expect (session.setTrackVolumeDb (0, -7.0f));
            expect (session.saveTrackPreset (0, "My Vocal"), session.getLastError());
            const auto tracksBefore = session.getNumAudioTracks();
            expect (session.addTrackFromPreset (find (Kind::trackPreset, "My Vocal")), session.getLastError());
            expectEquals (session.getNumAudioTracks(), tracksBefore + 1);
            expect (session.getEffectNames (tracksBefore) == session.getEffectNames (0), "its effects");
            expectWithinAbsoluteError (session.getTrackVolumeDb (tracksBefore), -7.0f, 0.1f, "its level");
            session.undo();
            expectEquals (session.getNumAudioTracks(), tracksBefore, "one undo removes it");

            expect (session.presetExists (Kind::trackPreset, "My Vocal"), "asks before replacing it");
            expect (! session.presetExists (Kind::trackPreset, "Nobody Saved This"));
            expect (session.addTrackFromPreset (find (Kind::trackPreset, "My Vocal")));
            expect (session.addTrackFromPreset (find (Kind::trackPreset, "My Vocal")));
            expect (session.getTrackName (tracksBefore) != session.getTrackName (tracksBefore + 1),
                    "two from one preset get their own names: " + session.getTrackName (tracksBefore + 1));
            session.undo();
            session.undo();

            const auto firstAdded = session.getNumAudioTracks();

            for (const auto& p : session.getPresets (Kind::trackPreset))
                if (p.isBuiltIn())
                    expect (session.addTrackFromPreset (p), p.name + ": " + session.getLastError());

            bool drumMachine = false;

            for (int i = firstAdded; i < session.getNumAudioTracks(); ++i)
                if (session.getTrackName (i).startsWith ("Drums:"))
                    drumMachine = drumMachine || session.getDrumMachine (i) != nullptr;

            expect (drumMachine, "a drums preset brings its Drum Machine");

            beginTest ("Presets: a song template keeps the setup and leaves out the recordings");
            const auto wav = tempFolder.getChildFile ("preset-source.wav");
            expect (writeTestWav (wav, 1.0));
            expect (session.importAudioFileAt (wav, 0, te::TimePosition()));
            session.setBpm (101.0);
            expect (session.addSection (0.0, 16.0, "Intro"), session.getLastError());
            const auto tracks = session.getNumAudioTracks();
            expect (session.saveSongTemplate ("My Setup"), session.getLastError());
            expect (session.createSongFromTemplate ("From My Setup", find (Kind::songTemplate, "My Setup")), session.getLastError());
            expectEquals (session.getNumAudioTracks(), tracks, "the same tracks");
            expectEquals (session.getBpm(), 101.0);
            expectEquals (countClips(), 0, "no recordings or notes");
            expectEquals ((int) session.getSections().size(), 1, "the sections stay (they're the song's shape, not a recording)");
            expect (session.getSongName().contains ("From My Setup"), session.getSongName());
            expect (session.getEffectNames (1) == session.getEffectNames (0) || session.getEffectNames (1).size() > 0, "effects came along");

            beginTest ("Presets: a built-in template makes its tracks, return, tempo and master");
            const auto hipHop = session.getPresets (Kind::songTemplate)[0];
            expect (session.createSongFromTemplate ("Beat", hipHop), session.getLastError());
            expectEquals (session.getBpm(), 90.0);
            expect (session.getNumAudioTracks() >= 6, juce::String (session.getNumAudioTracks()) + " tracks");
            expect (session.isMasterChainOn(), "the master chain is on");
            expect (session.getDrumMachine (0) != nullptr, "drums first");

            beginTest ("Presets: every built-in template opens, with all its tracks and effects");
            for (const auto& t : session.getPresets (Kind::songTemplate))
            {
                if (! t.isBuiltIn())
                    continue;

                expect (session.createSongFromTemplate ("Genre " + t.name, t), t.name + ": " + session.getLastError());
                expect (session.getNumAudioTracks() >= 2, t.name);

                for (int i = 0; i < session.getNumAudioTracks(); ++i)
                    if (session.getTrackName (i).startsWith ("Guitar:") || session.getTrackName (i).startsWith ("Bass guitar"))
                        expect (session.getEffectNames (i).contains ("Amp"), t.name + ": " + session.getTrackName (i) + " has its amp");

                if (t.name == "Metal")
                {
                    expectEquals (session.getTrackName (2), juce::String ("Guitar: metal rhythm"));
                    expectWithinAbsoluteError (session.getTrackPan (2), -1.0f, 0.01f, "the doubled guitars go left");
                    expectWithinAbsoluteError (session.getTrackPan (3), 1.0f, 0.01f, "and right");
                    expectEquals (session.getTrackName (3), juce::String ("Guitar: metal rhythm 2"));
                }

                if (t.name == "Trap (sliding 808)")
                {
                    te::Plugin* synth = nullptr;

                    for (auto* p : te::getAudioTracks (*session.getEdit())[1]->pluginList)
                        if (p->isSynth())
                            synth = p;

                    expect (synth != nullptr && (int) synth->state.getProperty ("voiceMode", 2) == 1, "the 808 is mono, so it slides");
                }
            }

            beginTest ("Presets: yours can be deleted, built-ins can't; a damaged file says so");
            expect (session.deletePreset (find (Kind::effectChain, "My Chain")));
            expect (find (Kind::effectChain, "My Chain").name.isEmpty(), "gone from the list");
            expect (! session.deletePreset (vocal), "built-in");

            const auto broken = Session::getPresetFolder (Kind::effectChain).getChildFile ("Broken" + Session::getPresetExtension (Kind::effectChain));
            broken.replaceWithText ("this is not xml");
            expect (! session.applyEffectChain (0, find (Kind::effectChain, "Broken")), "a damaged chain is refused");
            expect (session.getLastError().contains ("damaged"), session.getLastError());
            broken.deleteFile();

            // Leave the presets folder as the test found it
            session.deletePreset (find (Kind::trackPreset, "My Vocal"));
            session.deletePreset (find (Kind::songTemplate, "My Setup"));
        }

        void runSampleLibraryTests (AudioEngine& engine, const juce::File& tempFolder)
        {
            beginTest ("Samples & loops: the own folder and added folders are scanned, with lengths and loops");
            engine.getEngine().getPropertyStorage().getPropertiesFile().removeValue ("ezSampleFolders");   // an earlier run's folders
            auto own = SampleLibrary::getOwnFolder();
            expect (own.isDirectory(), "Documents\\EzDAWsIt\\Samples is made");
            expect (writeTestWav (own.getChildFile ("Kicks").getChildFile ("Big Kick.wav"), 0.5));
            expect (writeTestWav (own.getChildFile ("Drum Loop 90bpm.wav"), 2.0));
            own.getChildFile ("notes.txt").replaceWithText ("not a sound");

            const auto pack = tempFolder.getChildFile ("My Pack");
            expect (writeTestWav (pack.getChildFile ("Pad.wav"), 1.0));

            SampleLibrary library (engine.getEngine());
            library.waitForScan();
            expectEquals ((int) library.getEntries().size(), 2, "the two sounds in the own folder (not the text file)");

            const auto find = [&library] (const juce::String& wanted) -> const SampleLibrary::Entry*
            {
                for (const auto& e : library.getEntries())
                    if (e.file.getFileNameWithoutExtension() == wanted)
                        return &e;

                return nullptr;
            };

            if (const auto* kick = find ("Big Kick"))
            {
                expectWithinAbsoluteError (kick->seconds, 0.5, 0.01);
                expect (! kick->isLoop);
                expectEquals (kick->describe(), juce::String (juce::CharPointer_UTF8 ("0.5 s \xc2\xb7 Kicks")));   // the middle dot, spelled out
            }
            else
            {
                expect (false, "the kick in a subfolder is found");
            }

            if (const auto* loop = find ("Drum Loop 90bpm"))
                expect (loop->isLoop && loop->describe().startsWith ("Loop") && loop->describe().contains ("2.0 s"), loop->describe());
            else
                expect (false, "the loop is found");

            beginTest ("Samples & loops: folders are remembered; the own folder can't be removed");
            expect (library.addFolder (pack));
            expect (! library.addFolder (pack), "not twice");
            expect (! library.addFolder (tempFolder.getChildFile ("not there")), "not a missing folder");
            library.waitForScan();
            expect (find ("Pad") != nullptr, "the added folder's sound is listed");

            {
                SampleLibrary again (engine.getEngine());
                expect (again.getFolders().contains (pack), "remembered in the app settings");
                again.waitForScan();
            }

            expect (! library.removeFolder (own), "the own folder stays");
            expect (library.removeFolder (pack));
            library.waitForScan();
            expect (find ("Pad") == nullptr, "and its sounds go (the files stay)");
            expect (pack.getChildFile ("Pad.wav").existsAsFile());

            beginTest ("Preview: a sound file can be started and stopped (no device: nothing is heard)");
            expect (engine.startPreview (pack.getChildFile ("Pad.wav")));
            engine.stopPreview();
            expect (! engine.isPreviewing());
            expect (! engine.startPreview (own.getChildFile ("notes.txt")), "not a sound");
        }

        void runMasterTests (Session& session, const juce::File& tempFolder)
        {
            using State = Session::ExportStatus::State;
            using Chain = MasteringChainPlugin;

            beginTest ("Master: every song has a mastering chain, switched off, that can't be undone away");
            expect (session.createSong ("Master Test"), session.getLastError());
            expect (session.getMasteringChain() != nullptr, "the chain is there");
            expect (! session.isMasterChainOn(), "and off: a song sounds the same until someone switches it on");
            session.undo();
            expect (session.getMasteringChain() != nullptr, "undo doesn't take it away");

            beginTest ("Master: on/off and presets are undoable, and a change by hand says (edited)");
            expect (session.setMasterChainOn (true));
            expect (session.isMasterChainOn());
            session.undo();
            expect (! session.isMasterChainOn(), "undo switches it off again");

            const auto& club = Chain::getPresets()[4];
            expect (session.applyMasterPreset (4), session.getLastError());
            expect (session.isMasterChainOn(), "a preset switches the chain on");
            expectWithinAbsoluteError (session.getMasterControl (Chain::gain), club.values[Chain::gain], 0.01f);
            expectEquals (session.getMasterPresetName(), juce::String (club.name));
            expect (session.setMasterControl (Chain::gain, 7.5f));
            expect (session.getMasterPresetName().endsWith ("(edited)"), session.getMasterPresetName());
            session.undo();
            session.undo();
            expect (! session.isMasterChainOn() && session.getMasterControl (Chain::gain) != club.values[Chain::gain],
                    "the preset was one undo step");
            expect (! session.applyMasterPreset (99), "no such preset");

            beginTest ("Master: the loudness target is the song's, and Export starts from it");
            session.setMasterTarget (3);   // Club
            expectWithinAbsoluteError (session.getDefaultExportSettings().targetLufs, -8.0f, 0.01f);
            expectWithinAbsoluteError (session.getDefaultExportSettings().ceilingDb, -0.3f, 0.01f);
            session.setMasterDither (false);
            expect (! session.getDefaultExportSettings().dither);
            expect (! session.matchMasterTarget() && session.getLastError().contains ("Play"), "nothing measured yet: says what to do");

            beginTest ("Master: the chain is in the export, and \"Use the mastering chain\" leaves it out");
            const auto wav = tempFolder.getChildFile ("master-source.wav");
            expect (writeTestWav (wav, 2.0));
            expect (session.importAudioFileAt (wav, 0, te::TimePosition()), session.getLastError());
            session.setMasterVolumeDb (0.0f);

            auto settings = session.getDefaultExportSettings();
            settings.folder = tempFolder.getChildFile ("MasterExports");
            settings.matchLoudness = false;

            const auto exportLoudness = [&] (const Session::ExportSettings& s)
            {
                if (! session.startExport (s))
                {
                    expect (false, "export: " + session.getLastError());
                    return te::LoudnessMeter::Readings();
                }

                const auto& status = waitForExport (session);
                expect (status.state == State::done && status.loudnessValid, status.message);
                te::LoudnessMeter::Readings r;
                r.integratedLufs = status.integratedLufs;
                r.truePeakDb = status.truePeakDb;
                session.clearExportStatus();
                return r;
            };

            expect (session.applyMasterPreset (5), session.getLastError());   // just a limiter
            expect (session.setMasterControl (Chain::gain, 18.0f) && session.setMasterControl (Chain::ceiling, -1.0f));
            const auto limited = exportLoudness (settings);
            settings.useMasterChain = false;
            const auto without = exportLoudness (settings);
            expect (session.isMasterChainOn(), "the chain is back on after an export without it");
            expect (limited.integratedLufs > without.integratedLufs + 6.0f,
                    "the limiter's gain is in the export (" + juce::String (limited.integratedLufs) + " vs " + juce::String (without.integratedLufs) + ")");
            expect (limited.truePeakDb <= -0.8f, "and the true peak stays under the ceiling (" + juce::String (limited.truePeakDb) + ")");

            expect (session.setMasterChainOn (false));
            settings.useMasterChain = true;
            const auto off = exportLoudness (settings);
            expectWithinAbsoluteError (off.integratedLufs, without.integratedLufs, 0.3f, "chain off = without the chain");

            beginTest ("Master: a reference is measured and A/B never reaches an export");
            expect (! session.loadReference (tempFolder.getChildFile ("not-there.wav")), "a missing file is refused");
            expect (session.loadReference (wav), session.getLastError());
            expect (session.getReferenceFile() == wav);

            for (int i = 0; i < 400 && ! session.isReferenceMeasured(); ++i)
            {
                pumpMessages (5);
                session.updateMaster();
            }

            expect (session.isReferenceMeasured(), "its loudness is measured");
            expectWithinAbsoluteError (session.getReferenceLufs(), without.integratedLufs, 1.0f, "the same file, the same loudness");
            session.setReferenceListening (true);
            expect (session.isReferenceListening());
            const auto withB = exportLoudness (settings);
            expect (! session.isReferenceListening(), "exporting switches B off");
            expectWithinAbsoluteError (withB.integratedLufs, off.integratedLufs, 0.3f, "the export is the mix, not the reference");

            beginTest ("Master: everything comes back with the song");
            expect (session.applyMasterPreset (2));
            session.setMasterTarget (1);
            expect (session.saveSong(), session.getLastError());
            const auto file = session.getSongFile();
            expect (session.closeSong());
            expect (session.openSong (file), session.getLastError());
            expect (session.isMasterChainOn());
            expectEquals (session.getMasterPresetName(), juce::String (Chain::getPresets()[2].name));
            expectEquals (session.getMasterTarget(), 1);
            expect (session.getReferenceFile() == wav, "the reference file is remembered");
            session.clearReference();
            expect (session.getReferenceFile() == juce::File());
            session.setMasterVolumeDb (-3.0f);
        }

        void runMixTests (Session& session, const juce::File& songFile)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong())
                return;

            auto& edit = *session.getEdit();
            const auto trackAt = [&] (int i) { return te::getAudioTracks (edit)[i]; };
            const auto indexOf = [&] (te::AudioTrack* t) { return te::getAudioTracks (edit).indexOf (t); };
            const auto busOf = [&] (int i) { return session.getTrackOutput (i).fromFirstOccurrenceOf ("bus:", false, false); };
            // What syncBusRouting keeps right: the "track N" text Tracktion stores (and the autosave writes as is)
            const auto storedNumberIsRight = [&] (te::AudioTrack* source)
            {
                auto* dest = source->getOutput().getDestinationTrack();
                const auto text = source->state.getChildWithName (te::IDs::OUTPUTDEVICES).getChildWithName (te::IDs::DEVICE)[te::IDs::name].toString();
                return dest != nullptr && text == "track " + juce::String (dest->getAudioTrackNumber());
            };
            const auto busChoice = [&] (int from, int bus) -> juce::String
            {
                for (auto& c : session.getTrackOutputChoices (from))
                    if (c.id == "bus:" + trackAt (bus)->itemID.toString())
                        return c.id;

                return {};
            };

            while (session.getNumAudioTracks() < 2)
                session.addAudioTrack();

            beginTest ("An FX return is a track at the end, its reverb 100% wet, solo-safe, with a bus of its own");
            const auto before = session.getNumAudioTracks();
            const auto returnsBefore = session.getReturns().size();
            const auto r1 = session.addReturnTrack (Session::ReturnEffect::reverb);
            expectEquals (r1, before, session.getLastError());

            if (r1 < 0 || session.getReturns().empty())
                return;   // setup failed: the rest would crash, not fail

            expect (session.getTrackKind (r1) == Session::TrackKind::fxReturn);
            expectEquals (session.getReturns().size(), returnsBefore + 1);
            expectEquals (session.getEffectInfo (r1).size(), 1, "the reverb is its one effect (the return plugin isn't an insert)");
            expect (trackAt (r1)->isSoloIsolate (false), "soloing a track keeps its reverb");
            auto* reverb = trackAt (r1)->pluginList.getPluginsOfType<te::ReverbPlugin>().getFirst();
            expect (reverb != nullptr && reverb->dryValue.get() == 0.0f, "100% wet");
            const auto bus1 = session.getReturns().back().bus;
            expect (! session.canSend (r1), "returns have no sends (no feedback)");
            expect (! session.setSendLevel (r1, bus1, 0.0f));

            const auto r2 = session.addReturnTrack (Session::ReturnEffect::delay);

            if (r2 < 0)
            {
                expect (false, "setup: " + session.getLastError());
                return;
            }

            expect (r2 == r1 + 1 && session.getTrackName (r2) == "Delay");
            const auto bus2 = session.getReturns().back().bus;
            expect (bus2 != bus1, "each return has its own bus");
            const auto r3 = session.addReturnTrack (Session::ReturnEffect::reverb);
            expectEquals (session.getTrackName (r3), juce::String ("Reverb 2"), "names don't clash");
            session.undo();
            expectEquals (session.getNumAudioTracks(), r3, "one undo removes a return");
            session.redo();
            expectEquals (session.getNumAudioTracks(), r3 + 1);
            session.undo();

            beginTest ("Sends: made when first turned up, post-fader, never shown as an insert; a drag is one undo step");
            const auto fxBefore = session.getEffectInfo (0).size();
            expect (! session.getSend (0, bus1).exists);
            expect (session.setSendLevel (0, bus1, -6.0f), session.getLastError());
            auto send = session.getSend (0, bus1);
            expect (send.exists && send.enabled && ! send.preFader);
            expectWithinAbsoluteError (send.levelDb, -6.0f, 0.05f);
            expectEquals (session.getEffectInfo (0).size(), fxBefore, "a send isn't an insert");
            expect (session.setSendLevel (0, bus1, -3.0f));
            expect (session.setSendLevel (0, bus1, -2.0f, true));
            expect (session.setSendLevel (0, bus1, -1.0f, true));
            expectWithinAbsoluteError (session.getSend (0, bus1).levelDb, -1.0f, 0.05f);
            session.undo();
            expectWithinAbsoluteError (session.getSend (0, bus1).levelDb, -6.0f, 0.05f, "one undo takes back the whole drag");
            expect (! session.setSendLevel (0, 999, 0.0f), "no return on that bus");

            beginTest ("Pre/post moves the send around the fader; new effects still go before it; off keeps the level");
            expect (session.setSendPreFader (0, bus1, true), session.getLastError());
            expect (session.getSend (0, bus1).preFader);
            const auto eq = session.getAvailableEffects()[0];
            expect (session.addEffect (0, eq, false), session.getLastError());
            {
                auto* track = trackAt (0);
                auto* sendPlugin = track->pluginList.getPluginsOfType<te::AuxSendPlugin>().getFirst();
                auto* eqPlugin = track->pluginList.getPluginsOfType<te::EqualiserPlugin>().getLast();
                expect (sendPlugin != nullptr && eqPlugin != nullptr
                         && track->pluginList.indexOf (eqPlugin) < track->pluginList.indexOf (sendPlugin), "effects go before pre-fader sends");
            }
            expect (session.getSend (0, bus1).preFader, "and the send stays pre-fader");
            session.undo();   // the EQ
            expect (session.setSendPreFader (0, bus1, false));
            expect (! session.getSend (0, bus1).preFader);
            session.undo();
            expect (session.getSend (0, bus1).preFader, "pre/post is undoable");
            expect (session.setSendEnabled (0, bus1, false));
            send = session.getSend (0, bus1);
            expect (send.exists && ! send.enabled);
            expectWithinAbsoluteError (send.levelDb, -6.0f, 0.05f, "off keeps the level");
            expect (session.setSendEnabled (0, bus1, true));
            expect (session.setSendLevel (0, bus1, Session::minVolumeDb));
            expectEquals (session.getSend (0, bus1).levelDb, Session::minVolumeDb, "the bottom is -inf");
            session.undo();
            expect (session.removeSend (0, bus1));
            expect (! session.getSend (0, bus1).exists);
            session.undo();
            expect (session.getSend (0, bus1).exists, "removing is undoable");
            expect (session.setSendPreFader (1, bus2, true), "choosing pre/post first makes a silent send");
            send = session.getSend (1, bus2);
            expect (send.exists && send.preFader && send.levelDb == Session::minVolumeDb);

            beginTest ("Deleting a return takes its sends with it; one undo brings both back");
            expect (session.setSendLevel (0, bus2, -10.0f));
            expect (session.deleteTrack (r2));
            expect (! session.getSend (0, bus2).exists && ! session.getSend (1, bus2).exists);
            session.undo();
            expect (session.getTrackKind (r2) == Session::TrackKind::fxReturn);
            expect (session.getSend (0, bus2).exists && session.getSend (1, bus2).exists);

            beginTest ("A duplicated return gets its own bus; renaming a return renames its sends");
            expect (session.duplicateTrack (r1), session.getLastError());
            {
                const auto returns = session.getReturns();
                juce::Array<int> buses;

                for (auto& r : returns)
                    buses.addIfNotAlreadyThere (r.bus);

                expectEquals (buses.size(), (int) returns.size(), "no two returns share a bus");
            }
            session.undo();
            const auto oldName = session.getTrackName (r1);
            expect (session.renameTrack (r1, "Plate"));
            expectEquals (edit.getAuxBusName (bus1), juce::String ("Plate"));
            session.undo();
            expectEquals (edit.getAuxBusName (bus1), oldName, "undo puts the send name back too");

            beginTest ("Tracks play into a bus, kept by ID: moving, deleting, undo and reopening never lose it");
            const auto b1 = session.addBusTrack();
            expect (b1 >= 0 && session.getTrackKind (b1) == Session::TrackKind::bus, session.getLastError());

            if (b1 < 0)
                return;

            expectEquals (session.getTrackOutputChoices (r1).size(), session.getTrackOutputChoices().size(), "returns are offered no buses");
            expect (! session.setTrackInput (b1, session.getTrackInputChoices().getLast().id) || session.getTrackInputChoices().size() < 2,
                    "buses take no input");
            expect (! session.setTrackArmed (b1, true), "nor record");
            auto* bus1Track = trackAt (b1);
            auto* source = trackAt (0);
            expect (busChoice (b1, b1).isEmpty(), "a bus can't play into itself");
            expect (session.setTrackOutput (0, busChoice (0, b1)), session.getLastError());
            expect (source->getOutput().getDestinationTrack() == bus1Track);
            expectEquals (session.getTracksIntoBus (b1).size(), 1);
            expect (session.moveTrack (0, 1));
            expect (source->getOutput().getDestinationTrack() == bus1Track && busOf (1) == bus1Track->itemID.toString(), "moved: still into the bus");
            session.undo();
            expect (source->getOutput().getDestinationTrack() == bus1Track && indexOf (source) == 0, "undo: still into the bus");
            session.redo();
            expect (source->getOutput().getDestinationTrack() == bus1Track);
            session.undo();

            // The bus itself moves across tracks: its number changes
            expect (session.moveTrack (indexOf (bus1Track), 0));
            expect (storedNumberIsRight (source), "the stored number follows the bus");
            expect (session.setTrackOutput (indexOf (source), Session::mainMixID));
            session.undo();   // the stored "track N" from before the move comes back: it must be put right
            expect (source->getOutput().getDestinationTrack() == bus1Track && storedNumberIsRight (source), "undo after the bus moved: into the right track");
            session.undo();   // the move
            expect (storedNumberIsRight (source) && indexOf (bus1Track) == b1);
            session.addAudioTrack();
            expect (session.moveTrack (session.getNumAudioTracks() - 1, 0));   // a new track ahead of everything
            expect (source->getOutput().getDestinationTrack() == bus1Track, "a track added ahead: still into the bus");
            expect (storedNumberIsRight (source));
            expect (session.deleteTrack (0));
            expect (source->getOutput().getDestinationTrack() == bus1Track && storedNumberIsRight (source), "a track deleted ahead: still into the bus");
            session.undo();
            expect (storedNumberIsRight (source));
            session.undo();
            session.undo();
            expect (source->getOutput().getDestinationTrack() == bus1Track && indexOf (source) == 0 && storedNumberIsRight (source));

            beginTest ("Buses can chain, never in a circle; returns play into the main mix only");
            const auto b2 = session.addBusTrack ("Test group");
            expectEquals (session.getTrackName (b2), juce::String ("Test group"));
            expect (session.setTrackOutput (b1, busChoice (b1, b2)), session.getLastError());
            expect (busChoice (b2, b1).isEmpty(), "the group already gets the bus: the bus can't be offered to it");
            expect (! session.setTrackOutput (b2, "bus:" + bus1Track->itemID.toString()), "a circle is refused");
            expect (! session.setTrackOutput (r1, "bus:" + bus1Track->itemID.toString()), "a return can't play into a bus");
            expect (session.routeTracksToNewBus ({ r1 }) < 0, "nor be put on a new one");

            beginTest ("Deleting a bus sends its tracks where the bus went; undo puts it back");
            auto* bus2Track = trackAt (b2);
            expect (session.deleteTrack (b1));
            expect (source->getOutput().getDestinationTrack() == bus2Track, "into the group, where the bus went");
            session.undo();
            bus1Track = trackAt (b1);
            expect (source->getOutput().getDestinationTrack() == bus1Track && bus1Track->getOutput().getDestinationTrack() == bus2Track);

            beginTest ("Routing and sends survive saving and reopening");
            const auto sourceID = source->itemID, bus1ID = bus1Track->itemID;
            expect (session.saveSong() && session.closeSong() && session.openSong (songFile), session.getLastError());
            {
                auto& reopened = *session.getEdit();
                auto* src = te::findAudioTrackForID (reopened, sourceID);
                auto* bus = te::findAudioTrackForID (reopened, bus1ID);
                expect (src != nullptr && bus != nullptr && src->getOutput().getDestinationTrack() == bus);
                expect (session.getTrackKind (te::getAudioTracks (reopened).indexOf (bus)) == Session::TrackKind::bus);
                expect (session.getSend (0, bus1).exists && session.getSend (0, bus1).preFader);
                expect (session.getReturns().size() >= 2);
            }

            beginTest ("A new bus from tracks: one undo step");
            {
                auto& e = *session.getEdit();
                const auto n = session.getNumAudioTracks();
                const auto nb = session.routeTracksToNewBus ({ 1 });
                expect (nb == n && te::getAudioTracks (e)[1]->getOutput().getDestinationTrack() == te::getAudioTracks (e)[nb], session.getLastError());
                session.undo();
                expectEquals (session.getNumAudioTracks(), n);
            }

            runMixSoundTests (session);
        }

        /** Rendered for real: sound goes through sends (pre/post/off) and buses. */
        void runMixSoundTests (Session& session)
        {
            beginTest ("Sound really goes through sends and buses");
            const auto choices = session.getAvailableInstruments();

            if (! session.addInstrumentTrack (choices[0], false))
            {
                expect (false, "setup: no synth: " + session.getLastError());
                return;
            }

            auto& edit = *session.getEdit();
            const auto synthIndex = session.getNumAudioTracks() - 1;
            auto* synth = te::getAudioTracks (edit)[synthIndex];
            const auto ret = session.addReturnTrack (Session::ReturnEffect::none, "Test return");
            auto* returnTrack = te::getAudioTracks (edit)[ret];

            if (ret < 0 || returnTrack == nullptr || session.getReturns().empty())
            {
                expect (false, "setup: no return: " + session.getLastError());
                return;
            }

            const auto bus = session.getReturns().back().bus;

            for (auto* t : te::getAudioTracks (edit))   // only the synth and its return can be heard
            {
                t->setMute (t != synth && t != returnTrack);
                t->setSolo (false);   // an earlier test may have left one soloed
            }

            auto clip = synth->insertMIDIClip (te::TimeRange (te::TimePosition(), te::TimePosition::fromSeconds (1.0)), nullptr);

            if (clip == nullptr)
            {
                expect (false, "setup: no clip");
                return;
            }

            clip->getSequence().addNote (60, te::BeatPosition(), te::BeatDuration::fromBeats (2.0), 100, 0, nullptr);
            const auto dry = renderPeak (edit, 1.0);
            expect (dry > 0.01f, "the synth plays (" + juce::String (dry) + ")");

            session.setTrackVolumeDb (synthIndex, Session::minVolumeDb);   // the fader right down: -60 dB
            const auto faderDown = renderPeak (edit, 1.0);
            expect (faderDown < dry * 0.01f, "fader down: almost nothing");

            expect (session.setSendLevel (synthIndex, bus, 0.0f));
            expect (renderPeak (edit, 1.0) < dry * 0.01f, "a post-fader send follows the fader");
            session.setTrackVolumeDb (synthIndex, 0.0f);
            const auto postUp = renderPeak (edit, 1.0);
            expect (postUp > dry * 1.3f, "with the fader up, a post-fader send adds the return to the dry sound (" + juce::String (postUp) + ")");
            session.setTrackVolumeDb (synthIndex, Session::minVolumeDb);
            expect (session.setSendPreFader (synthIndex, bus, true));
            const auto pre = renderPeak (edit, 1.0);
            expect (pre > dry * 0.3f, "a pre-fader send plays through the return with the fader down (" + juce::String (pre) + ")");
            expect (session.setSendEnabled (synthIndex, bus, false));
            expect (renderPeak (edit, 1.0) < dry * 0.01f, "a send switched off is silent");
            expect (session.setSendEnabled (synthIndex, bus, true));
            returnTrack->setMute (true);
            expect (renderPeak (edit, 1.0) < dry * 0.01f, "muting the return silences what's sent to it");
            returnTrack->setMute (false);
            expect (session.removeSend (synthIndex, bus));

            // Into a bus: the bus's fader now controls the synth
            session.setTrackVolumeDb (synthIndex, 0.0f);
            const auto b = session.routeTracksToNewBus ({ synthIndex });
            auto* busTrack = te::getAudioTracks (edit)[b];

            if (b < 0 || busTrack == nullptr)
            {
                expect (false, "setup: no bus: " + session.getLastError());
                return;
            }

            busTrack->setMute (false);
            const auto viaBus = renderPeak (edit, 1.0);
            expect (viaBus > dry * 0.5f, "the synth plays through the bus (" + juce::String (viaBus) + ")");
            session.setTrackVolumeDb (b, Session::minVolumeDb);
            expect (renderPeak (edit, 1.0) < dry * 0.01f, "the bus fader turns it down");
            session.setTrackVolumeDb (b, 0.0f);
            busTrack->setMute (true);
            expect (renderPeak (edit, 1.0) < dry * 0.01f, "muting the bus silences what plays into it");
            clip = nullptr;   // release before the song closes
        }

        MasterTapPlugin* findTap (Session& session, int* count = nullptr)
        {
            MasterTapPlugin* tap = nullptr;
            int n = 0;

            if (auto* edit = session.getEdit())
                for (auto* plugin : edit->getMasterPluginList())
                    if (auto* t = dynamic_cast<MasterTapPlugin*> (plugin))
                    {
                        tap = t;
                        ++n;
                    }

            if (count != nullptr)
                *count = n;

            return tap;
        }

        /** Level meters: the main meter in every song, peaks, loudness, clip lights. */
        void runMeterTests (Session& session, const juce::File& songFile)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong())
                return;

            beginTest ("Every song has one main meter, between the mastering chain and the reference, that undo can't remove");
            int count = 0;
            auto* tap = findTap (session, &count);
            expectEquals (count, 1);

            // The meter measures the mix after the chain, and keeps measuring it while the reference plays
            {
                // Scoped: these hold references, which must be let go before the song closes
                const auto masters = session.getEdit()->getMasterPluginList().getPlugins();
                expectEquals (masters.size(), 3);
                expect (masters.size() == 3 && dynamic_cast<MasteringChainPlugin*> (masters[0].get()) != nullptr
                         && masters[1].get() == tap && dynamic_cast<ReferencePlugin*> (masters[2].get()) != nullptr,
                        "chain, meter, reference");
            }

            expect (session.saveSong() && session.closeSong() && session.openSong (songFile), session.getLastError());
            findTap (session, &count);
            expectEquals (count, 1, "reopening doesn't add a second one");
            tap = findTap (session);

            if (tap == nullptr)
                return;

            beginTest ("The main meter measures peak and loudness");
            tap->baseClassInitialise ({ te::TimePosition(), 44100.0, 512 });
            juce::AudioBuffer<float> buffer (2, 512);
            double phase = 0.0;

            for (int block = 0; block < 4 * 44100 / 512; ++block)   // 4 seconds of a 1 kHz sine at half scale
            {
                for (int i = 0; i < 512; ++i)
                {
                    const auto v = 0.5f * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * 1000.0 / 44100.0;
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }

                tap->applyToBuffer (te::PluginRenderContext (&buffer, 0, 512, nullptr, 0.0, {}, true, false, false, false));
            }

            expectWithinAbsoluteError (tap->takePeak (0), 0.5f, 0.01f);
            expectWithinAbsoluteError (tap->takePeak (1), 0.5f, 0.01f);
            expectWithinAbsoluteError (tap->takePeak (0), 0.0f, 1.0e-6f, "taking the peak clears it");
            const auto loud = tap->getLoudness();
            expect (loud.shortTermValid && loud.momentaryValid);
            expectWithinAbsoluteError (loud.shortTermLufs, -6.0f, 0.3f, "a half-scale 1 kHz sine in both channels is -6.0 LUFS");
            expectWithinAbsoluteError (buffer.getSample (0, 100), 0.5f * (float) std::sin (phase - juce::MathConstants<double>::twoPi * 1000.0 / 44100.0 * (512 - 100)), 1.0e-4f,
                                       "the audio passes through untouched");

            beginTest ("The meter bank: clip light stays on until reset; tracks come and go safely");
            auto& meters = session.getMeters();
            meters.poll();
            // The meter shows the level after the master volume (a new song starts at -3 dB), so go well over
            const auto masterDb = session.getEdit()->getMasterVolumePlugin()->getVolumeDb();
            buffer.applyGain (4.0f * juce::Decibels::decibelsToGain (juce::jmax (0.0f, -masterDb)));   // peak 2.0 (+6 dB) after the master volume
            tap->applyToBuffer (te::PluginRenderContext (&buffer, 0, 512, nullptr, 0.0, {}, true, false, false, false));
            meters.poll();
            auto main = meters.getMain();
            expect (main.clipped, "a sample over 0 dB lights the clip light");
            expectWithinAbsoluteError (main.maxDb, 6.0f, 0.2f, "the reading includes the master volume");
            meters.poll();
            expect (meters.getMain().clipped, "and it stays on");
            meters.resetMain();
            expect (! meters.getMain().clipped, "until reset");
            tap->baseClassDeinitialise();

            const auto tracksBefore = session.getNumAudioTracks();
            session.addAudioTrack();
            meters.poll();
            expectEquals ((int) meters.getTrack (tracksBefore).levelDb[0], (int) MeterBank::floorDb, "silent until audio plays");
            expect (! meters.getTrack (tracksBefore).clipped, "and no clip light on a silent track");
            expect (! meters.getTrack (0).clipped);
            expect (session.deleteTrack (tracksBefore));
            meters.poll();   // the deleted track's meter is let go
            session.undo();  // and a recreated one is picked up
            meters.poll();
            session.redo();
            meters.poll();
            expectEquals (session.getNumAudioTracks(), tracksBefore);
            beginTest ("Master volume and pan; meters switch between peak and RMS");
            const auto masterBefore = session.getMasterVolumeDb();
            const auto panBefore = session.getMasterPan();
            session.beginUndoStep();
            session.setMasterVolumeDb (-6.0f);
            expectWithinAbsoluteError (session.getMasterVolumeDb(), -6.0f, 0.05f);
            session.setMasterVolumeDb (40.0f);
            expectWithinAbsoluteError (session.getMasterVolumeDb(), Session::maxVolumeDb, 0.05f, "clamped");
            session.setMasterPan (-2.0f);
            expectWithinAbsoluteError (session.getMasterPan(), -1.0f, 1.0e-4f);
            session.undo();
            expectWithinAbsoluteError (session.getMasterVolumeDb(), masterBefore, 0.05f, "one undo step");
            expectWithinAbsoluteError (session.getMasterPan(), panBefore, 1.0e-4f);
            meters.setMode (te::LevelMeasurer::RMSMode);
            meters.poll();

            if (auto* firstMeter = te::getAudioTracks (*session.getEdit())[0]->getLevelMeterPlugin())
                expect (firstMeter->measurer.getMode() == te::LevelMeasurer::RMSMode, "the tracks' measurers follow");

            meters.setMode (te::LevelMeasurer::peakMode);

            expect (session.closeSong());
            meters.poll();   // nothing open: nothing held
            expect (session.openSong (songFile));
            meters.poll();
            meters.resetAll();
        }

        /** Clip and track editing: selection, clipboard, duplicate, split, trim, track commands. */
        void runEditingTests (Session& session, const juce::File& songFile, const juce::File& tempFolder)
        {
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong() || session.getNumAudioTracks() < 2)
                return;

            auto& edit = *session.getEdit();
            session.setBpm (120.0);
            const auto at = [] (double seconds) { return te::TimePosition::fromSeconds (seconds); };
            const auto secs = [] (double seconds) { return te::TimeDuration::fromSeconds (seconds); };
            const auto clipsOn = [&edit] (int track) { return te::getAudioTracks (edit)[track]->getClips().size(); };
            const auto startOf = [] (te::Clip* c) { return c != nullptr ? c->getPosition().getStart().inSeconds() : -1.0; };

            // Work far to the right of anything earlier tests made
            beginTest ("A new MIDI clip appears where asked and is selected");
            const int before0 = clipsOn (0);
            expect (session.createMidiClip (0, at (100.0), secs (2.0)), session.getLastError());
            expectEquals (clipsOn (0), before0 + 1);
            auto* a = session.getSelectedClip();
            expectWithinAbsoluteError (startOf (a), 100.0, 1.0e-6);
            expect (! session.createMidiClip (0, at (100.0), secs (0.0)), "zero-length clips are refused");
            expect (! session.createMidiClip (99, at (100.0), secs (1.0)), "a missing track is refused");

            if (a == nullptr)
                return;

            beginTest ("Shift-select adds to the selection; toggling removes");
            expect (session.createMidiClip (1, at (104.0), secs (1.0)));
            auto* b = session.getSelectedClip();
            session.selectClip (a, true);
            expectEquals (session.getSelectedClips().size(), 2);
            session.toggleClipSelected (*a);
            expectEquals (session.getSelectedClips().size(), 1);
            expect (! session.isClipSelected (*a) && session.isClipSelected (*b));

            beginTest ("Moving several clips moves them together, and undoes in one step");
            session.selectClips ({ a, b });
            expect (session.moveSelectedClips (secs (2.0), 0), session.getLastError());
            expectWithinAbsoluteError (startOf (a), 102.0, 1.0e-6);
            expectWithinAbsoluteError (startOf (b), 106.0, 1.0e-6);
            session.undo();
            expectWithinAbsoluteError (startOf (a), 100.0, 1.0e-6);
            expectWithinAbsoluteError (startOf (b), 104.0, 1.0e-6);
            // Past the last track whatever the track count (earlier tests add tracks)
            expect (! session.moveSelectedClips (secs (0.0), session.getNumAudioTracks()), "moving off the last track is refused");
            expectEquals (clipsOn (0), before0 + 1, "a refused move leaves the clips on their tracks");
            expectWithinAbsoluteError (startOf (b), 104.0, 1.0e-6, "a refused move changes nothing");

            beginTest ("Copy and paste keeps the clips' spacing and tracks");
            expect (session.copySelectedClips());
            expect (session.hasClipsToPaste());
            const int before1 = clipsOn (1);
            expect (session.pasteClips (at (200.0), 0), session.getLastError());
            const auto pasted = session.getSelectedClips();
            expectEquals (pasted.size(), 2);
            expectEquals (clipsOn (1), before1 + 1);

            double earliest = 1.0e9, latest = 0.0;

            for (auto* c : pasted)
            {
                expect (c != a && c != b, "pasted clips are new clips");
                earliest = juce::jmin (earliest, startOf (c));
                latest = juce::jmax (latest, startOf (c));
            }

            expectWithinAbsoluteError (earliest, 200.0, 1.0e-6);
            expectWithinAbsoluteError (latest, 204.0, 1.0e-6);
            session.undo();
            expectEquals (clipsOn (1), before1, "undo removes the paste");

            beginTest ("Cut removes the clips but they can still be pasted");
            session.selectClips ({ b });
            expect (session.cutSelectedClips());
            expectEquals (clipsOn (1), before1 - 1);
            expect (session.pasteClips (at (104.0), 1));
            expectEquals (clipsOn (1), before1);
            b = session.getSelectedClip();

            beginTest ("Duplicate puts a copy straight after the selection");
            session.selectClips ({ a });
            expect (session.duplicateSelectedClips());
            expectWithinAbsoluteError (startOf (session.getSelectedClip()), 102.0, 1.0e-6);
            session.undo();

            beginTest ("Ctrl+drag copy leaves the original where it was");
            expect (session.copyClipTo (*a, at (110.0), 1));
            expectWithinAbsoluteError (startOf (a), 100.0, 1.0e-6);
            expectWithinAbsoluteError (startOf (session.getSelectedClip()), 110.0, 1.0e-6);
            session.undo();

            session.selectClips ({ a, b });
            const int beforeCopy0 = clipsOn (0), beforeCopy1 = clipsOn (1);
            expect (session.copySelectedClipsBy (secs (20.0), 0), session.getLastError());
            expectEquals (clipsOn (0), beforeCopy0 + 1);
            expectEquals (clipsOn (1), beforeCopy1 + 1);
            expectEquals (session.getSelectedClips().size(), 2, "the copies become the selection");
            expectWithinAbsoluteError (startOf (a), 100.0, 1.0e-6);
            session.undo();
            expectEquals (clipsOn (0) + clipsOn (1), beforeCopy0 + beforeCopy1, "one undo removes every copy");

            beginTest ("Split cuts a clip in two at the time");
            session.selectClips ({ a });
            const int beforeSplit = clipsOn (0);
            expect (session.splitClipsAt (at (101.0)), session.getLastError());
            expectEquals (clipsOn (0), beforeSplit + 1);
            expectWithinAbsoluteError (a->getPosition().getEnd().inSeconds(), 101.0, 1.0e-6);
            session.undo();
            session.selectClips ({ a });
            expect (! session.splitClipsAt (at (150.0)), "splitting a selected clip away from it says so");
            expect (! session.splitClipsAt (at (102.0)), "splitting exactly at a clip's end says so (Tracktion can't)");
            session.selectClip (nullptr);
            expect (! session.splitClipsAt (at (150.0)), "with nothing selected, an empty spot says so too");
            expectEquals (clipsOn (0), beforeSplit, "undo joins it back");
            session.selectClips ({ a });
            expect (session.splitClipsAt (at (101.0)));
            session.undo();
            expectEquals (clipsOn (0), beforeSplit, "undo joins it back");
            expectWithinAbsoluteError (a->getPosition().getEnd().inSeconds(), 102.0, 1.0e-6);

            beginTest ("Trimming MIDI moves an edge but keeps notes in place");
            auto* midiA = dynamic_cast<te::MidiClip*> (a);
            expect (midiA != nullptr);

            if (midiA == nullptr)
                return;

            // A note 1.5 s into the clip (120 bpm: 3 beats) must still play at 101.5 s after trimming the start
            midiA->getSequence().addNote (60, te::BeatPosition::fromBeats (3.0), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);
            const auto noteTime = [&]
            {
                auto* note = midiA->getSequence().getNotes().getFirst();
                return note != nullptr ? midiA->getTimeOfContentBeat (note->getStartBeat()).inSeconds() : -1.0;
            };
            expectWithinAbsoluteError (noteTime(), 101.5, 1.0e-3);
            expect (session.trimClipStart (*a, at (100.5)));
            expectWithinAbsoluteError (startOf (a), 100.5, 1.0e-6);
            expectWithinAbsoluteError (a->getPosition().getOffset().inSeconds(), 0.5, 1.0e-6, "trimming the start sets the offset");
            expectWithinAbsoluteError (noteTime(), 101.5, 1.0e-3, "the note still plays at the same time");
            expectWithinAbsoluteError (a->getPosition().getEnd().inSeconds(), 102.0, 1.0e-6);
            expect (session.trimClipEnd (*a, at (101.5)));
            expectWithinAbsoluteError (a->getPosition().getEnd().inSeconds(), 101.5, 1.0e-6);
            expect (session.trimClipEnd (*a, at (50.0)));
            expect (a->getPosition().getLength().inSeconds() > 0.0, "a trim can't make a clip vanish");

            beginTest ("Trimming audio can't reach past the ends of the file");
            const auto wav = tempFolder.getChildFile ("trim.wav");
            expect (writeTestWav (wav, 1.0));
            expect (session.importAudioFileAt (wav, 1, at (120.0)), session.getLastError());
            te::Clip* audio = nullptr;

            for (auto* c : te::getAudioTracks (edit)[1]->getClips())
                if (std::abs (startOf (c) - 120.0) < 1.0e-6)
                    audio = c;

            expect (audio != nullptr, "drag-drop import lands at the time and track asked");

            if (audio != nullptr)
            {
                session.trimClipStart (*audio, at (110.0));
                expectWithinAbsoluteError (startOf (audio), 120.0, 1.0e-3, "can't reveal audio before the file starts");
                session.trimClipEnd (*audio, at (130.0));
                expectWithinAbsoluteError (audio->getPosition().getEnd().inSeconds(), 121.0, 1.0e-3, "can't reach past the file's end");
                session.trimClipStart (*audio, at (120.25));
                expectWithinAbsoluteError (audio->getPosition().getOffset().inSeconds(), 0.25, 1.0e-3, "trimming the start keeps the audio in place");
            }

            beginTest ("Clip mute, rename and colour are undoable");
            expect (session.setClipMuted (*a, true));
            expect (a->isMuted());
            session.undo();
            expect (! a->isMuted());
            expect (session.renameClip (*a, "  Verse  "));
            expectEquals (a->getName(), juce::String ("Verse"));
            expect (! session.renameClip (*a, "   "));
            expect (session.renameClip (*a, "Chorus"));
            session.undo();
            expectEquals (a->getName(), juce::String ("Verse"));
            const auto colourBefore = a->getColour();
            expect (session.setClipColour (*a, juce::Colours::red));
            expect (a->getColour() == juce::Colours::red);
            session.undo();
            expect (a->getColour() == colourBefore, "colour is undoable");

            beginTest ("Select all, then delete them all in one undoable step");
            session.selectAllClips();
            const int total = session.getSelectedClips().size();
            expect (total >= 3);
            expect (session.deleteSelectedClips());
            expectEquals (clipsOn (0) + clipsOn (1), 0);
            session.undo();
            int restored = 0;

            for (auto* t : te::getAudioTracks (edit))
                restored += t->getClips().size();

            expectEquals (restored, total);

            beginTest ("Tracks can be duplicated, moved, recoloured and deleted, all undoably");
            const int numTracks = session.getNumAudioTracks();
            const auto name0 = te::getAudioTracks (edit)[0]->getName();
            expect (session.duplicateTrack (0), session.getLastError());
            expectEquals (session.getNumAudioTracks(), numTracks + 1);
            expectEquals (te::getAudioTracks (edit)[1]->getName(), name0 + " copy");
            expect (session.moveTrack (1, numTracks));   // to the bottom
            expectEquals (te::getAudioTracks (edit)[numTracks]->getName(), name0 + " copy");
            expect (session.moveTrack (numTracks, 0));   // to the top
            expectEquals (te::getAudioTracks (edit)[0]->getName(), name0 + " copy");
            expect (session.setTrackColour (0, juce::Colours::blue));
            expect (session.getTrackColour (0) == juce::Colours::blue);
            expect (session.deleteTrack (0));
            expectEquals (session.getNumAudioTracks(), numTracks);
            expectEquals (te::getAudioTracks (edit)[0]->getName(), name0);
            session.undo();
            expectEquals (session.getNumAudioTracks(), numTracks + 1, "undo brings a deleted track back");
            expect (! session.deleteTrack (99) && ! session.moveTrack (0, 99));
            session.deleteTrack (0);

            // A new track starts with a colour (the design's colour bar), in the add's undo step
            const auto tracksBeforeAdd = session.getNumAudioTracks();
            session.addAudioTrack();
            expect (! session.getTrackColour (tracksBeforeAdd).isTransparent(), "a new track gets a colour");
            session.undo();
            expectEquals (session.getNumAudioTracks(), tracksBeforeAdd, "one undo removes the new track and its colour");

            beginTest ("Dropping files below the last track makes a new track, all in one undo step");
            {
                const int tracksBefore = session.getNumAudioTracks();
                const auto second = tempFolder.getChildFile ("drop2.wav");
                const auto notAudio = tempFolder.getChildFile ("notes.txt");
                expect (writeTestWav (second, 0.5) && notAudio.replaceWithText ("hello"));
                session.setTrackPan (0, 0.5f);   // an earlier edit that must survive the undo
                juce::StringArray problems;
                expect (session.importAudioFilesAt ({ wav, notAudio, second }, tracksBefore, at (10.0), problems), session.getLastError());
                expectEquals (session.getNumAudioTracks(), tracksBefore + 1);
                expectEquals (problems.size(), 1, "the text file is reported, the rest imported");
                auto* dropTrack = te::getAudioTracks (edit)[tracksBefore];
                expectEquals (dropTrack->getClips().size(), 2);

                if (dropTrack->getClips().size() == 2)
                    expectWithinAbsoluteError (startOf (dropTrack->getClips()[1]), 11.0, 1.0e-3, "the second file follows the first");

                session.undo();
                expectEquals (session.getNumAudioTracks(), tracksBefore, "one undo removes the new track and its clips");
                expectWithinAbsoluteError (session.getTrackPan (0), 0.5f, 0.01f, "...and nothing before it");

                problems.clear();
                expect (! session.importAudioFilesAt ({ notAudio }, tracksBefore, at (10.0), problems));
                expectEquals (session.getNumAudioTracks(), tracksBefore, "a drop with nothing importable leaves no empty track");
            }

            beginTest ("The edits survive saving and reopening, and the clipboard doesn't cross songs");
            session.selectAllClips();
            expect (session.copySelectedClips());
            expect (session.saveSong() && session.closeSong() && session.openSong (songFile), session.getLastError());
            expectEquals (session.getNumAudioTracks(), numTracks);
            expect (! session.hasClipsToPaste(), "closing a song empties the clipboard (its audio paths are relative to that song)");
        }

        void runImportTests (Session& session, const juce::File& songFile, const juce::File& tempFolder)
        {
            beginTest ("Imported audio is copied into the song folder, so deleting the original can't break the song");
            ensureSongIsOpen (session, songFile);

            if (! session.hasSong())
                return;   // already reported by ensureSongIsOpen; don't crash the whole run

            const auto original = tempFolder.getChildFile ("tone.wav");
            expect (writeTestWav (original, 1.0), "could not write test wav");
            const int clipsBefore = te::getAudioTracks (*session.getEdit()).getFirst()->getClips().size();

            expect (session.importAudioFile (original), session.getLastError());
            expectEquals (te::getAudioTracks (*session.getEdit()).getFirst()->getClips().size(), clipsBefore + 1);

            const auto copied = songFile.getParentDirectory().getChildFile (Session::importedAudioFolderName).getChildFile ("tone.wav");
            expect (copied.existsAsFile(), "the audio must live inside the song folder");
            expect (original.deleteFile());
            expect (copied.existsAsFile());
        }
    };

    static AppPathsTests appPathsTests;
    static SessionTests sessionTests;
}
