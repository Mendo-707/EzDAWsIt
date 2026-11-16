/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Session: FX returns, sends and buses (see Session.h for what each one is).

    How it maps onto Tracktion:
      - A return is an audio track whose first plugin is an AuxReturnPlugin on bus N.
      - A send is an AuxSendPlugin on bus N in the sending track's plugin list. Its place in the
        list is pre/post fader: before the track's VolumeAndPanPlugin = pre, after it = post.
        Mute is the plugin's bypass (Tracktion's own send mute parks the level at -100 dB).
      - A bus is an audio track other tracks play into (TrackOutput::setOutputToTrack).
    Tracktion remembers "plays into track N" by track NUMBER, which goes stale when tracks move.
    We keep the bus's ID on the source track (busProp) and put the number right after every
    change that can move tracks (syncBusRouting).
*/

#include "Session.h"
#include "AudioEngine.h"

namespace ez
{
    namespace
    {
        const juce::Identifier kindProp ("ezKind"), busProp ("ezBus");
        constexpr const char* busKind = "bus";
        constexpr const char* returnKind = "return";
        constexpr const char* busChoicePrefix = "bus:";

        te::AuxReturnPlugin* findReturnPlugin (te::AudioTrack& track)
        {
            for (auto* plugin : track.pluginList)
                if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (plugin))
                    return r;

            return nullptr;
        }

        juce::ValueTree outputDeviceState (te::AudioTrack& track)
        {
            return track.state.getChildWithName (te::IDs::OUTPUTDEVICES).getChildWithName (te::IDs::DEVICE);
        }

        float sendPositionFor (float db)
        {
            return db <= Session::minVolumeDb ? 0.0f
                                              : te::decibelsToVolumeFaderPosition (juce::jmin (db, Session::maxVolumeDb));
        }
    }

    //==============================================================================
    Session::TrackKind Session::kindOf (const te::AudioTrack& track)
    {
        const auto kind = track.state[kindProp].toString();

        if (kind == busKind)
            return TrackKind::bus;

        if (kind == returnKind)
            return TrackKind::fxReturn;

        // A song from elsewhere: a track with a return plugin behaves as one, so treat it as one
        for (auto* plugin : const_cast<te::AudioTrack&> (track).pluginList)
            if (dynamic_cast<te::AuxReturnPlugin*> (plugin) != nullptr)
                return TrackKind::fxReturn;

        return TrackKind::normal;
    }

    Session::TrackKind Session::getTrackKind (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);
        return track != nullptr ? kindOf (*track) : TrackKind::normal;
    }

    juce::String Session::describeKind (TrackKind kind)
    {
        switch (kind)
        {
            case TrackKind::bus:      return "Bus";
            case TrackKind::fxReturn: return "FX return";
            case TrackKind::normal:   break;
        }

        return "Track";
    }

    te::AuxSendPlugin* Session::findSend (te::AudioTrack& track, int bus) const
    {
        for (auto* plugin : track.pluginList)
            if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin))
                if (send->busNumber.get() == bus)
                    return send;

        return nullptr;
    }

    te::AudioTrack* Session::findReturnTrack (int bus) const
    {
        if (edit != nullptr)
            for (auto* track : te::getAudioTracks (*edit))
                if (kindOf (*track) == TrackKind::fxReturn)
                    if (auto* r = findReturnPlugin (*track); r != nullptr && r->busNumber.get() == bus)
                        return track;

        return nullptr;
    }

    int Session::findFreeBus() const
    {
        if (edit == nullptr)
            return -1;

        // Not used by a return, nor by a stray send (which would suddenly start playing into the new return)
        juce::BigInteger used;

        for (auto* track : te::getAudioTracks (*edit))
            for (auto* plugin : track->pluginList)
            {
                if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (plugin))
                    used.setBit (juce::jlimit (0, 1023, r->busNumber.get()));
                else if (auto* s = dynamic_cast<te::AuxSendPlugin*> (plugin))
                    used.setBit (juce::jlimit (0, 1023, s->busNumber.get()));
            }

        for (int bus = 0; bus < maxReturns; ++bus)
            if (! used[bus])
                return bus;

        return -1;
    }

    te::AudioTrack* Session::findBusTarget (const te::AudioTrack& track) const
    {
        const auto id = te::EditItemID::fromVar (track.state[busProp]);

        if (edit == nullptr || ! id.isValid())
            return nullptr;

        auto* bus = te::findAudioTrackForID (*edit, id);
        return bus != nullptr && bus != &track && kindOf (*bus) == TrackKind::bus ? bus : nullptr;
    }

    bool Session::canPlayInto (const te::AudioTrack& source, const te::AudioTrack& bus) const
    {
        if (&source == &bus || kindOf (bus) != TrackKind::bus || kindOf (source) == TrackKind::fxReturn)
            return false;

        // Never in a circle: follow the bus's own chain (by ID, which is always right)
        const auto limit = getNumAudioTracks() + 1;
        int steps = 0;

        for (auto* t = &bus; t != nullptr && steps <= limit; t = findBusTarget (*t), ++steps)
            if (t == &source)
                return false;

        return steps <= limit;
    }

    juce::String Session::makeUniqueTrackName (const juce::String& base) const
    {
        juce::StringArray names;

        if (edit != nullptr)
            for (auto* t : te::getAudioTracks (*edit))
                names.add (t->getName());

        if (! names.contains (base))
            return base;

        for (int n = 2;; ++n)
            if (! names.contains (base + " " + juce::String (n)))
                return base + " " + juce::String (n);
    }

    te::AudioTrack* Session::appendTrack (const juce::String& name, TrackKind kind)
    {
        const auto before = getNumAudioTracks();
        edit->ensureNumberOfAudioTracks (before + 1);
        auto* track = getNumAudioTracks() == before + 1 ? te::getAudioTracks (*edit).getLast() : nullptr;

        if (track == nullptr)
            return nullptr;

        track->setName (name);

        if (kind == TrackKind::bus)
            track->setColour (getBusColour());
        else
            colourNewTracks (before);

        if (kind != TrackKind::normal)
            track->state.setProperty (kindProp, kind == TrackKind::bus ? busKind : returnKind, &edit->getUndoManager());

        return track;
    }

    bool Session::routeIntoBus (te::AudioTrack& source, te::AudioTrack& bus)
    {
        if (! canPlayInto (source, bus))
            return fail ("\"" + source.getName() + "\" can't play into \"" + bus.getName() + "\""
                         + (kindOf (source) == TrackKind::fxReturn ? ": FX returns play into the main mix" : " (that would go round in a circle)"));

        source.state.setProperty (busProp, bus.itemID.toVar(), &edit->getUndoManager());
        source.getOutput().setOutputToTrack (&bus);
        return source.getOutput().getDestinationTrack() == &bus || fail ("Could not route \"" + source.getName() + "\" into the bus");
    }

    void Session::removeSendsTo (int bus)
    {
        for (auto* track : te::getAudioTracks (*edit))
            for (auto* plugin : track->pluginList.getPlugins())   // a copy: we delete as we go
                if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin); send != nullptr && send->busNumber.get() == bus)
                    send->deleteFromParent();
    }

    void Session::rerouteTracksInto (te::AudioTrack& bus)
    {
        // What played into the bus now goes where the bus went (another bus, or the main mix)
        auto* next = findBusTarget (bus);
        auto& um = edit->getUndoManager();

        for (auto* track : te::getAudioTracks (*edit))
        {
            if (findBusTarget (*track) != &bus)
                continue;

            if (next != nullptr && canPlayInto (*track, *next) && routeIntoBus (*track, *next))
                continue;

            track->state.removeProperty (busProp, &um);
            track->getOutput().setOutputToDefaultDevice (false);
        }
    }

    void Session::syncBusRouting()
    {
        if (edit == nullptr)
            return;

        for (auto* track : te::getAudioTracks (*edit))
        {
            auto device = outputDeviceState (*track);

            if (! track->state.hasProperty (busProp))
            {
                // Routed into a track some other way (a song from elsewhere): keep its number right too, or
                // the next save writes the fix into the user's last undo step
                if (auto* dest = track->getOutput().getDestinationTrack(); dest != nullptr && device.isValid())
                    if (const auto wanted = "track " + juce::String (dest->getAudioTrackNumber()); device[te::IDs::name].toString() != wanted)
                        device.setProperty (te::IDs::name, wanted, nullptr);

                continue;
            }

            auto* bus = findBusTarget (*track);

            if (bus == nullptr || ! canPlayInto (*track, *bus))
            {
                // The bus is gone (or the song was changed elsewhere): play into the main mix rather than nowhere
                log ("\"" + track->getName() + "\" lost its bus: it plays into the main mix");
                track->state.removeProperty (busProp, nullptr);

                if (device.isValid())
                    device.setProperty (te::IDs::name, te::DeviceManager::getDefaultAudioOutDeviceName (false), nullptr);

                continue;
            }

            const auto wanted = "track " + juce::String (bus->getAudioTrackNumber());

            if (device.isValid() && (device[te::IDs::name].toString() != wanted || track->getOutput().getDestinationTrack() != bus))
            {
                // Not undoable on purpose: this only restates what the bus ID already says.
                // Clearing first makes Tracktion look the number up again even if the text was right.
                device.setProperty (te::IDs::name, juce::String(), nullptr);
                device.setProperty (te::IDs::name, wanted, nullptr);
            }
        }

        // Tracktion's own name for each bus (it names the send plugins) follows its return's track name.
        // Derived here, never in an undo step: Tracktion writes these names without the undo manager
        const auto returns = getReturns();

        if (! returns.empty() && ! edit->state.getChildWithName ("AUXBUSNAMES").isValid())
            edit->state.getOrCreateChildWithName ("AUXBUSNAMES", nullptr);   // made outside any undo step, so undo can't detach it

        for (const auto& r : returns)
            if (edit->getAuxBusName (r.bus) != r.name.substring (0, 20))
                edit->setAuxBusName (r.bus, r.name);
    }

    //==============================================================================
    int Session::addReturnTrack (ReturnEffect effect, const juce::String& name)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            { fail ("No song is open"); return -1; }

        if (refuseWhileRecording ("adding an FX return"))
            return -1;

        const auto bus = findFreeBus();

        if (bus < 0)
            { fail ("A song can have up to " + juce::String (maxReturns) + " FX returns"); return -1; }

        // Make the plugins first: if one fails, the song is left exactly as it was. The step starts
        // now so their settings below are part of it (not of whatever was done last)
        beginUndoStep();
        auto returnPlugin = edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
        te::Plugin::Ptr fx;

        if (effect == ReturnEffect::reverb)       fx = edit->getPluginCache().createNewPlugin (te::ReverbPlugin::xmlTypeName, {});
        else if (effect == ReturnEffect::delay)   fx = edit->getPluginCache().createNewPlugin (te::DelayPlugin::xmlTypeName, {});

        auto* ret = dynamic_cast<te::AuxReturnPlugin*> (returnPlugin.get());

        if (ret == nullptr || (effect != ReturnEffect::none && fx == nullptr))
            { fail ("Could not make the FX return"); return -1; }

        ret->busNumber = bus;

        // 100% wet: the dry sound already reaches the mix through the sending track
        if (auto* reverb = dynamic_cast<te::ReverbPlugin*> (fx.get()))
        {
            reverb->dryValue = 0.0f;
            reverb->roomSizeValue = 0.6f;
        }
        else if (auto* delay = dynamic_cast<te::DelayPlugin*> (fx.get()))
        {
            delay->mixValue = 1.0f;
            delay->lengthMs = juce::jlimit (60, 2000, juce::roundToInt (60000.0 / juce::jmax (20.0, getBpm()) * 0.75));   // dotted 1/8
        }

        const auto base = name.trim().isNotEmpty() ? name.trim()
                        : effect == ReturnEffect::reverb ? juce::String ("Reverb")
                        : effect == ReturnEffect::delay ? juce::String ("Delay") : juce::String ("FX return");

        auto* track = appendTrack (makeUniqueTrackName (base), TrackKind::fxReturn);

        if (track == nullptr)
            { fail ("Could not add the FX return"); return -1; }

        track->pluginList.insertPlugin (returnPlugin, 0, nullptr);

        if (fx != nullptr)
        {
            int index = -1;

            if (auto* volume = track->getVolumePlugin())
                index = track->pluginList.indexOf (volume);

            track->pluginList.insertPlugin (fx, index, nullptr);
        }

        track->setSoloIsolate (true);   // soloing a track keeps its reverb
        syncBusRouting();                // names the bus after the return

        log ("Added FX return \"" + track->getName() + "\" on bus " + juce::String (bus + 1));
        notifyStateChanged();
        return getTrackIndex (track);
    }

    int Session::addBusTrack (const juce::String& name)
    {
        return makeBus ({}, name);
    }

    int Session::routeTracksToNewBus (const juce::Array<int>& trackIndexes)
    {
        return makeBus (trackIndexes, {});
    }

    int Session::makeBus (const juce::Array<int>& trackIndexes, const juce::String& name)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            { fail ("No song is open"); return -1; }

        if (refuseWhileRecording ("adding a bus"))
            return -1;

        juce::Array<te::AudioTrack*> sources;

        for (auto i : trackIndexes)
        {
            auto* t = getTrack (i);

            if (t == nullptr)
                { fail ("No such track"); return -1; }

            if (kindOf (*t) == TrackKind::fxReturn)
                { fail ("FX returns play into the main mix, not a bus"); return -1; }

            sources.addIfNotAlreadyThere (t);
        }

        // If every track already played into the same bus, the new bus plays into that one (nothing changes sound-wise)
        te::AudioTrack* commonBus = sources.isEmpty() ? nullptr : findBusTarget (*sources.getFirst());

        for (auto* t : sources)
            if (findBusTarget (*t) != commonBus)
                commonBus = nullptr;

        beginUndoStep();
        auto* bus = appendTrack (makeUniqueTrackName (name.trim().isNotEmpty() ? name.trim() : juce::String ("Bus")), TrackKind::bus);

        if (bus == nullptr)
            { fail ("Could not add the bus"); return -1; }

        if (commonBus != nullptr && ! routeIntoBus (*bus, *commonBus))
            log ("New bus plays into the main mix: " + lastError);

        for (auto* t : sources)
        {
            if (! routeIntoBus (*t, *bus))
            {
                const auto reason = lastError;
                edit->getUndoManager().undoCurrentTransactionOnly();   // all or nothing
                notifyStateChanged();
                { fail (reason); return -1; }
            }
        }

        log ("Added \"" + bus->getName() + "\"" + (sources.isEmpty() ? juce::String() : " with " + juce::String (sources.size()) + " track(s) in it"));
        notifyStateChanged();
        return getTrackIndex (bus);
    }

    std::vector<Session::ReturnInfo> Session::getReturns() const
    {
        std::vector<ReturnInfo> returns;

        if (edit != nullptr)
        {
            const auto tracks = te::getAudioTracks (*edit);

            for (int i = 0; i < tracks.size(); ++i)
                if (kindOf (*tracks[i]) == TrackKind::fxReturn)
                    if (auto* r = findReturnPlugin (*tracks[i]))
                        returns.push_back ({ i, r->busNumber.get(), tracks[i]->getName() });
        }

        return returns;
    }

    juce::Array<int> Session::getTracksIntoBus (int busTrackIndex) const
    {
        juce::Array<int> into;

        if (auto* bus = getTrack (busTrackIndex); bus != nullptr && kindOf (*bus) == TrackKind::bus)
        {
            const auto tracks = te::getAudioTracks (*edit);

            for (int i = 0; i < tracks.size(); ++i)
                if (findBusTarget (*tracks[i]) == bus)
                    into.add (i);
        }

        return into;
    }

    //==============================================================================
    bool Session::canSend (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);
        return track != nullptr && kindOf (*track) != TrackKind::fxReturn;
    }

    Session::SendInfo Session::getSend (int trackIndex, int bus) const
    {
        SendInfo info;

        if (auto* track = getTrack (trackIndex))
        {
            if (auto* send = findSend (*track, bus))
            {
                info.exists = true;
                // The stored value, not the parameter: that one catches up later (after an undo, say).
                // Automated: what the lane says now
                const auto position = send->gain->hasAutomationPoints() && isAutomationReadOn() ? send->gain->getCurrentValue() : send->gainLevel.get();
                info.levelDb = position <= 0.0f ? minVolumeDb : juce::jlimit (minVolumeDb, maxVolumeDb, te::volumeFaderPositionToDB (position));
                info.enabled = send->isEnabled();

                if (auto* volume = track->getVolumePlugin())
                {
                    auto parent = send->state.getParent();
                    info.preFader = parent.indexOf (send->state) < parent.indexOf (volume->state);
                }
            }
        }

        return info;
    }

    bool Session::setSendLevel (int trackIndex, int bus, float db, bool continuesGesture)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return fail ("No such track");

        if (! canSend (trackIndex))
            return fail ("FX returns don't have sends (they would feed back into themselves)");

        if (findReturnTrack (bus) == nullptr)
            return fail ("That FX return doesn't exist any more");

        auto* send = findSend (*track, bus);
        const bool created = send == nullptr;

        if (created && refuseWhileRecording ("adding a send"))
            return false;   // a new plugin rebuilds playback

        if (! continuesGesture)
            beginUndoStep();


        if (send == nullptr)
        {
            auto plugin = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
            send = dynamic_cast<te::AuxSendPlugin*> (plugin.get());

            if (send == nullptr)
                return fail ("Could not make the send");

            send->busNumber = bus;

            // Post-fader (the usual choice): straight after the track's fader
            int index = -1;

            if (auto* volume = track->getVolumePlugin())
                index = track->pluginList.indexOf (volume) + 1;

            track->pluginList.insertPlugin (plugin, index, nullptr);
            send = findSend (*track, bus);

            if (send == nullptr)
                return fail ("Could not make the send: is the track full? Remove an effect first");
        }

        if (refuseIfAutomated (send->gain.get(), "This send"))
            return false;

        const auto position = sendPositionFor (db);

        if (send->gainLevel.get() != position)
            send->gain->setParameter (position, juce::sendNotification);

        if (created)
            notifyStateChanged();

        return true;
    }

    bool Session::setSendPreFader (int trackIndex, int bus, bool preFader)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return fail ("No such track");

        if (refuseWhileRecording ("changing sends"))
            return false;

        beginUndoStep();

        if (findSend (*track, bus) == nullptr && ! setSendLevel (trackIndex, bus, minVolumeDb, true))
            return false;   // made silent, so choosing pre/post first never jumps the level

        auto* send = findSend (*track, bus);
        auto* volume = track->getVolumePlugin();

        if (send == nullptr || volume == nullptr)
            return fail ("This track has no fader to put the send before or after");

        auto parent = send->state.getParent();
        const auto from = parent.indexOf (send->state), fader = parent.indexOf (volume->state);

        // moveChild puts it AT the index: the fader's slot, which shifts the fader one way or the other
        if ((preFader && from > fader) || (! preFader && from < fader))
            parent.moveChild (from, fader, &edit->getUndoManager());

        notifyStateChanged();
        return getSend (trackIndex, bus).preFader == preFader || fail ("Could not move the send");
    }

    bool Session::setSendEnabled (int trackIndex, int bus, bool enabled)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* track = getTrack (trackIndex);
        auto* send = track != nullptr ? findSend (*track, bus) : nullptr;

        if (send == nullptr)
            return fail ("There is no send to turn on or off");

        if (refuseWhileRecording ("muting sends"))
            return false;   // switching a send rebuilds playback

        beginUndoStep();
        send->setEnabled (enabled);
        notifyStateChanged();
        return true;
    }

    bool Session::removeSend (int trackIndex, int bus)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* track = getTrack (trackIndex);
        auto* send = track != nullptr ? findSend (*track, bus) : nullptr;

        if (send == nullptr)
            return true;   // already none

        if (refuseWhileRecording ("removing sends"))
            return false;

        beginUndoStep();
        send->deleteFromParent();
        notifyStateChanged();
        return true;
    }

    //==============================================================================
    // Outputs (the plain choices live here too, so bus routing is all in one place)
    juce::Array<Session::RoutingChoice> Session::getTrackOutputChoices (int trackIndex) const
    {
        juce::Array<RoutingChoice> choices;
        choices.add ({ mainMixID, "Main mix" });

        if (auto* source = getTrack (trackIndex))
            for (auto* t : te::getAudioTracks (*edit))
                if (canPlayInto (*source, *t))
                    choices.add ({ busChoicePrefix + t->itemID.toString(), "Bus: " + t->getName() });

        for (auto& output : audioEngine.getOutputs())
            choices.add ({ output.id, "Direct to " + output.name });

        return choices;
    }

    juce::String Session::getTrackOutput (int trackIndex) const
    {
        if (auto* track = getTrack (trackIndex))
        {
            if (auto* bus = findBusTarget (*track))
                return busChoicePrefix + bus->itemID.toString();

            auto& output = track->getOutput();

            if (output.usesDefaultAudioOut())
                return mainMixID;

            // getOutputDeviceID() returns a NAME; our choices use the device ID
            if (auto* device = output.getOutputDevice (false))
                return device->getDeviceID();
        }

        return {};
    }

    bool Session::setTrackOutput (int trackIndex, const juce::String& choiceID)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return fail ("No such track");

        if (isRecording())
            return fail ("Stop recording before changing outputs");

        if (choiceID.startsWith (busChoicePrefix))
        {
            auto* bus = te::findAudioTrackForID (*edit, te::EditItemID::fromString (choiceID.fromFirstOccurrenceOf (busChoicePrefix, false, false)));

            if (bus == nullptr)
                return fail ("That bus doesn't exist any more");

            beginUndoStep();
            const bool ok = routeIntoBus (*track, *bus);
            notifyStateChanged();   // the bus strip lists what plays into it
            return ok;
        }

        bool knownChoice = choiceID == mainMixID;

        for (auto& output : audioEngine.getOutputs())
            knownChoice = knownChoice || output.id == choiceID;

        if (! knownChoice)
            return fail ("That output isn't available (is it enabled in Audio Setup?)");

        beginUndoStep();
        const bool wasOnBus = track->state.hasProperty (busProp);
        track->state.removeProperty (busProp, &edit->getUndoManager());

        if (choiceID == mainMixID)
            track->getOutput().setOutputToDefaultDevice (false);
        else
            track->getOutput().setOutputToDeviceID (choiceID);

        if (wasOnBus)
            notifyStateChanged();

        return getTrackOutput (trackIndex) == choiceID || fail ("The output could not be selected");
    }
}
