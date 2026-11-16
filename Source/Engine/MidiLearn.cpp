/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    MIDI controllers and MIDI clock out (see MidiLearn.h).
*/

#include "MidiLearn.h"
#include "AudioEngine.h"

namespace ez
{
    namespace
    {
        const juce::Identifier mapsID ("EZMIDIMAP"), mapID ("MAP"), channelID ("ch"), numberID ("num"), noteID ("note"),
                               pluginID ("plugin"), paramID ("param"), actionID ("action");

        constexpr juce::uint32 gestureGapMs = 600;   // moves closer together than this are one undo step
    }

    MidiLearn::MidiLearn (AudioEngine& e, Session& s)
        : audioEngine (e), session (s)
    {
        session.addListener (this);
        audioEngine.getEngine().getDeviceManager().addChangeListener (this);
        listenToInputs();
        attachToSong();
        startTimerHz (60);
    }

    MidiLearn::~MidiLearn()
    {
        stopTimer();
        detachFromSong();

        for (auto& d : listenedTo)
            if (auto* physical = dynamic_cast<te::PhysicalMidiInputDevice*> (d.get()))
                physical->removeListener (this);

        listenedTo.clear();
        audioEngine.getEngine().getDeviceManager().removeChangeListener (this);
        session.removeListener (this);
    }

    //==============================================================================
    void MidiLearn::listenToInputs()
    {
        // Held by shared_ptr: a device that disappears is still safe to stop listening to
        for (auto& d : listenedTo)
            if (auto* physical = dynamic_cast<te::PhysicalMidiInputDevice*> (d.get()))
                physical->removeListener (this);

        listenedTo.clear();

        for (auto& d : audioEngine.getEngine().getDeviceManager().getMidiInDevices())
        {
            if (auto* physical = dynamic_cast<te::PhysicalMidiInputDevice*> (d.get()))
            {
                physical->addListener (this);
                listenedTo.push_back (d);
            }
        }
    }

    juce::StringArray MidiLearn::getInputNames() const
    {
        juce::StringArray names;

        for (auto& d : listenedTo)
            names.add (d->getAliasOrName() + (d->isEnabled() ? juce::String() : juce::String (" (off)")));

        return names;
    }

    void MidiLearn::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        listenToInputs();
        changed();
    }

    void MidiLearn::attachToSong()
    {
        detachFromSong();

        if (auto* edit = session.getEdit())
        {
            attachedEdit = edit;
            edit->getParameterChangeHandler().addListener (*this);
        }

        changed();
    }

    void MidiLearn::detachFromSong()
    {
        if (attachedEdit != nullptr)
            attachedEdit->getParameterChangeHandler().removeListener (*this);

        attachedEdit = nullptr;
        touched = nullptr;   // belongs to the song
        learning = false;
        learningAction = {};
    }

    void MidiLearn::changed()
    {
        if (onChange != nullptr)
            onChange();
    }

    //==============================================================================
    void MidiLearn::handleIncomingMidiMessage (const juce::MidiMessage& m)
    {
        Incoming in {};

        if (m.isController())
            in = { m.getChannel(), m.getControllerNumber(), m.getControllerValue(), false };
        else if (m.isNoteOn())
            in = { m.getChannel(), m.getNoteNumber(), m.getVelocity(), true };
        else
            return;   // notes off, clock, pitch bend ... aren't for controls

        // Hand-over to the message thread; when full (a flood), the newest are dropped
        const juce::SpinLock::ScopedLockType lock (writeLock);
        const auto scope = fifo.write (1);

        if (scope.blockSize1 > 0)
            queue[(size_t) scope.startIndex1] = in;
        else if (scope.blockSize2 > 0)
            queue[(size_t) scope.startIndex2] = in;
    }

    void MidiLearn::handleForTest (const juce::MidiMessage& m)
    {
        handleIncomingMidiMessage (m);
        timerCallback();
    }

    void MidiLearn::timerCallback()
    {
        // A knob turned fast sends dozens of values: only the newest per control matters
        std::vector<Incoming> batch;
        const auto ready = fifo.getNumReady();
        const auto scope = fifo.read (ready);

        for (int i = 0; i < scope.blockSize1; ++i)  batch.push_back (queue[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)  batch.push_back (queue[(size_t) (scope.startIndex2 + i)]);

        for (size_t i = 0; i < batch.size(); ++i)
        {
            const auto& m = batch[i];
            bool newerFollows = false;

            for (size_t j = i + 1; j < batch.size() && ! m.isNote; ++j)
                newerFollows = newerFollows || (! batch[j].isNote && batch[j].channel == m.channel && batch[j].number == m.number);

            if (! newerFollows)
                apply (m);
        }
    }

    void MidiLearn::pluginParameterChanged (te::AutomatableParameter& p, bool isFollowingAutomation)
    {
        // Only what a person moves in EzDAWsIt, on the message thread
        if (! learning || isFollowingAutomation || learningAction.isNotEmpty() || ! juce::MessageManager::existsAndIsCurrentThread())
            return;

        if (touched.get() != &p)
        {
            touched = &p;
            session.showHint ("Now move a knob or fader on your controller for \"" + p.getPluginAndParamName() + "\"");
            changed();
        }
    }

    //==============================================================================
    juce::ValueTree MidiLearn::getState (bool createIfMissing) const
    {
        auto* edit = session.getEdit();

        if (edit == nullptr)
            return {};

        auto maps = edit->state.getChildWithName (mapsID);

        if (! maps.isValid() && createIfMissing)
        {
            maps = juce::ValueTree (mapsID);
            edit->state.appendChild (maps, nullptr);   // not an undo step: the links are settings
        }

        return maps;
    }

    te::AutomatableParameter::Ptr MidiLearn::findParameter (const juce::ValueTree& map) const
    {
        auto* edit = session.getEdit();

        if (edit == nullptr || ! map.hasProperty (pluginID))
            return {};

        const auto id = te::EditItemID::fromVar (map[pluginID]);

        if (auto plugin = te::findPluginForID (*edit, id))
            return plugin->getAutomatableParameterByID (map[paramID].toString());

        return {};
    }

    juce::String MidiLearn::describe (const juce::ValueTree& map) const
    {
        if (map.hasProperty (actionID))
            return map[actionID].toString();

        if (auto p = findParameter (map))
        {
            juce::String where;

            if (auto* track = dynamic_cast<te::Track*> (p->getTrack()))
                where = track->getName() + ": ";

            return where + p->getPluginAndParamName();
        }

        return "(a control that's gone)";
    }

    juce::Array<MidiLearn::Mapping> MidiLearn::getMappings() const
    {
        juce::Array<Mapping> list;

        for (auto map : getState (false))
            list.add ({ (int) map[channelID], (int) map[numberID], (bool) map[noteID], describe (map) });

        return list;
    }

    bool MidiLearn::removeMapping (int channel, int number, bool isNote)
    {
        auto state = getState (false);

        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            const auto map = state.getChild (i);

            if ((int) map[channelID] == channel && (int) map[numberID] == number && (bool) map[noteID] == isNote)
                return removeMapping (i);
        }

        return false;
    }

    bool MidiLearn::removeMapping (int index)
    {
        auto state = getState (false);

        if (! juce::isPositiveAndBelow (index, state.getNumChildren()))
            return false;

        state.removeChild (index, nullptr);
        changed();
        return true;
    }

    void MidiLearn::clearMappings()
    {
        if (auto state = getState (false); state.isValid())
            state.removeAllChildren (nullptr);

        changed();
    }

    //==============================================================================
    juce::StringArray MidiLearn::getActionNames()
    {
        return { "Play / stop", "Record", "Back to start", "Loop on/off", "Click on/off", "Undo", "Redo",
                 "Punch on/off", "Next take", "Previous take", "Tuner" };
    }

    void MidiLearn::startLearning()
    {
        if (session.getEdit() == nullptr)
            return;

        learning = true;
        learningAction = {};
        touched = nullptr;
        session.showHint ("MIDI learn: touch a control in EzDAWsIt, then move a knob or fader on your controller (Ctrl+Shift+L to stop)");
        changed();
    }

    void MidiLearn::startLearningAction (const juce::String& action)
    {
        if (session.getEdit() == nullptr || ! getActionNames().contains (action))
            return;

        learning = true;
        learningAction = action;
        touched = nullptr;
        session.showHint ("MIDI learn: press the pad or button that should do \"" + action + "\"");
        changed();
    }

    void MidiLearn::stopLearning()
    {
        learning = false;
        learningAction = {};
        touched = nullptr;
        changed();
    }

    juce::String MidiLearn::getLearnStatus() const
    {
        if (! learning)                    return {};
        if (learningAction.isNotEmpty())   return "Press a pad or button for \"" + learningAction + "\"";
        if (touched == nullptr)            return "Touch a control in EzDAWsIt (a fader, a knob ...)";
        return "Now move a knob or fader on your controller for \"" + touched->getPluginAndParamName() + "\"";
    }

    void MidiLearn::apply (const Incoming& m)
    {
        auto state = getState (learning);   // made only when a link is about to be saved

        if (! state.isValid())
            return;

        const auto matches = [&m] (const juce::ValueTree& map)
        {
            return (int) map[channelID] == m.channel && (int) map[numberID] == m.number && (bool) map[noteID] == m.isNote;
        };

        if (learning)
        {
            const bool isAction = learningAction.isNotEmpty();
            const bool pressed = m.isNote || m.value >= 64;

            if ((isAction && ! pressed) || (! isAction && (m.isNote || touched == nullptr)))
                return;   // waiting for the right kind of control

            // One control, one job: the new link replaces any old one on the same knob or pad
            for (int i = state.getNumChildren(); --i >= 0;)
                if (matches (state.getChild (i)))
                    state.removeChild (i, nullptr);

            juce::ValueTree map (mapID);
            map.setProperty (channelID, m.channel, nullptr)
               .setProperty (numberID, m.number, nullptr)
               .setProperty (noteID, m.isNote, nullptr);

            if (isAction)
            {
                map.setProperty (actionID, learningAction, nullptr);
            }
            else
            {
                auto* plugin = touched->getPlugin();

                if (plugin == nullptr)
                {
                    session.showHint ("That control can't be linked (it isn't part of a track or effect): touch another one");
                    touched = nullptr;
                    changed();
                    return;
                }

                map.setProperty (pluginID, plugin->itemID.toVar(), nullptr)
                   .setProperty (paramID, touched->paramID, nullptr);
            }

            state.appendChild (map, nullptr);
            session.showHint ("Linked " + juce::String (m.isNote ? "pad " : "knob ") + juce::String (m.number)
                              + " (channel " + juce::String (m.channel) + ") to " + describe (map));
            learning = false;
            learningAction = {};
            touched = nullptr;
            changed();
            return;
        }

        for (auto map : state)
        {
            if (! matches (map))
                continue;

            if (map.hasProperty (actionID))
            {
                // A button: once per press (a CC button sends 127 then 0)
                auto& last = lastValues[(m.channel << 16) | (m.isNote ? 0x8000 : 0) | m.number];
                const auto before = last;
                last = m.value;

                if ((m.isNote ? m.value > 0 : (m.value >= 64 && before < 64)) && performAction != nullptr)
                    performAction (map[actionID].toString());
            }
            else if (auto p = findParameter (map))
            {
                const auto now = juce::Time::getMillisecondCounter();

                if (now - lastMoveTime > gestureGapMs)
                    session.beginUndoStep();   // one turn of a knob: one undo step

                lastMoveTime = now;
                p->setNormalisedParameter ((float) m.value / 127.0f, juce::sendNotification);
            }
        }
    }

    //==============================================================================
    juce::Array<MidiLearn::ClockOutput> MidiLearn::getClockOutputs() const
    {
        juce::Array<ClockOutput> list;
        auto& dm = audioEngine.getEngine().getDeviceManager();

        for (int i = 0; i < dm.getNumMidiOutDevices(); ++i)
            if (auto* out = dm.getMidiOutDevice (i))
                list.add ({ out->getDeviceID(), out->getAliasOrName(), out->isEnabled() && out->isSendingClock() });

        return list;
    }

    bool MidiLearn::setSendingClock (const juce::String& outputID, bool shouldSend)
    {
        auto& dm = audioEngine.getEngine().getDeviceManager();

        for (int i = 0; i < dm.getNumMidiOutDevices(); ++i)
        {
            if (auto* out = dm.getMidiOutDevice (i); out != nullptr && out->getDeviceID() == outputID)
            {
                if (shouldSend && ! out->isEnabled())
                    out->setEnabled (true);

                out->setSendingClock (shouldSend);   // MIDI clock carries start, stop and the tempo
                session.showHint (shouldSend ? out->getAliasOrName() + " follows the tempo, play and stop (MIDI clock)"
                                             : out->getAliasOrName() + ": MIDI clock off");
                changed();
                return true;
            }
        }

        return false;
    }

    bool MidiLearn::isSendingClockAnywhere() const
    {
        for (const auto& o : getClockOutputs())
            if (o.sending)
                return true;

        return false;
    }
}
