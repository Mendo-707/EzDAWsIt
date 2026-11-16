/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Session: the master channel's mastering chain, loudness target and reference A/B (see Session.h).
*/

#include "Session.h"
#include "Mastering.h"
#include "Meters.h"
#include "LoudnessScan.h"
#include "AudioEngine.h"

namespace ez
{
    namespace
    {
        const juce::Identifier targetProp ("ezMasterTarget"), ditherProp ("ezMasterDither"), refMatchProp ("ezRefMatch");
        const juce::String edited (" (edited)");

        template <typename PluginType>
        PluginType* findMaster (te::Edit* edit)
        {
            if (edit != nullptr)
                for (auto* p : edit->getMasterPluginList())
                    if (auto* typed = dynamic_cast<PluginType*> (p))
                        return typed;

            return nullptr;
        }
    }

    //==============================================================================
    void Session::ensureMasterPlugins()
    {
        // Called right after ensureMasterTap: the chain goes before the meter, the reference after it
        auto& masters = edit->getMasterPluginList();
        bool added = false;

        if (findMaster<MasteringChainPlugin> (edit.get()) == nullptr)
        {
            if (auto chain = edit->getPluginCache().createNewPlugin (MasteringChainPlugin::xmlTypeName, {}))
            {
                chain->setEnabled (false);   // a song only sounds different once someone switches it on
                int tapIndex = masters.size();

                for (int i = 0; i < masters.size(); ++i)
                    if (dynamic_cast<MasterTapPlugin*> (masters[i]) != nullptr)
                        tapIndex = i;

                masters.insertPlugin (chain, tapIndex, nullptr);
                added = true;
            }
        }

        if (findMaster<ReferencePlugin> (edit.get()) == nullptr)
        {
            if (auto reference = edit->getPluginCache().createNewPlugin (ReferencePlugin::xmlTypeName, {}))
            {
                masters.insertPlugin (reference, -1, nullptr);
                added = true;
            }
        }

        if (added)
            edit->getUndoManager().clearUndoHistory();   // part of every song, not something to undo

        referenceLufsValid = false;
        referenceScan.reset();

        // The reference plugin loaded its file already: only its loudness needs measuring again (not saved)
        if (auto* reference = getReference(); reference != nullptr && reference->getFile().existsAsFile())
            startReferenceScan (reference->getFile());
    }

    MasteringChainPlugin* Session::getMasteringChain() const    { return findMaster<MasteringChainPlugin> (edit.get()); }
    ReferencePlugin* Session::getReference() const               { return findMaster<ReferencePlugin> (edit.get()); }

    //==============================================================================
    bool Session::isMasterChainOn() const
    {
        auto* chain = getMasteringChain();
        return chain != nullptr && chain->isEnabled();
    }

    bool Session::setMasterChainOn (bool shouldBeOn)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* chain = getMasteringChain();

        if (chain == nullptr)
            return fail ("Open a song first");

        if (chain->isEnabled() == shouldBeOn)
            return true;

        beginUndoStep();
        chain->setEnabled (shouldBeOn);
        notifyStateChanged();
        return true;
    }

    float Session::getMasterControl (int control) const
    {
        auto* chain = getMasteringChain();
        return chain != nullptr ? chain->getStored (control) : 0.0f;
    }

    juce::String Session::formatMasterControl (int control) const
    {
        auto* chain = getMasteringChain();
        auto* p = chain != nullptr ? chain->getControl (control) : nullptr;
        return p != nullptr ? p->valueToString (chain->getStored (control)) : juce::String();
    }

    bool Session::setMasterControl (int control, float value, bool continuesGesture)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* chain = getMasteringChain();

        if (chain == nullptr || chain->getControl (control) == nullptr)
            return fail ("The mastering chain isn't there");

        if (! continuesGesture)
            beginUndoStep();

        const auto before = chain->getStored (control);
        chain->set (control, value);

        if (chain->getStored (control) != before && ! chain->getPresetName().endsWith (edited))
            chain->setPresetName (chain->getPresetName() + edited);

        return true;
    }

    std::vector<Session::ChainPreset> Session::getMasterPresets()
    {
        std::vector<ChainPreset> list;

        for (const auto& p : MasteringChainPlugin::getPresets())
            list.push_back ({ p.name, p.description });

        return list;
    }

    juce::String Session::getMasterPresetName() const
    {
        auto* chain = getMasteringChain();
        return chain != nullptr ? chain->getPresetName() : juce::String();
    }

    bool Session::applyMasterPreset (int presetIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* chain = getMasteringChain();
        const auto& presets = MasteringChainPlugin::getPresets();

        if (chain == nullptr)
            return fail ("Open a song first");

        if (! juce::isPositiveAndBelow (presetIndex, (int) presets.size()))
            return fail ("No such preset");

        const auto& preset = presets[(size_t) presetIndex];
        beginUndoStep();

        for (int c = 0; c < MasteringChainPlugin::firstModuleSwitch; ++c)
            chain->set (c, preset.values[c]);

        for (int m = 0; m < MasteringChainPlugin::numModules; ++m)
            chain->set (MasteringChainPlugin::firstModuleSwitch + m, (preset.modulesOff & (1 << m)) != 0 ? 0.0f : 1.0f);

        chain->setPresetName (preset.name);
        chain->setEnabled (true);   // picking a preset means you want to hear it
        notifyStateChanged();
        return true;
    }

    //==============================================================================
    int Session::getMasterTarget() const
    {
        if (edit == nullptr)
            return 0;

        return juce::jlimit (0, getLoudnessTargets().size() - 1, (int) edit->state.getProperty (targetProp, 0));
    }

    void Session::setMasterTarget (int index)
    {
        if (edit != nullptr && juce::isPositiveAndBelow (index, getLoudnessTargets().size()))
        {
            edit->state.setProperty (targetProp, index, nullptr);   // a view choice: saved, not an undo step
            notifyStateChanged();
        }
    }

    bool Session::getMasterDither() const
    {
        return edit == nullptr || (bool) edit->state.getProperty (ditherProp, true);
    }

    void Session::setMasterDither (bool shouldDither)
    {
        if (edit != nullptr)
            edit->state.setProperty (ditherProp, shouldDither, nullptr);
    }

    te::LoudnessMeter::Readings Session::getMasterLoudness() const
    {
        return meters != nullptr ? meters->getMain().loudness : te::LoudnessMeter::Readings();
    }

    bool Session::matchMasterTarget()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* chain = getMasteringChain();

        if (chain == nullptr)
            return fail ("Open a song first");

        const auto loudness = getMasterLoudness();

        if (! loudness.integratedValid)
            return fail ("Play the song (the loudest part at least) so its loudness can be measured, then try again");

        const auto& target = getLoudnessTargets().getReference (getMasterTarget());
        const bool limiterWasOn = chain->isEnabled() && chain->getStored (MasteringChainPlugin::limiterOn) >= 0.5f;
        const auto currentGain = limiterWasOn ? chain->getStored (MasteringChainPlugin::gain) : 0.0f;
        const auto wanted = juce::jlimit (0.0f, 18.0f, currentGain + (target.lufs - loudness.integratedLufs));

        if (target.lufs - loudness.integratedLufs < 0 && currentGain <= 0.01f)
            return fail ("The song is already louder than " + juce::String (target.lufs, 0) + " LUFS: turn the master fader down instead");

        // The master fader comes after the chain: above 0 dB it would push the peaks over the ceiling
        const auto ceiling = juce::jlimit (-3.0f, 0.0f, target.ceilingDb - juce::jmax (0.0f, getMasterVolumeDb()));

        beginUndoStep();
        chain->setEnabled (true);
        chain->set (MasteringChainPlugin::limiterOn, 1.0f);
        chain->set (MasteringChainPlugin::gain, wanted);
        chain->set (MasteringChainPlugin::ceiling, ceiling);

        if (! chain->getPresetName().endsWith (edited))
            chain->setPresetName (chain->getPresetName() + edited);

        meters->resetMain();   // measure again from here
        notifyStateChanged();

        showHint ("Limiter gain " + juce::String (wanted, 1) + " dB, ceiling " + juce::String (ceiling, 1)
                  + " dB: play the song again to check it lands on " + juce::String (target.lufs, 0) + " LUFS");
        return true;
    }

    //==============================================================================
    bool Session::loadReference (const juce::File& file)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* reference = getReference();

        if (reference == nullptr)
            return fail ("Open a song first");

        if (! reference->setFile (file))
            return fail ("Can't read " + file.getFileName() + " (WAV, AIFF, FLAC, Ogg or MP3)");

        startReferenceScan (file);
        updateReferenceGain();
        notifyStateChanged();
        return true;
    }

    void Session::startReferenceScan (const juce::File& file)
    {
        referenceLufsValid = false;
        referenceScan = std::make_unique<LoudnessScan> (audioEngine.getEngine().getAudioFileFormatManager().readFormatManager, file, true);
        referenceScan->startThread();
    }

    void Session::clearReference()
    {
        if (auto* reference = getReference())
        {
            reference->setListening (false);
            reference->setFile ({});
        }

        referenceScan.reset();
        referenceLufsValid = false;
        notifyStateChanged();
    }

    juce::File Session::getReferenceFile() const
    {
        auto* reference = getReference();
        return reference != nullptr ? reference->getFile() : juce::File();
    }

    bool Session::isReferenceListening() const
    {
        auto* reference = getReference();
        return reference != nullptr && reference->isListening();
    }

    void Session::setReferenceListening (bool shouldListen)
    {
        if (auto* reference = getReference())
        {
            reference->setListening (shouldListen && reference->getFile().existsAsFile() && ! isExporting());
            updateReferenceGain();
            notifyStateChanged();
        }
    }

    bool Session::getReferenceMatchLoudness() const
    {
        return edit == nullptr || (bool) edit->state.getProperty (refMatchProp, true);
    }

    void Session::setReferenceMatchLoudness (bool shouldMatch)
    {
        if (edit != nullptr)
        {
            edit->state.setProperty (refMatchProp, shouldMatch, nullptr);
            updateReferenceGain();
            notifyStateChanged();
        }
    }

    void Session::updateReferenceGain()
    {
        auto* reference = getReference();

        if (reference == nullptr)
            return;

        // Matched: as loud as the mix is (or, before the mix is measured, as the target), master fader included
        // (it comes after the reference too, so take it back out)
        float gainDb = 0.0f;

        if (getReferenceMatchLoudness() && referenceLufsValid)
        {
            const auto mix = getMasterLoudness();
            const auto mixLufs = mix.integratedValid ? mix.integratedLufs
                               : mix.shortTermValid  ? mix.shortTermLufs
                                                     : getLoudnessTargets().getReference (getMasterTarget()).lufs;
            gainDb = juce::jlimit (-30.0f, 12.0f, mixLufs - referenceLufs - getMasterVolumeDb());
        }
        else
        {
            gainDb = -getMasterVolumeDb();   // as it is on the file
        }

        reference->setGainDb (gainDb);
    }

    void Session::updateMaster()
    {
        if (referenceScan != nullptr && referenceScan->finished)
        {
            referenceLufsValid = referenceScan->valid;
            referenceLufs = referenceScan->readings.integratedLufs;
            referenceScan.reset();
            notifyStateChanged();
        }

        if (isReferenceListening())
            updateReferenceGain();   // follows the mix's loudness as it's measured
    }

    //==============================================================================
    Session::MasterAdvice Session::adviseMaster (const MasterState& m)
    {
        using Fix = MasterAdvice::Fix;
        const auto db = [] (float v) { return juce::String (v, 1); };

        if (! m.measured)
            return { Fix::none, "Listening...", "Play the song through its loudest part. The assistant listens through the main meter, "
                                                "then suggests one fix at a time.", {} };

        if (m.truePeakDb > 0.0f && ! m.chainOn)
        {
            const auto down = std::ceil (m.truePeakDb + 1.0f);
            return { Fix::lowerFader, "Your mix clips", "Peaks reach +" + db (m.truePeakDb) + " dB, past 0: that crackles. "
                     "Turn the master down " + juce::String ((int) down) + " dB, then let the limiter bring the loudness back.",
                     "Turn down " + juce::String ((int) down) + " dB", down };
        }

        if (! m.chainOn)
            return { Fix::chainOn, "Start the mastering chain", "A gentle EQ, glue and a limiter, set to a balanced starting point. "
                     "Compare with it off any time (Ctrl+Shift+A).", "Switch on (Balanced)" };

        if (m.rangeLu > 14.0f && (! m.glueOn || m.glueAmount < 50.0f))
            return { Fix::moreGlue, "Big jumps in level", "The loud and quiet parts are " + db (m.rangeLu) + " LU apart: the quiet parts "
                     "get lost on phones and in cars. More glue evens it out.", "More glue" };

        if (m.clubTarget && ! m.monoBassOn)
            return { Fix::monoBass, "Bass in mono for the club", "Club systems play the bass in mono: keeping it centred makes it "
                     "tighter and louder there.", "Mono bass" };

        if (std::abs (m.integratedLufs - m.targetLufs) > 1.0f)
        {
            const auto off = m.integratedLufs - m.targetLufs;
            return { Fix::matchTarget, off < 0 ? "Quieter than " + m.targetName : "Louder than " + m.targetName,
                     db (std::abs (off)) + " LU " + (off < 0 ? "under" : "over") + " the target of " + db (m.targetLufs)
                     + " LUFS. The limiter can set it, keeping peaks under " + db (m.ceilingDb) + " dB.",
                     off < 0 ? juce::String ("Match the target") : juce::String ("Match the target") };
        }

        if (m.referenceMeasured && ! m.referenceMatching && std::abs (m.referenceLufs - m.integratedLufs) > 2.0f)
            return { Fix::matchReference, "Compare fairly with the reference", "The reference is " + db (std::abs (m.referenceLufs - m.integratedLufs))
                     + " LU " + (m.referenceLufs > m.integratedLufs ? "louder" : "quieter") + ": louder always sounds better. "
                     "Play it as loud as your mix to judge the tone.", "Match its loudness" };

        return { Fix::none, "On target", db (m.integratedLufs) + " LUFS, peaks at " + db (m.truePeakDb) + " dB. "
                 "Check it on headphones and a phone, then export (Ctrl+R).", {} };
    }

    Session::MasterAdvice Session::getMasterAdvice() const
    {
        MasterState m;

        if (edit == nullptr)
            return adviseMaster (m);

        const auto loudness = getMasterLoudness();
        m.measured = loudness.integratedValid;
        m.integratedLufs = loudness.integratedLufs;
        m.truePeakDb = loudness.truePeakDb;
        m.rangeLu = loudness.loudnessRangeValid ? loudness.loudnessRangeLu : 0.0f;

        if (auto* chain = getMasteringChain())
        {
            m.chainOn = chain->isEnabled();
            m.glueOn = chain->getStored (MasteringChainPlugin::glueOn) >= 0.5f;
            m.monoBassOn = chain->getStored (MasteringChainPlugin::stereoOn) >= 0.5f && chain->getStored (MasteringChainPlugin::monoBass) >= 0.5f;
            m.glueAmount = (chain->getStored (MasteringChainPlugin::glueLow) + chain->getStored (MasteringChainPlugin::glueMid)
                            + chain->getStored (MasteringChainPlugin::glueHigh)) / 3.0f;
        }

        const auto& target = getLoudnessTargets().getReference (getMasterTarget());
        m.targetLufs = target.lufs;
        m.ceilingDb = target.ceilingDb;
        m.targetName = target.name;
        m.clubTarget = target.name.containsIgnoreCase ("club");
        m.referenceMeasured = referenceLufsValid && getReferenceFile().existsAsFile();
        m.referenceLufs = referenceLufs;
        m.referenceMatching = getReferenceMatchLoudness();
        return adviseMaster (m);
    }

    bool Session::applyMasterAdvice (const MasterAdvice& advice)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        using Fix = MasterAdvice::Fix;
        auto* chain = getMasteringChain();

        if (edit == nullptr || chain == nullptr)
            return fail ("Open a song first");

        switch (advice.fix)
        {
            case Fix::none:
                return true;

            case Fix::lowerFader:
                beginUndoStep();
                setMasterVolumeDb (getMasterVolumeDb() - juce::jlimit (1.0f, 24.0f, advice.amount));
                break;

            case Fix::chainOn:
                if (! applyMasterPreset (0))
                    return false;
                break;

            case Fix::moreGlue:
                beginUndoStep();
                chain->setEnabled (true);
                chain->set (MasteringChainPlugin::glueOn, 1.0f);

                for (auto c : { MasteringChainPlugin::glueLow, MasteringChainPlugin::glueMid, MasteringChainPlugin::glueHigh })
                    chain->set (c, juce::jlimit (0.0f, 80.0f, chain->getStored (c) + 25.0f));
                break;

            case Fix::monoBass:
                beginUndoStep();
                chain->set (MasteringChainPlugin::stereoOn, 1.0f);
                chain->set (MasteringChainPlugin::monoBass, 1.0f);
                break;

            case Fix::matchTarget:
                return matchMasterTarget();   // its own undo step and hint

            case Fix::matchReference:
                setReferenceMatchLoudness (true);
                showHint ("The reference now plays as loud as your mix: switch with Ctrl+Shift+F");
                notifyStateChanged();
                return true;
        }

        if (advice.fix != Fix::chainOn && ! chain->getPresetName().endsWith (" (edited)"))
            chain->setPresetName (chain->getPresetName() + " (edited)");

        meters->resetMain();   // listen again from here
        showHint (advice.title + ": done. Play the song again for the next tip.");
        notifyStateChanged();
        return true;
    }
}
