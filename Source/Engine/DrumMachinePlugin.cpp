/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "DrumMachinePlugin.h"

namespace ez
{
    namespace
    {
        constexpr double maxSampleSeconds = 30.0;   // longer files are cut: a pad is for hits, not songs
        constexpr const char* kitPrefix = "kit:";

        float velocityGain (int velocity)
        {
            return std::pow (juce::jlimit (1, 127, velocity) / 127.0f, 1.5f);
        }
    }

    const char* DrumMachinePlugin::xmlTypeName = "ezdrummachine";

    const juce::Identifier DrumMachinePlugin::kitID ("kit"), DrumMachinePlugin::padType ("PAD"), DrumMachinePlugin::nameID ("name"),
                           DrumMachinePlugin::fileID ("file"), DrumMachinePlugin::tuneID ("tune"), DrumMachinePlugin::decayID ("decay"),
                           DrumMachinePlugin::startID ("start"), DrumMachinePlugin::panID ("pan"), DrumMachinePlugin::levelID ("level"),
                           DrumMachinePlugin::chokeID ("choke"), DrumMachinePlugin::muteID ("mute"), DrumMachinePlugin::soloID ("solo"),
                           DrumMachinePlugin::reverseID ("reverse");

    //==============================================================================
    DrumMachinePlugin::DrumMachinePlugin (te::PluginCreationInfo info) : te::Plugin (info)
    {
        formats.registerBasicFormats();
        ensurePads();
        rebuildSnapshot();
    }

    DrumMachinePlugin::~DrumMachinePlugin()
    {
        cancelPendingUpdate();
        notifyListenersOfDeletion();
    }

    //==============================================================================
    juce::Identifier DrumMachinePlugin::idFor (Value v)
    {
        switch (v)
        {
            case Value::tune:   return tuneID;
            case Value::decay:  return decayID;
            case Value::start:  return startID;
            case Value::pan:    return panID;
            case Value::level:  return levelID;
        }

        return tuneID;
    }

    double DrumMachinePlugin::defaultFor (Value v)
    {
        return v == Value::decay ? 1.0 : 0.0;
    }

    double DrumMachinePlugin::clampValue (Value v, double x)
    {
        switch (v)
        {
            case Value::tune:   return juce::jlimit (minTune, maxTune, x);
            case Value::decay:  return juce::jlimit (minDecay, maxDecay, x);
            case Value::start:  return juce::jlimit (minStart, maxStart, x);
            case Value::pan:    return juce::jlimit (-1.0, 1.0, x);
            case Value::level:  return juce::jlimit (minLevel, maxLevel, x);
        }

        return x;
    }

    void DrumMachinePlugin::ensurePads()
    {
        int count = 0;

        for (auto child : state)
            if (child.hasType (padType))
                ++count;

        for (int pad = count; pad < numPads; ++pad)
        {
            juce::ValueTree p (padType);
            p.setProperty (nameID, DrumKits::getPadName (pad), nullptr);
            p.setProperty (chokeID, DrumKits::getDefaultChokeGroup (pad), nullptr);
            state.appendChild (p, nullptr);
        }
    }

    juce::ValueTree DrumMachinePlugin::getPadState (int pad) const
    {
        int index = 0;

        for (auto child : state)
            if (child.hasType (padType))
                if (index++ == pad)
                    return child;

        return {};
    }

    DrumMachinePlugin::PadSettings DrumMachinePlugin::getPad (int pad) const
    {
        PadSettings s;
        const auto p = getPadState (pad);

        if (! p.isValid())
            return s;

        s.name = p.getProperty (nameID, DrumKits::getPadName (pad)).toString();
        s.file = p.getProperty (fileID).toString();
        s.tune = clampValue (Value::tune, p.getProperty (tuneID, 0.0));
        s.decay = clampValue (Value::decay, p.getProperty (decayID, 1.0));
        s.start = clampValue (Value::start, p.getProperty (startID, 0.0));
        s.pan = clampValue (Value::pan, p.getProperty (panID, 0.0));
        s.level = clampValue (Value::level, p.getProperty (levelID, 0.0));
        s.choke = juce::jlimit (0, maxChokeGroup, (int) p.getProperty (chokeID, 0));
        s.muted = p.getProperty (muteID, false);
        s.soloed = p.getProperty (soloID, false);
        s.reversed = p.getProperty (reverseID, false);
        return s;
    }

    juce::String DrumMachinePlugin::getKitName() const
    {
        return state.getProperty (kitID).toString();
    }

    juce::String DrumMachinePlugin::makeKitSource (int kitIndex, int pad)
    {
        return kitPrefix + juce::String (DrumKits::getKit (kitIndex).name) + ":" + juce::String (pad);
    }

    juce::String DrumMachinePlugin::describeSource (const juce::String& source)
    {
        if (source.startsWith (kitPrefix))
        {
            const auto rest = source.substring ((int) std::strlen (kitPrefix));
            return rest.upToLastOccurrenceOf (":", false, false) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + DrumKits::getPadName (rest.fromLastOccurrenceOf (":", false, false).getIntValue());
        }

        return source.isEmpty() ? juce::String ("No sound") : source.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
    }

    juce::File DrumMachinePlugin::resolveSource (const juce::String& source, juce::String& error) const
    {
        if (source.isEmpty())
            return {};

        if (source.startsWith (kitPrefix))
        {
            const auto rest = source.substring ((int) std::strlen (kitPrefix));
            const auto kit = DrumKits::findKit (rest.upToLastOccurrenceOf (":", false, false));

            if (kit < 0)
            {
                error = "Unknown kit sound \"" + source + "\"";
                return {};
            }

            return DrumKits::getSampleFile (kit, rest.fromLastOccurrenceOf (":", false, false).getIntValue(), error);
        }

        if (juce::File::isAbsolutePath (source))
            return juce::File (source);

        // A file kept with the song: relative to the song's folder, so the song can move
        if (edit.editFileRetriever)
            return edit.editFileRetriever().getParentDirectory().getChildFile (source);

        error = "The song has no folder yet";
        return {};
    }

    //==============================================================================
    void DrumMachinePlugin::loadSampleIfChanged (int pad, const juce::String& source)
    {
        auto& slot = loaded[(size_t) pad];

        // A sound that failed is tried again on every change (the file may be back)
        if (slot.file == source && (slot.audio != nullptr || source.isEmpty()))
            return;

        slot = {};
        slot.file = source;
        slot.generation = nextGeneration++;

        if (source.isEmpty())
            return;

        const auto file = resolveSource (source, slot.error);

        if (slot.error.isNotEmpty())
            return;

        if (! file.existsAsFile())
        {
            slot.error = "Missing file: " + file.getFullPathName();
            return;
        }

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        {
            slot.error = "Not an audio file this app can read: " + file.getFileName();
            return;
        }

        const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * maxSampleSeconds));
        const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        auto buffer = std::make_shared<juce::AudioBuffer<float>> (channels, length);

        if (! reader->read (buffer.get(), 0, length, 0, true, channels > 1))
        {
            slot.error = "Could not read " + file.getFileName();
            return;
        }

        slot.audio = std::move (buffer);
        slot.sampleRate = reader->sampleRate;
    }

    void DrumMachinePlugin::rebuildSnapshot()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        {
            std::unique_ptr<Snapshot> old;   // freed here, after the lock
            const juce::SpinLock::ScopedLockType sl (retiredLock);
            old = std::move (retired);
        }

        auto next = std::make_unique<Snapshot>();
        bool anySolo = false;
        std::array<PadSettings, numPads> settings;

        for (int pad = 0; pad < numPads; ++pad)
        {
            settings[(size_t) pad] = getPad (pad);
            anySolo = anySolo || settings[(size_t) pad].soloed;
        }

        for (int pad = 0; pad < numPads; ++pad)
        {
            const auto& s = settings[(size_t) pad];
            loadSampleIfChanged (pad, s.file);
            const auto& slot = loaded[(size_t) pad];
            auto& p = next->pads[(size_t) pad];

            p.audio = slot.audio;
            p.sourceSampleRate = slot.sampleRate;
            p.generation = slot.generation;
            p.tuneRatio = (float) std::pow (2.0, s.tune / 12.0);
            const auto gain = juce::Decibels::decibelsToGain ((float) s.level, (float) minLevel);
            p.gainL = gain * (s.pan > 0.0 ? (float) (1.0 - s.pan) : 1.0f);
            p.gainR = gain * (s.pan < 0.0 ? (float) (1.0 + s.pan) : 1.0f);
            p.decay = (float) s.decay;
            p.start = (float) s.start;
            p.choke = s.choke;
            p.reversed = s.reversed;
            p.playable = ! s.muted && (! anySolo || s.soloed);
        }

        std::unique_ptr<Snapshot> unused;   // a snapshot the audio thread never took: freed here

        {
            const juce::SpinLock::ScopedLockType sl (pendingLock);
            unused = std::move (pending);
            pending = std::move (next);
        }
    }

    std::shared_ptr<const juce::AudioBuffer<float>> DrumMachinePlugin::getPadAudio (int pad) const
    {
        return juce::isPositiveAndBelow (pad, numPads) ? loaded[(size_t) pad].audio : nullptr;
    }

    juce::String DrumMachinePlugin::getPadLoadError (int pad) const
    {
        return juce::isPositiveAndBelow (pad, numPads) ? loaded[(size_t) pad].error : juce::String();
    }

    juce::uint32 DrumMachinePlugin::getPadHitCount (int pad) const noexcept
    {
        return juce::isPositiveAndBelow (pad, numPads) ? hits[(size_t) pad].load (std::memory_order_relaxed) : 0u;
    }

    void DrumMachinePlugin::flushPendingUpdates()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        handleUpdateNowIfNeeded();
    }

    void DrumMachinePlugin::valueTreeChanged()
    {
        needsRebuild = true;
        triggerAsyncUpdate();
        te::Plugin::valueTreeChanged();
    }

    void DrumMachinePlugin::handleAsyncUpdate()
    {
        if (needsRebuild.exchange (false))
        {
            ensurePads();   // e.g. undo of a load that replaced the whole state
            rebuildSnapshot();
        }
        else
        {
            std::unique_ptr<Snapshot> old;   // the audio thread asked us to free what it swapped out
            const juce::SpinLock::ScopedLockType sl (retiredLock);
            old = std::move (retired);
        }
    }

    void DrumMachinePlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        te::copyValueTree (state, v, getUndoManager());
        ensurePads();
    }

    bool DrumMachinePlugin::hasNameForMidiNoteNumber (int note, int, juce::String& name)
    {
        const auto pad = padForNote (note);

        if (pad < 0)
            return false;

        name = getPad (pad).name;
        return true;
    }

    //==============================================================================
    void DrumMachinePlugin::initialise (const te::PluginInitialisationInfo&)
    {
        for (auto& v : voices)
            v.active = false;
    }

    void DrumMachinePlugin::deinitialise()
    {
        for (auto& v : voices)
            v.active = false;
    }

    void DrumMachinePlugin::startVoice (int pad, int velocity, int delay, const Snapshot& snapshot)
    {
        const auto& sound = snapshot.pads[(size_t) pad];

        if (! sound.playable || sound.audio == nullptr || sound.audio->getNumSamples() < 2)
            return;

        // Choke: this pad cuts off every pad in its group (including itself), from the moment it plays
        if (sound.choke > 0)
            for (auto& v : voices)
                if (v.active && ! v.releasing && snapshot.pads[(size_t) v.pad].choke == sound.choke)
                {
                    v.chokeAt = v.chokeAt >= 0 ? juce::jmin (v.chokeAt, delay) : delay;
                    v.releaseStep = 1.0f / (float) juce::jmax (1.0, 0.005 * sampleRate);
                }

        // A free voice, or the oldest one
        Voice* voice = nullptr;

        for (auto& v : voices)
            if (! v.active)
            {
                voice = &v;
                break;
            }

        if (voice == nullptr)
        {
            voice = &voices[0];

            for (auto& v : voices)
                if (v.age < voice->age)
                    voice = &v;
        }

        const auto length = (double) sound.audio->getNumSamples();
        auto& v = *voice;
        v = {};
        v.active = true;
        v.pad = pad;
        v.delay = delay;
        v.generation = sound.generation;
        v.first = sound.start * (length - 1.0);
        v.last = v.first + (length - 1.0 - v.first) * sound.decay;
        v.position = 0.0;
        v.increment = sound.sourceSampleRate / sampleRate * sound.tuneRatio;

        // Shortened sounds fade out over their last 15% (at least 5 ms); full ones just avoid a click
        const auto span = v.last - v.first;
        const auto fadeLength = sound.decay < 0.999f ? juce::jmax (0.005 * sound.sourceSampleRate, span * 0.15)
                                                     : 0.002 * sound.sourceSampleRate;
        v.fadeFrom = juce::jmax (0.0, span - fadeLength);

        const auto g = velocityGain (velocity);
        v.gainL = g * sound.gainL;
        v.gainR = g * sound.gainR;
        v.age = ++voiceAge;
        hits[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
    }

    void DrumMachinePlugin::renderVoice (Voice& v, const PadSound& sound, juce::AudioBuffer<float>& out, int startSample, int numSamples)
    {
        const auto& audio = *sound.audio;
        const auto* left = audio.getReadPointer (0);
        const auto* right = audio.getReadPointer (juce::jmin (1, audio.getNumChannels() - 1));
        const auto lastIndex = audio.getNumSamples() - 1;
        auto* outL = out.getWritePointer (0, startSample);
        auto* outR = out.getNumChannels() > 1 ? out.getWritePointer (1, startSample) : nullptr;
        const auto span = v.last - v.first;
        const auto fadeLength = span - v.fadeFrom;

        int i = juce::jmin (v.delay, numSamples);
        v.delay -= i;

        for (; i < numSamples; ++i)
        {
            if (v.chokeAt >= 0 && i >= v.chokeAt)
            {
                v.releasing = true;
                v.chokeAt = -1;
            }

            if (v.position >= span || (v.releasing && v.release <= 0.0f))
            {
                v.active = false;
                return;
            }

            auto index = v.first + v.position;

            if (sound.reversed)
                index = v.last - v.position;   // the same part, backwards

            const auto i0 = juce::jlimit (0, lastIndex, (int) index);
            const auto i1 = juce::jmin (lastIndex, i0 + 1);
            const auto frac = (float) (index - (double) i0);

            auto amp = 1.0f;

            if (v.position > v.fadeFrom && fadeLength > 0.0)
                amp = (float) ((span - v.position) / fadeLength);

            if (v.releasing)
            {
                amp *= v.release;
                v.release -= v.releaseStep;
            }

            const auto l = left[i0] + (left[i1] - left[i0]) * frac;
            const auto r = right[i0] + (right[i1] - right[i0]) * frac;
            outL[i] += l * amp * v.gainL;

            if (outR != nullptr)
                outR[i] += r * amp * v.gainR;

            v.position += v.increment;
        }

        if (v.chokeAt >= 0)   // the choking hit was in this block: it has happened by now
        {
            v.releasing = true;
            v.chokeAt = -1;
        }
    }

    void DrumMachinePlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr)
            return;

        SCOPED_REALTIME_CHECK

        auto& out = *fc.destBuffer;
        out.clear (fc.bufferStartSample, fc.bufferNumSamples);

        // Take the newest settings, if the message thread has made some and isn't mid-swap
        {
            const juce::SpinLock::ScopedTryLockType tryPending (pendingLock);

            if (tryPending.isLocked() && pending != nullptr)
            {
                const juce::SpinLock::ScopedTryLockType tryRetired (retiredLock);

                if (tryRetired.isLocked() && retired == nullptr)   // never free memory here: hand the old one back
                {
                    retired = std::move (live);
                    live = std::move (pending);
                    triggerAsyncUpdate();   // the message thread frees it (and then a later swap isn't blocked)
                }
            }
        }

        if (live == nullptr || out.getNumChannels() < 1)
            return;

        const auto& snapshot = *live;

        // A voice whose pad got a new sample stops (it must not read the old one)
        for (auto& v : voices)
            if (v.active && v.generation != snapshot.pads[(size_t) v.pad].generation)
                v.active = false;

        if (auto* midi = fc.bufferForMidiMessages)
        {
            if (midi->isAllNotesOff)
                for (auto& v : voices)
                    v.active = false;

            for (auto& m : *midi)
            {
                if (m.isNoteOn())
                {
                    const auto pad = padForNote (m.getNoteNumber());

                    if (pad >= 0)
                        startVoice (pad, m.getVelocity(), juce::jlimit (0, juce::jmax (0, fc.bufferNumSamples - 1),
                                                                        juce::roundToInt (m.getTimeStamp() * sampleRate)), snapshot);
                }
                else if (m.isAllNotesOff() || m.isAllSoundOff())
                {
                    for (auto& v : voices)
                        v.active = false;
                }
                // Note-offs are ignored: a drum hit plays to its end (or its decay)
            }
        }

        for (auto& v : voices)
            if (v.active)
                renderVoice (v, snapshot.pads[(size_t) v.pad], out, fc.bufferStartSample, fc.bufferNumSamples);
    }
}
