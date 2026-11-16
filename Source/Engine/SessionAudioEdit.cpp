/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Session: audio clip edits (fades, gain, reverse, normalise, transpose, follow the tempo),
    ducking (sidechain compression) and tap tempo. See Session.h.
*/

#include "Session.h"

namespace ez
{
    namespace
    {
        te::AudioClipBase* asAudio (te::Clip& clip)    { return dynamic_cast<te::AudioClipBase*> (&clip); }

        const juce::Identifier duckProp ("ezDuck");   // marks the compressor that ducking added

        /** Does `track`'s sound depend on `on` (plays into it, sends to it, or is keyed by it), at any distance?
            Ducking `on` under `track` would then be a loop: the audio graph can't be built (it recurses forever). */
        bool dependsOn (te::Edit& edit, te::AudioTrack* track, te::AudioTrack* on)
        {
            const auto tracks = te::getAudioTracks (edit);
            std::vector<te::AudioTrack*> toVisit { track }, seen;

            while (! toVisit.empty())
            {
                auto* t = toVisit.back();
                toVisit.pop_back();

                if (t == nullptr || std::find (seen.begin(), seen.end(), t) != seen.end())
                    continue;

                if (t == on)
                    return true;

                seen.push_back (t);

                juce::Array<int> returnsHere;

                for (auto* p : t->pluginList)
                {
                    if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (p))
                        returnsHere.add (r->busNumber.get());

                    if (p->getSidechainSourceID().isValid())
                        for (auto* source : tracks)
                            if (source->itemID == p->getSidechainSourceID())
                                toVisit.push_back (source);
                }

                for (auto* source : tracks)
                {
                    // What plays into it: its bus inputs, and what sends to its return
                    if (source->getOutput().getDestinationTrack() == t)
                        toVisit.push_back (source);

                    if (! returnsHere.isEmpty())
                        for (auto* p : source->pluginList)
                            if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p); send != nullptr && returnsHere.contains (send->getBusNumber()))
                                toVisit.push_back (source);
                }
            }

            return false;
        }
    }

    //==============================================================================
    double Session::getClipFadeIn (te::Clip& clip) const
    {
        auto* a = asAudio (clip);
        return a != nullptr ? a->getFadeIn().inSeconds() : 0.0;
    }

    double Session::getClipFadeOut (te::Clip& clip) const
    {
        auto* a = asAudio (clip);
        return a != nullptr ? a->getFadeOut().inSeconds() : 0.0;
    }

    bool Session::setClipFades (te::Clip& clip, double fadeIn, double fadeOut)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* a = asAudio (clip);

        if (a == nullptr)
            return fail ("Fades are for audio clips");

        if (refuseWhileRecording ("changing fades"))
            return false;

        // Together they fit in the clip (Tracktion would otherwise shorten the other one)
        const auto length = clip.getPosition().getLength().inSeconds();
        fadeIn = juce::jlimit (0.0, length, fadeIn);
        fadeOut = juce::jlimit (0.0, juce::jmax (0.0, length - fadeIn), fadeOut);

        if (! batchEdit)
            beginUndoStep();
        a->setFadeIn (te::TimeDuration::fromSeconds (fadeIn));
        a->setFadeOut (te::TimeDuration::fromSeconds (fadeOut));
        notifyStateChanged();
        return true;
    }

    float Session::getClipGainDb (te::Clip& clip) const
    {
        auto* a = asAudio (clip);
        return a != nullptr ? a->getGainDB() : 0.0f;
    }

    bool Session::setClipGainDb (te::Clip& clip, float db)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* a = asAudio (clip);

        if (a == nullptr)
            return fail ("Clip gain is for audio clips (MIDI clips: change the velocities)");

        if (refuseWhileRecording ("changing clip gain"))
            return false;

        if (! batchEdit)
            beginUndoStep();
        a->setGainDB (juce::jlimit (-48.0f, 24.0f, db));
        notifyStateChanged();
        return true;
    }

    bool Session::isClipReversed (te::Clip& clip) const
    {
        auto* a = asAudio (clip);
        return a != nullptr && a->getIsReversed();
    }

    bool Session::setClipReversed (te::Clip& clip, bool reversed)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* a = asAudio (clip);

        if (a == nullptr)
            return fail ("Only audio clips can be reversed");

        if (refuseWhileRecording ("reversing a clip"))
            return false;

        if (! batchEdit)
            beginUndoStep();
        a->setIsReversed (reversed);   // Tracktion renders a reversed copy in the background
        notifyStateChanged();
        return true;
    }

    bool Session::normaliseClip (te::Clip& clip, float peakDb)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* a = asAudio (clip);

        if (a == nullptr)
            return fail ("Normalise is for audio clips");

        if (refuseWhileRecording ("normalising"))
            return false;

        const auto file = a->getOriginalFile();
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr || reader->sampleRate <= 0)
            return fail ("Could not read " + file.getFileName());

        // The part of the file this clip plays (before any reversing or stretching: the peak is the same)
        const auto position = clip.getPosition();
        const auto speed = juce::jmax (0.01, a->getSpeedRatio());
        const auto start = (juce::int64) (position.getOffset().inSeconds() * speed * reader->sampleRate);
        const auto count = (juce::int64) (position.getLength().inSeconds() * speed * reader->sampleRate);
        const auto from = juce::jlimit ((juce::int64) 0, reader->lengthInSamples, start);
        const auto num = juce::jlimit ((juce::int64) 0, reader->lengthInSamples - from, count);

        if (num <= 0)
            return fail ("The clip plays none of its file");

        juce::Range<float> ranges[8];
        const auto channels = juce::jmin (8, (int) reader->numChannels);
        reader->readMaxLevels (from, num, ranges, channels);
        float peak = 0.0f;

        for (int c = 0; c < channels; ++c)
            peak = juce::jmax (peak, std::abs (ranges[c].getStart()), std::abs (ranges[c].getEnd()));

        if (peak < 1.0e-5f)
            return fail ("The clip is silent: nothing to normalise");

        const auto gain = juce::jlimit (-48.0f, 24.0f, peakDb - juce::Decibels::gainToDecibels (peak));

        if (! batchEdit)
            beginUndoStep();
        a->setGainDB (gain);
        notifyStateChanged();
        showHint ("Normalised: clip gain " + juce::String (gain >= 0 ? "+" : "") + juce::String (gain, 1) + " dB (its loudest peak at "
                  + juce::String (peakDb, 1) + " dB)");
        return true;
    }

    float Session::getClipTranspose (te::Clip& clip) const
    {
        auto* a = asAudio (clip);
        return a != nullptr ? a->getPitchChange() : 0.0f;
    }

    bool Session::setClipTranspose (te::Clip& clip, float semitones)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* a = asAudio (clip);

        if (a == nullptr)
            return fail ("Transpose is for audio clips (MIDI clips: move the notes in the piano roll)");

        if (refuseWhileRecording ("transposing"))
            return false;

        if (! batchEdit)
            beginUndoStep();

        if (a->getTimeStretchMode() == te::TimeStretcher::disabled)
            a->setTimeStretchMode (te::TimeStretcher::defaultMode);

        a->setPitchChange (juce::jlimit (-24.0f, 24.0f, semitones));
        notifyStateChanged();
        return true;
    }

    bool Session::isClipFollowingTempo (te::Clip& clip) const
    {
        auto* a = asAudio (clip);
        return a != nullptr && a->getAutoTempo();
    }

    double Session::guessLoopBpm (double lengthSeconds, double songBpm, const juce::String& fileName)
    {
        // "Drum Loop 90bpm", "90 BPM", "loop_174_bpm"
        const auto lower = fileName.toLowerCase();

        if (const auto at = lower.indexOf ("bpm"); at > 0)
        {
            auto before = lower.substring (0, at).trimEnd().trimCharactersAtEnd ("_- ");
            int digits = 0;

            while (digits < before.length() && juce::CharacterFunctions::isDigit (before[before.length() - 1 - digits]))
                ++digits;

            if (const auto bpm = before.getLastCharacters (digits).getDoubleValue(); bpm >= 40.0 && bpm <= 300.0)
                return bpm;
        }

        // Its length in whole bars of 4 (1, 2, 4, 8 or 16), whichever lands nearest the song's tempo
        if (lengthSeconds <= 0.0)
            return songBpm;

        double best = songBpm, bestDistance = 1.0e9;

        for (int bars : { 1, 2, 4, 8, 16 })
        {
            const auto bpm = bars * 4 * 60.0 / lengthSeconds;

            if (bpm < 50.0 || bpm > 220.0)
                continue;

            if (const auto distance = std::abs (std::log2 (bpm / songBpm)); distance < bestDistance)
            {
                best = bpm;
                bestDistance = distance;
            }
        }

        return best;
    }

    bool Session::setClipFollowsTempo (te::Clip& clip, bool follow)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* a = asAudio (clip);

        if (a == nullptr)
            return fail ("MIDI clips always follow the tempo");

        if (refuseWhileRecording ("changing a clip's tempo"))
            return false;

        if (! batchEdit)
            beginUndoStep();

        if (follow)
        {
            const auto info = a->getAudioFile().getInfo();
            const auto fileSeconds = info.sampleRate > 0 ? (double) info.lengthInSamples / info.sampleRate : 0.0;
            auto& loop = a->getLoopInfo();

            if (loop.getBpm (info) < 40.0 || loop.getBpm (info) > 300.0)
                loop.setBpm (guessLoopBpm (fileSeconds, getBpm(), a->getOriginalFile().getFileNameWithoutExtension()), info);

            if (a->getTimeStretchMode() == te::TimeStretcher::disabled)
                a->setTimeStretchMode (te::TimeStretcher::defaultMode);

            a->setAutoTempo (true);
            showHint ("Follows the song's tempo (its own: " + juce::String (loop.getBpm (info), 1) + " BPM)");
        }
        else
        {
            a->setAutoTempo (false);
        }

        notifyStateChanged();
        return true;
    }

    //==============================================================================
    int Session::getDuckingTrigger (int trackIndex) const
    {
        if (auto* track = getTrack (trackIndex))
            for (auto* p : track->pluginList)
                if (auto* comp = dynamic_cast<te::CompressorPlugin*> (p); comp != nullptr && comp->state.getProperty (duckProp, false)
                     && comp->useSidechainTrigger.get())
                    for (int i = 0; i < getNumAudioTracks(); ++i)
                        if (auto* t = getTrack (i); t != nullptr && t->itemID == comp->getSidechainSourceID())
                            return i;

        return -1;
    }

    bool Session::duckUnder (int trackIndex, int triggerTrack)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* track = getTrack (trackIndex);
        auto* trigger = getTrack (triggerTrack);

        if (track == nullptr || trigger == nullptr)
            return fail ("No such track");

        if (trackIndex == triggerTrack)
            return fail ("A track can't duck under itself");

        if (dependsOn (*edit, trigger, track))
            return fail ("\"" + trigger->getName() + "\" already depends on \"" + track->getName()
                         + "\" (it plays into it, or ducks under it): ducking this way round would make a loop");

        if (refuseWhileRecording ("setting up ducking"))
            return false;

        beginUndoStep();
        te::CompressorPlugin* comp = nullptr;

        for (auto* p : track->pluginList)
            if (auto* c = dynamic_cast<te::CompressorPlugin*> (p); c != nullptr && c->state.getProperty (duckProp, false))
                comp = c;

        if (comp == nullptr)
        {
            auto plugin = edit->getPluginCache().createNewPlugin (te::CompressorPlugin::xmlTypeName, {});
            const auto before = track->pluginList.size();
            track->pluginList.insertPlugin (plugin, effectInsertIndex (*track), nullptr);
            comp = dynamic_cast<te::CompressorPlugin*> (plugin.get());

            if (comp == nullptr || track->pluginList.size() != before + 1)
            {
                edit->getUndoManager().undoCurrentTransactionOnly();
                return fail ("This track is full: remove an effect first");
            }

            comp->state.setProperty (duckProp, true, &edit->getUndoManager());

            // Quick to duck, back in time for the next beat
            comp->thresholdGain.setParameter (juce::Decibels::decibelsToGain (-24.0f), juce::sendNotification);
            comp->ratio.setParameter (0.85f, juce::sendNotification);   // strong
            comp->attackMs.setParameter (1.0f, juce::sendNotification);
            comp->releaseMs.setParameter (juce::jlimit (60.0f, 300.0f, (float) (60000.0 / juce::jmax (40.0, getBpm()) * 0.4)), juce::sendNotification);
        }

        comp->setSidechainSourceID (trigger->itemID);
        comp->useSidechainTrigger = true;
        comp->guessSidechainRouting();
        notifyStateChanged();
        showHint ("\"" + track->getName() + "\" ducks each time \"" + trigger->getName() + "\" plays (its compressor: Duck)");
        return true;
    }

    bool Session::canDuckUnder (int trackIndex, int triggerTrack) const
    {
        auto* track = getTrack (trackIndex);
        auto* trigger = getTrack (triggerTrack);
        return edit != nullptr && track != nullptr && trigger != nullptr && track != trigger && ! dependsOn (*edit, trigger, track);
    }

    bool Session::stopDucking (int trackIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return fail ("No such track");

        juce::Array<te::Plugin*> toRemove;

        for (auto* p : track->pluginList)
            if (auto* c = dynamic_cast<te::CompressorPlugin*> (p); c != nullptr && c->state.getProperty (duckProp, false))
                toRemove.add (p);

        if (toRemove.isEmpty())
            return fail ("This track isn't ducking");

        beginUndoStep();

        for (auto* p : toRemove)
            p->deleteFromParent();

        notifyStateChanged();
        return true;
    }

    //==============================================================================
    bool Session::tapTempo()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("Open a song first");

        if (refuseWhileRecording ("changing the tempo"))
            return false;

        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;

        if (! tapTimes.empty() && now - tapTimes.back() > 2.0)
            tapTimes.clear();   // a pause: start counting again

        tapTimes.push_back (now);

        if (tapTimes.size() > 8)
            tapTimes.erase (tapTimes.begin());

        if (tapTimes.size() < 2)
        {
            showHint ("Tap tempo: keep tapping (K) in time");
            return true;
        }

        const auto bpm = juce::jlimit (30.0, 300.0, 60.0 * (double) (tapTimes.size() - 1) / (tapTimes.back() - tapTimes.front()));

        // The tempo in effect at the playhead; a run of taps is one undo step
        const auto index = juce::jmax (0, edit->tempoSequence.indexOfTempoAt (getPosition()));

        if (! setTempoChangeBpm (index, std::round (bpm * 10.0) / 10.0, tapTimes.size() > 2))
            return false;

        showHint ("Tap tempo: " + juce::String (getBpm(), 1) + " BPM (" + juce::String ((int) tapTimes.size()) + " taps)");
        return true;
    }

    //==============================================================================
    bool Session::editSelectedAudioClips (const std::function<bool (te::Clip&)>& editOne)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        juce::Array<te::Clip*> clips;

        for (auto* c : getSelectedClips())
            if (c != nullptr && asAudio (*c) != nullptr)
                clips.add (c);

        if (clips.isEmpty())
            return fail ("Select one or more audio clips first");

        if (refuseWhileRecording ("editing clips"))
            return false;

        beginUndoStep();   // all of them: one undo step
        const juce::ScopedValueSetter<bool> inBatch (batchEdit, true);
        bool ok = true;

        for (auto* c : clips)
            ok = editOne (*c) && ok;

        return ok;
    }
}
