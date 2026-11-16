/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "Screenshots.h"
#include "../Core/AppPaths.h"
#include "../Engine/AudioEngine.h"
#include "../Engine/Session.h"
#include "../UI/CommandIDs.h"
#include "../UI/Dialogs.h"
#include "../UI/MainComponent.h"
#include "../UI/ParamEditor.h"

namespace ez::Screenshots
{
    namespace
    {
        struct NoDevice : public AudioEngine::Behaviour
        {
            bool autoInitialiseDeviceManager() override     { return false; }
        };

        void pump (int ms)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
        }

        /** Eight bars at 120 BPM of a kick-snare-hat loop with a bass line: something that
            looks like a real recording in the waveform. */
        juce::File writeDemoLoop (const juce::File& folder)
        {
            const auto file = folder.getChildFile ("Demo Loop.wav");
            const double rate = 44100.0;
            const int beats = 32, samplesPerBeat = (int) (rate * 0.5);
            juce::AudioBuffer<float> buffer (2, beats * samplesPerBeat);
            buffer.clear();
            juce::Random random (7);

            for (int beat = 0; beat < beats; ++beat)
            {
                const int start = beat * samplesPerBeat;
                const bool snare = beat % 2 == 1;

                for (int i = 0; i < samplesPerBeat; ++i)
                {
                    const auto t = (double) i / rate;
                    const auto kick = snare ? 0.0 : std::sin (juce::MathConstants<double>::twoPi * (55.0 + 90.0 * std::exp (-t * 30.0)) * t) * std::exp (-t * 9.0);
                    const auto noise = snare ? (random.nextFloat() * 2.0f - 1.0f) * std::exp (-t * 14.0) * 0.7 : 0.0;
                    const auto hat = (random.nextFloat() * 2.0f - 1.0f) * std::exp (-std::fmod (t, 0.25) * 60.0) * 0.18;
                    const auto bass = std::sin (juce::MathConstants<double>::twoPi * (beat % 8 < 4 ? 55.0 : 49.0) * t) * 0.25;
                    const auto v = (float) juce::jlimit (-1.0, 1.0, 0.8 * kick + noise + hat + bass);

                    buffer.setSample (0, start + i, v);
                    buffer.setSample (1, start + i, v * 0.92f);
                }
            }

            file.deleteFile();
            juce::WavAudioFormat wav;

            std::unique_ptr<juce::OutputStream> out = file.createOutputStream();

            if (out != nullptr)
                if (auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (rate)
                                                                                          .withNumChannels (2)
                                                                                          .withBitsPerSample (24)))
                    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());

            return file;
        }

        te::MidiClip* midiClip (Session& session, int track, double startBeat, double beats, const juce::String& name)
        {
            const auto& tempo = session.getEdit()->tempoSequence;
            const auto start = tempo.toTime (te::BeatPosition::fromBeats (startBeat));
            const auto end = tempo.toTime (te::BeatPosition::fromBeats (startBeat + beats));

            if (! session.createMidiClip (track, start, end - start, name))
                return nullptr;

            return dynamic_cast<te::MidiClip*> (session.getSelectedClip());
        }

        /** A short song that uses every kind of track, so each view has something on it. */
        void buildDemoSong (Session& session, const juce::File& folder)
        {
            session.setBpm (120.0);

            for (const auto& preset : session.getPresets (Session::PresetKind::trackPreset))
                if (preset.isBuiltIn() && preset.name.startsWith ("Drums:"))
                {
                    session.addTrackFromPreset (preset);
                    break;
                }

            while (session.getNumAudioTracks() < 6)
                session.addAudioTrack();

            const juce::StringArray names { "Vocal", "Guitar", "Bass", "Keys", "Drums", "Loop" };
            const juce::Array<juce::Colour> colours { juce::Colour (0xffe0607e), juce::Colour (0xffe8a24a), juce::Colour (0xff6c8cff),
                                                      juce::Colour (0xff4cc9a0), juce::Colour (0xff38bdf8), juce::Colour (0xffb58cff) };

            for (int i = 0; i < juce::jmin (names.size(), session.getNumAudioTracks()); ++i)
            {
                session.renameTrack (i, names[i]);
                session.setTrackColour (i, colours[i]);
            }

            // Keys: chords; Bass: a line
            if (auto* keys = midiClip (session, 3, 0.0, 16.0, "Chords"))
                for (int bar = 0; bar < 4; ++bar)
                    for (auto pitch : { 60, 64, 67, 71 })
                        session.addMidiNote (*keys, pitch - (bar % 2) * 3, bar * 4.0, 3.5, 90);

            if (auto* bass = midiClip (session, 2, 0.0, 32.0, "Bass line"))
                for (int i = 0; i < 32; ++i)
                    session.addMidiNote (*bass, 36 + (i % 4 == 3 ? 7 : 0) - (i / 8 % 2) * 2, i, 0.75, 70 + (i % 4) * 12);

            if (auto* beat = midiClip (session, 4, 0.0, 32.0, "Beat"))
                for (int i = 0; i < 64; ++i)
                {
                    session.addMidiNote (*beat, 42, i * 0.5, 0.25, i % 2 == 0 ? 100 : 70);

                    if (i % 4 == 0) session.addMidiNote (*beat, 36, i * 0.5, 0.25, 120);
                    if (i % 8 == 4) session.addMidiNote (*beat, 38, i * 0.5, 0.25, 110);
                }

            const auto loop = writeDemoLoop (folder);
            session.importAudioFileAt (loop, 5, te::TimePosition());
            session.importAudioFileAt (loop, 1, te::TimePosition::fromSeconds (8.0));

            session.addSection (0.0, 16.0, "Intro");
            session.addSection (16.0, 48.0, "Verse");
            session.addSection (48.0, 80.0, "Chorus");

            session.setLoopRange (te::TimePosition::fromSeconds (8.0), te::TimePosition::fromSeconds (16.0));
            session.setLooping (true);
            session.setPosition (te::TimePosition::fromSeconds (5.0));
            session.selectClip (nullptr);
        }

        bool save (juce::Component& c, const juce::File& file)
        {
            const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            file.deleteFile();
            juce::PNGImageFormat png;
            auto out = file.createOutputStream();
            return out != nullptr && png.writeImageToStream (image, *out);
        }

        /** Every built-in effect and the synth, in their editors: one PNG each, plus a list of their
            parameters (what a layout can refer to) in plugins.txt. */
        int savePluginEditors (Session& session, const juce::File& folder)
        {
            int problems = 0;
            juce::String list;

            const auto render = [&] (te::Plugin& plugin, const juce::String& name)
            {
                list << "== " << name << " (" << plugin.getPluginType() << ")\n";

                for (auto* p : plugin.getAutomatableParameters())
                    list << "  " << p->paramID << " | " << p->getParameterName() << " | " << p->valueRange.start << ".." << p->valueRange.end
                         << " step " << p->valueRange.interval << " | now " << p->getCurrentValueAsString() << "\n";

                auto content = ParamEditor::createContent (plugin);
                content->setVisible (true);
                juce::Component holder;
                holder.setSize (content->getWidth(), content->getHeight());
                holder.addAndMakeVisible (*content);
                holder.addToDesktop (0);
                holder.setVisible (true);
                pump (300);
                const auto fileName = "plugin-" + juce::File::createLegalFileName (name).replaceCharacter (' ', '-').toLowerCase() + ".png";
                problems += save (holder, folder.getChildFile (fileName)) ? 0 : 1;

                // plug-ins with pages (the synth): every other page too
                juce::Array<juce::TextButton*> tabs;
                std::function<void (juce::Component&)> findTabs = [&] (juce::Component& c)
                {
                    for (auto* child : c.getChildren())
                    {
                        if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getTooltip().startsWith ("Show ")
                                                                                 && b->getTooltip().contains ("(Ctrl+"))
                            tabs.add (b);

                        findTabs (*child);
                    }
                };
                findTabs (*content);

                for (int i = 1; i < tabs.size(); ++i)
                {
                    tabs[i]->triggerClick();
                    pump (300);
                    holder.setSize (content->getWidth(), content->getHeight());
                    problems += save (holder, folder.getChildFile (fileName.replace (".png", "-" + juce::String (i + 1) + ".png"))) ? 0 : 1;
                }

                holder.removeFromDesktop();
                holder.removeChildComponent (content.get());
            };

            const auto track = session.getNumAudioTracks();
            session.addAudioTrack();

            for (const auto& choice : session.getAvailableEffects())
            {
                if (choice.pluginType == te::ExternalPlugin::xmlTypeName || choice.name == "Vocal Chain" || choice.name == "Tuner")
                    continue;   // these have editors of their own

                if (! session.addEffect (track, choice, false))
                {
                    ++problems;
                    continue;
                }

                if (auto* audioTrack = te::getAudioTracks (*session.getEdit())[track])
                    for (auto* plugin : audioTrack->pluginList.getPlugins())
                        if (plugin->getPluginType() == choice.pluginType)
                            render (*plugin, choice.name);
            }

            for (const auto& choice : session.getAvailableInstruments())
                if (choice.name.startsWith ("Synth") && session.addInstrumentTrack (choice, false))
                    if (auto* audioTrack = te::getAudioTracks (*session.getEdit()).getLast())
                        for (auto* plugin : audioTrack->pluginList.getPlugins())
                            if (plugin->getPluginType() == choice.pluginType)
                                render (*plugin, choice.name);

            folder.getChildFile ("plugins.txt").replaceWithText (list);
            return problems;
        }

        /** Dialog windows open as separate desktop windows: save each one. */
        int saveDialogs (juce::Component& main, const juce::File& folder, const juce::String& prefix)
        {
            int saved = 0;

            for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
                if (auto* c = juce::Desktop::getInstance().getComponent (i); c != nullptr && c != &main && c->isVisible()
                                                                             && c->getWidth() > 200 && c->getHeight() > 150)
                    saved += save (*c, folder.getChildFile (prefix + juce::String (saved + 1) + ".png")) ? 1 : 0;

            return saved;
        }
    }

    int run (const juce::File& folder)
    {
        folder.createDirectory();
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const auto root = exe.getSiblingFile ("screenshot-run");
        root.deleteRecursively();
        AppPaths::setRootFolderOverride (root);
        AppPaths::createFolders();

        int problems = 0;
        {
            AudioEngine engine ("EzDAWsIt-Screenshots", std::make_unique<te::UIBehaviour>(), std::make_unique<NoDevice>());
            engine.getEngine().getPropertyStorage().getPropertiesFile().setValue ("ezTourSeen", true);
            Session session (engine);

            juce::DocumentWindow window ("EzDAWsIt", juce::Colours::black, 0, true);
            auto* main = new MainComponent (engine, session);
            window.setContentOwned (main, false);
            window.setSize (1280, 760);
            window.setVisible (true);
            pump (1500);   // the start-up song opens
            problems += save (*main, folder.getChildFile ("empty-1280x760.png")) ? 0 : 1;

            buildDemoSong (session, root);
            pump (800);

            struct Shot { juce::String name; juce::CommandID view; };
            const Shot views[] = { { "arrange", CommandIDs::showArrangeView },
                                   { "mix",     CommandIDs::showMixView },
                                   { "master",  CommandIDs::showMasterView } };

            for (auto size : { juce::Point<int> (1280, 760), juce::Point<int> (900, 500) })
            {
                window.setSize (size.x, size.y);
                const auto suffix = "-" + juce::String (size.x) + "x" + juce::String (size.y) + ".png";

                for (const auto& shot : views)
                {
                    main->perform (juce::ApplicationCommandTarget::InvocationInfo (shot.view));
                    pump (500);
                    problems += save (*main, folder.getChildFile (shot.name + suffix)) ? 0 : 1;
                }

                // A selected MIDI clip opens its editor in the detail panel
                main->perform (juce::ApplicationCommandTarget::InvocationInfo (CommandIDs::showArrangeView));

                for (auto* track : te::getAudioTracks (*session.getEdit()))
                    if (track->getName() == "Keys" && ! track->getClips().isEmpty())
                        session.selectClip (track->getClips().getFirst());

                main->perform (juce::ApplicationCommandTarget::InvocationInfo (CommandIDs::toggleClipEditor));
                pump (600);
                problems += save (*main, folder.getChildFile ("arrange-pianoroll" + suffix)) ? 0 : 1;

                for (auto* track : te::getAudioTracks (*session.getEdit()))
                    if (track->getName() == "Drums" && ! track->getClips().isEmpty())
                        session.selectClip (track->getClips().getFirst());

                pump (600);
                problems += save (*main, folder.getChildFile ("arrange-drums" + suffix)) ? 0 : 1;

                main->perform (juce::ApplicationCommandTarget::InvocationInfo (CommandIDs::toggleDetailPanel));   // the track's devices
                pump (600);
                problems += save (*main, folder.getChildFile ("arrange-devices" + suffix)) ? 0 : 1;
                main->perform (juce::ApplicationCommandTarget::InvocationInfo (CommandIDs::toggleDetailPanel));
                session.selectClip (nullptr);
            }

            window.setSize (1280, 760);

            // A menu with every kind of item, and a message box
            {
                juce::PopupMenu sub, menu;
                sub.addItem (100, "Red");
                sub.addItem (101, "Blue");
                menu.addSectionHeader ("Track");
                menu.addItem (juce::PopupMenu::Item ("Rename\tF2").setID (1));
                menu.addItem (juce::PopupMenu::Item ("Duplicate\tCtrl+Shift+D").setID (2));
                menu.addItem (juce::PopupMenu::Item ("Mute\tM").setID (3).setTicked (true));
                menu.addSubMenu ("Colour", sub);
                menu.addSeparator();
                menu.addItem (juce::PopupMenu::Item ("Freeze (coming soon)").setID (4).setEnabled (false));
                menu.addItem (juce::PopupMenu::Item ("Delete\tCtrl+Delete").setID (5));
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ 400, 300, 1, 1 }));
                pump (400);
                saveDialogs (window, folder, "menu-");
                juce::PopupMenu::dismissAllActiveMenus();
                pump (200);

                main->perform (juce::ApplicationCommandTarget::InvocationInfo (CommandIDs::showAbout));
                pump (500);
                saveDialogs (window, folder, "message-");
                juce::ModalComponentManager::getInstance()->cancelAllModalComponents();
                pump (200);
            }

            for (auto command : { CommandIDs::exportSong, CommandIDs::audioSetup })
            {
                main->perform (juce::ApplicationCommandTarget::InvocationInfo (command));
                pump (800);
                saveDialogs (window, folder, command == CommandIDs::exportSong ? "dialog-export-" : "dialog-setup-");
                Dialogs::closeAll();
                pump (200);
            }

            problems += savePluginEditors (session, folder);

            session.closeSong();
            window.clearContentComponent();
        }

        AppPaths::setRootFolderOverride ({});
        return problems;
    }
}
