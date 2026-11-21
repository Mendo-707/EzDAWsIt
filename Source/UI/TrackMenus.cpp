/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "TrackMenus.h"
#include "../Engine/Effects.h"

namespace ez::TrackMenus
{
    const std::vector<NamedColour>& getTrackColours()
    {
        // The same colours new tracks get, so a new track's colour shows ticked here
        static const std::vector<NamedColour> colours = []
        {
            std::vector<NamedColour> list { { "None", juce::Colours::transparentBlack } };

            for (const auto& c : Session::getTrackPalette())
                list.push_back ({ c.name, c.colour });

            list.push_back ({ "Grey", Session::getBusColour() });
            return list;
        }();
        return colours;
    }

    namespace
    {
        enum SendAction { send0dB = 1, sendMinus6, sendMinus12, sendPre, sendPost, sendOnOff, sendRemove, sendShowReturn };

        juce::String describeSend (const Session::SendInfo& send)
        {
            if (! send.exists)
                return "off";

            return (send.levelDb <= Session::minVolumeDb + 0.05f ? juce::String ("-inf") : juce::String (send.levelDb, 1) + " dB")
                   + (send.preFader ? ", pre" : "") + (send.enabled ? "" : ", muted");
        }
    }

    juce::PopupMenu makeSendMenu (Session& session, int trackIndex, int bus, int base, const juce::String& header)
    {
        const auto send = session.getSend (trackIndex, bus);
        juce::PopupMenu menu;

        if (header.isNotEmpty())
            menu.addSectionHeader (header);

        menu.addItem (base + send0dB,     "0 dB (double-click or Alt+click the knob)");
        menu.addItem (base + sendMinus6,  "-6 dB");
        menu.addItem (base + sendMinus12, "-12 dB");
        menu.addSeparator();
        menu.addItem (base + sendPre,  "Pre-fader (ignores the track's fader)", true, send.exists && send.preFader);
        menu.addItem (base + sendPost, "Post-fader (follows the fader)", true, send.exists && ! send.preFader);
        menu.addItem (base + sendOnOff, "Mute this send (keeps the level)", send.exists, send.exists && ! send.enabled);
        menu.addItem (base + sendRemove, "Remove send", send.exists);
        menu.addSeparator();
        menu.addItem (base + sendShowReturn, "Go to the return");
        return menu;
    }

    bool performSendAction (Session& session, int t, int bus, int action, const Callbacks& callbacks)
    {
        switch (action)
        {
            case send0dB:      return session.setSendLevel (t, bus, 0.0f);
            case sendMinus6:   return session.setSendLevel (t, bus, -6.0f);
            case sendMinus12:  return session.setSendLevel (t, bus, -12.0f);
            case sendPre:      return session.setSendPreFader (t, bus, true);
            case sendPost:     return session.setSendPreFader (t, bus, false);
            case sendOnOff:    return session.setSendEnabled (t, bus, ! session.getSend (t, bus).enabled);
            case sendRemove:   return session.removeSend (t, bus);
            case sendShowReturn:
                for (const auto& r : session.getReturns())
                    if (r.bus == bus && callbacks.showTrack)
                        callbacks.showTrack (r.trackIndex);

                return true;
            default:           return true;
        }
    }

    void addEffectChoices (juce::PopupMenu& menu, const juce::Array<Session::PluginChoice>& choices, int base)
    {
        for (const auto& category : Effects::categories())
        {
            juce::PopupMenu sub;

            for (int i = 0; i < choices.size(); ++i)
                if (choices.getReference (i).category == category)
                    sub.addItem (base + 1 + i, choices.getReference (i).name);

            if (sub.getNumItems() > 0)
                menu.addSubMenu (category, sub);
        }

        juce::PopupMenu plugins;

        for (int i = 0; i < choices.size(); ++i)
            if (choices.getReference (i).category.isEmpty())
                plugins.addItem (base + 1 + i, choices.getReference (i).name);

        if (plugins.getNumItems() > 0)
            menu.addSubMenu ("VST3 plugins", plugins);
        else
            menu.addItem (-1, "VST3 plugins: scan in Setup > Plugins", false, false);
    }

    juce::PopupMenu makeAddReturnMenu (int base)
    {
        juce::PopupMenu menu;
        menu.addItem (base + 1, "Reverb return (Ctrl+Shift+R)");
        menu.addItem (base + 2, "Delay return");
        menu.addItem (base + 3, "Empty return (add your own effect)");
        return menu;
    }

    int performAddReturn (Session& session, int action)
    {
        switch (action)
        {
            case 1:  return session.addReturnTrack (Session::ReturnEffect::reverb);
            case 2:  return session.addReturnTrack (Session::ReturnEffect::delay);
            case 3:  return session.addReturnTrack (Session::ReturnEffect::none);
            default: return -1;
        }
    }

    void askForName (const juce::String& title, const juce::String& message, const juce::String& suggestion,
                     std::function<void (const juce::String&)> onName)
    {
        auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", suggestion);
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onName] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();

            if (result == 1 && name.isNotEmpty() && onName)
                onName (name);
        }), true);
    }

    void askForPresetName (Session& session, Session::PresetKind kind, const juce::String& title, const juce::String& message,
                           const juce::String& suggestion, std::function<void (const juce::String&)> onName)
    {
        askForName (title, message, suggestion, [&session, kind, onName] (const juce::String& name)
        {
            if (! session.presetExists (kind, name))
            {
                onName (name);
                return;
            }

            const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                          .withTitle ("Replace \"" + name + "\"?")
                                                          .withMessage ("You already have one called \"" + name + "\". Replace it with this one?")
                                                          .withButton ("Replace", 1).withButton ("Cancel", 0);
            juce::AlertWindow::showAsync (options, [name, onName] (int result)
            {
                if (result == 1)
                    onName (name);
            });
        });
    }

    juce::Array<Session::PresetInfo> addPresetItems (juce::PopupMenu& menu, Session& session, int trackIndex, int base)
    {
        juce::PopupMenu chains;
        const auto list = session.getPresets (Session::PresetKind::effectChain);

        for (int i = 0; i < list.size() && i < 900; ++i)
        {
            if (i > 0 && list[i - 1].isBuiltIn() && ! list[i].isBuiltIn())
                chains.addSeparator();   // yours after the built-in ones

            chains.addItem (base + 10 + i, list[i].name + (list[i].description.isNotEmpty() ? juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + list[i].description : juce::String()));
        }

        menu.addSubMenu ("Add an effect chain", chains);
        menu.addItem (base + 1, "Save its effects as a chain...", ! session.getEffectNames (trackIndex).isEmpty());
        menu.addItem (base + 2, "Save the track as a preset...");
        return list;
    }

    bool performPresetItem (Session& session, int trackIndex, int result, int base, const juce::Array<Session::PresetInfo>& list,
                            std::function<void (const juce::String&)> problem)
    {
        const auto item = result - base;

        if (item < 1 || item >= 1000)
            return false;

        const auto name = session.getTrackName (trackIndex);
        const auto report = [&session, problem] (bool ok) { if (! ok && problem) problem (session.getLastError()); };

        if (item == 1)
        {
            askForPresetName (session, Session::PresetKind::effectChain, "Save effect chain",
                              "A name for the chain (it goes in Browser > Effect chains):", name + " chain",
                        [&session, trackIndex, report] (const juce::String& n) { report (session.saveEffectChain (trackIndex, n)); });
            return true;
        }

        if (item == 2)
        {
            askForPresetName (session, Session::PresetKind::trackPreset, "Save track preset",
                              "A name for the preset (it goes in Browser > Track presets):", name,
                        [&session, trackIndex, report] (const juce::String& n) { report (session.saveTrackPreset (trackIndex, n)); });
            return true;
        }

        if (juce::isPositiveAndBelow (item - 10, list.size()))
            report (session.applyEffectChain (trackIndex, list[item - 10]));

        return true;
    }

    namespace
    {
        std::function<void (int)>& tunerOpener()
        {
            static std::function<void (int)> opener;
            return opener;
        }
    }

    void setTunerOpener (std::function<void (int)> opener)
    {
        tunerOpener() = std::move (opener);
    }

    void showTrackMenu (Session& session, int trackIndex, Callbacks callbacks)
    {
        enum { renameId = 1, duplicateId, deleteId, moveUpId, moveDownId, newBusFromId, addBusId, tunerId, stopDuckId,
               muteId, soloId, armId, freezeId, duckBase = 700,
               colourBase = 100, outputBase = 200, addReturnBase = 500, sendBase = 1000, sendStride = 16, presetBase = 10000 };

        const auto& colours = getTrackColours();
        const auto numTracks = session.getNumAudioTracks();
        const auto kind = session.getTrackKind (trackIndex);
        const auto outputs = session.getTrackOutputChoices (trackIndex);
        const auto currentOutput = session.getTrackOutput (trackIndex);
        const auto returns = session.getReturns();
        juce::PopupMenu menu, colourMenu, outputMenu, sendsMenu;

        if (kind != Session::TrackKind::normal)
            menu.addSectionHeader (Session::describeKind (kind) + ": " + session.getTrackName (trackIndex));

        const auto addWithKey = [&menu] (int id, const juce::String& text, const juce::String& key, bool enabled = true)
        {
            juce::PopupMenu::Item item (text);
            item.itemID = id;
            item.isEnabled = enabled;
            item.shortcutKeyDescription = key;
            menu.addItem (item);
        };

        addWithKey (renameId,    "Rename track", "F2");
        addWithKey (duplicateId, "Duplicate track", "Ctrl+Shift+D");
        addWithKey (moveUpId,    callbacks.horizontal ? "Move left" : "Move up", "Ctrl+Alt+Up", trackIndex > 0);
        addWithKey (moveDownId,  callbacks.horizontal ? "Move right" : "Move down", "Ctrl+Alt+Down", trackIndex < numTracks - 1);

        if (kind == Session::TrackKind::normal)
        {
            menu.addSeparator();
            addWithKey (muteId, "Mute", "M");
            addWithKey (soloId, "Solo", "S");
            addWithKey (armId, session.isTrackArmed (trackIndex) ? "Disarm" : "Arm for recording", "Shift+R");
            addWithKey (freezeId, session.isTrackFrozen (trackIndex) ? "Unfreeze (edit it again)" : "Freeze (render it, save CPU)", "Ctrl+Alt+F",
                        ! session.isRecording());
        }

        for (int i = 0; i < (int) colours.size(); ++i)
            colourMenu.addItem (colourBase + i, colours[(size_t) i].name, true, session.getTrackColour (trackIndex) == colours[(size_t) i].colour);

        menu.addSubMenu ("Track colour", colourMenu);
        menu.addSeparator();

        // Where it plays, and what it sends
        for (int i = 0; i < outputs.size(); ++i)
            outputMenu.addItem (outputBase + i, outputs.getReference (i).name, true, outputs.getReference (i).id == currentOutput);

        menu.addSubMenu ("Output", outputMenu);

        if (session.canSend (trackIndex))
        {
            for (int i = 0; i < (int) returns.size(); ++i)
                sendsMenu.addSubMenu (returns[(size_t) i].name + ": " + describeSend (session.getSend (trackIndex, returns[(size_t) i].bus)),
                                      makeSendMenu (session, trackIndex, returns[(size_t) i].bus, sendBase + i * sendStride));

            if (returns.empty())
                sendsMenu.addItem (-1, "No FX returns yet (add one below)", false);

            menu.addSubMenu ("Sends", sendsMenu);
            menu.addItem (newBusFromId, "Play into a new bus (Ctrl+Shift+G)", kind != Session::TrackKind::fxReturn);
        }

        menu.addItem (addBusId, "Add a bus (Ctrl+Shift+B)");
        menu.addSubMenu ("Add an FX return", makeAddReturnMenu (addReturnBase));
        menu.addSeparator();
        const auto chains = addPresetItems (menu, session, trackIndex, presetBase);

        // Ducking: quieter each time another track plays (bass under the kick, a pad pumping)
        if (kind != Session::TrackKind::fxReturn)
        {
            juce::PopupMenu duckMenu;
            const auto trigger = session.getDuckingTrigger (trackIndex);

            for (int t = 0; t < numTracks && t < 250; ++t)
                if (t != trackIndex && session.getTrackKind (t) == Session::TrackKind::normal)
                    duckMenu.addItem (duckBase + t, session.getTrackName (t), session.canDuckUnder (trackIndex, t), t == trigger);

            if (trigger >= 0)
            {
                duckMenu.addSeparator();
                duckMenu.addItem (stopDuckId, "Stop ducking");
            }

            menu.addSubMenu ("Duck under (sidechain)", duckMenu);
        }

        if (kind == Session::TrackKind::normal && tunerOpener() != nullptr)
        {
            juce::PopupMenu::Item tuner ("Tuner...");
            tuner.itemID = tunerId;
            tuner.shortcutKeyDescription = "Ctrl+U";
            menu.addItem (tuner);
        }

        menu.addSeparator();
        addWithKey (deleteId, "Delete track", "Ctrl+Del");

        // The session outlives every view; the callbacks belong to the caller, which checks its own lifetime
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [&session, trackIndex, callbacks, outputs, returns, chains] (int result)
        {
            if (result <= 0)
                return;

            const auto t = trackIndex;
            bool ok = true;
            int newTrack = -1;

            switch (result)
            {
                case renameId:    if (callbacks.rename) callbacks.rename(); return;
                case tunerId:     if (tunerOpener() != nullptr) tunerOpener() (t); return;
                case stopDuckId:  ok = session.stopDucking (t); break;
                case duplicateId: ok = session.duplicateTrack (t); break;
                case muteId:      session.setTrackMuted (t, ! session.isTrackMuted (t)); break;
                case soloId:      session.setTrackSoloed (t, ! session.isTrackSoloed (t)); break;
                case armId:       ok = session.setTrackArmed (t, ! session.isTrackArmed (t)); break;
                case freezeId:    ok = session.setTrackFrozen (t, ! session.isTrackFrozen (t)); break;
                case deleteId:    ok = session.deleteTrack (t); break;   // undoable, so no "are you sure?"
                case moveUpId:    ok = session.moveTrack (t, t - 1); if (ok && callbacks.moved) callbacks.moved (t - 1); break;
                case moveDownId:  ok = session.moveTrack (t, t + 1); if (ok && callbacks.moved) callbacks.moved (t + 1); break;
                case newBusFromId: newTrack = session.routeTracksToNewBus ({ t }); ok = newTrack >= 0; break;
                case addBusId:     newTrack = session.addBusTrack(); ok = newTrack >= 0; break;
                default:
                    if (result >= duckBase && result < duckBase + 250)
                    {
                        ok = session.duckUnder (t, result - duckBase);
                        break;
                    }

                    if (result >= presetBase)
                    {
                        performPresetItem (session, t, result, presetBase, chains, callbacks.problem);
                        return;
                    }

                    if (result >= sendBase)
                    {
                        const auto r = (result - sendBase) / sendStride;

                        if (juce::isPositiveAndBelow (r, (int) returns.size()))
                            ok = performSendAction (session, t, returns[(size_t) r].bus, (result - sendBase) % sendStride, callbacks);
                    }
                    else if (result >= addReturnBase)
                    {
                        newTrack = performAddReturn (session, result - addReturnBase);
                        ok = newTrack >= 0;
                    }
                    else if (result >= outputBase)
                    {
                        if (juce::isPositiveAndBelow (result - outputBase, outputs.size()))
                            ok = session.setTrackOutput (t, outputs.getReference (result - outputBase).id);
                    }
                    else if (result >= colourBase && result < colourBase + (int) getTrackColours().size())
                    {
                        ok = session.setTrackColour (t, getTrackColours()[(size_t) (result - colourBase)].colour);
                    }
                    break;
            }

            if (ok && newTrack >= 0 && callbacks.showTrack)
                callbacks.showTrack (newTrack);

            if (! ok && callbacks.problem)
                callbacks.problem (session.getLastError());
        });
    }

    void showInsertMenu (Session& session, int trackIndex, int effectIndex, juce::Component& target,
                         std::function<void (const juce::String&)> problem, std::function<void()> changed)
    {
        const auto effects = session.getEffectInfo (trackIndex);
        const auto choices = session.getAvailableEffects();
        constexpr int openId = 1, bypassId = 2, upId = 3, downId = 4, removeId = 5, addBase = 1000;
        juce::PopupMenu menu, addMenu;
        addEffectChoices (addMenu, choices, addBase - 1);   // IDs addBase + index

        if (juce::isPositiveAndBelow (effectIndex, effects.size()))
        {
            const auto& fx = effects.getReference (effectIndex);
            menu.addSectionHeader (fx.name);
            menu.addItem (openId, "Open");
            menu.addItem (bypassId, "Bypass", true, ! fx.isEnabled);
            menu.addItem (upId, "Move up", effectIndex > 0);
            menu.addItem (downId, "Move down", effectIndex < effects.size() - 1);
            menu.addItem (removeId, "Remove");
            menu.addSeparator();
            menu.addSubMenu ("Add an effect after the others", addMenu);
        }
        else
        {
            menu.addSectionHeader ("Add effect");
            addEffectChoices (menu, choices, addBase - 1);   // IDs addBase + index
        }

        juce::Component::SafePointer<juce::Component> safe (&target);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [&session, trackIndex, effectIndex, choices, problem, changed, safe, effects] (int result)
        {
            if (result <= 0)
                return;

            bool ok = true;

            if (result >= addBase)          ok = session.addEffect (trackIndex, choices[result - addBase], true);
            else if (result == openId)      ok = session.openEffectWindow (trackIndex, effectIndex);
            else if (result == bypassId)    ok = session.setEffectEnabled (trackIndex, effectIndex, ! effects.getReference (effectIndex).isEnabled);
            else if (result == upId)        ok = session.moveEffect (trackIndex, effectIndex, effectIndex - 1);
            else if (result == downId)      ok = session.moveEffect (trackIndex, effectIndex, effectIndex + 1);
            else if (result == removeId)    ok = session.removeEffect (trackIndex, effectIndex);

            if (safe == nullptr)
                return;

            if (! ok && problem)
                problem (session.getLastError());

            if (changed)
                changed();
        });
    }
}
