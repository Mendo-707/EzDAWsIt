/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Session: controls of our own effects and the Vocal Chain's presets (see Session.h).
*/

#include "Session.h"
#include "VocalChain.h"

namespace ez
{
    namespace
    {
        EffectPlugin* asEffect (const juce::Array<te::Plugin*>& effects, int index)
        {
            return juce::isPositiveAndBelow (index, effects.size()) ? dynamic_cast<EffectPlugin*> (effects[index]) : nullptr;
        }

        const juce::String edited (" (edited)");
    }

    Session::ParamRef Session::getEffectParam (int trackIndex, int effectIndex, int control) const
    {
        auto* fx = asEffect (getEffects (trackIndex), effectIndex);
        auto* p = fx != nullptr ? fx->getControl (control) : nullptr;
        return p != nullptr ? ParamRef { fx->itemID, p->paramID } : ParamRef {};
    }

    float Session::getEffectControl (int trackIndex, int effectIndex, int control) const
    {
        auto* fx = asEffect (getEffects (trackIndex), effectIndex);

        if (fx == nullptr || fx->getControl (control) == nullptr)
            return 0.0f;

        // Automated (and Read on): where the lane has it now; otherwise the stored value (the parameter lags an undo)
        auto* p = fx->getControl (control);
        return p->hasAutomationPoints() && isAutomationReadOn() ? p->getCurrentValue() : fx->getStored (control);
    }

    juce::String Session::formatEffectControl (int trackIndex, int effectIndex, int control) const
    {
        auto* fx = asEffect (getEffects (trackIndex), effectIndex);
        auto* p = fx != nullptr ? fx->getControl (control) : nullptr;
        return p != nullptr ? p->valueToString (getEffectControl (trackIndex, effectIndex, control)) : juce::String();
    }

    bool Session::setEffectControl (int trackIndex, int effectIndex, int control, float value, bool continuesGesture)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* fx = asEffect (getEffects (trackIndex), effectIndex);
        auto* p = fx != nullptr ? fx->getControl (control) : nullptr;

        if (p == nullptr)
            return fail ("That control isn't there any more");

        if (refuseIfAutomated (p, p->getParameterName()))
            return false;

        if (! continuesGesture)
            beginUndoStep();

        const auto before = fx->getStored (control);
        fx->set (control, value);

        // Moving a macro or switch by hand: the preset's name says it was changed (only if it did change)
        if (auto* chain = dynamic_cast<VocalChainPlugin*> (fx); chain != nullptr && fx->getStored (control) != before
             && control != VocalChainPlugin::root && control != VocalChainPlugin::scale
             && ! chain->getPresetName().endsWith (edited))
            chain->setPresetName (chain->getPresetName() + edited);

        return true;
    }

    std::vector<Session::ChainPreset> Session::getVocalChainPresets()
    {
        std::vector<ChainPreset> list;

        for (const auto& p : VocalChainPlugin::getPresets())
            list.push_back ({ p.name, p.description });

        return list;
    }

    juce::String Session::getVocalChainPresetName (int trackIndex, int effectIndex) const
    {
        auto* chain = dynamic_cast<VocalChainPlugin*> (asEffect (getEffects (trackIndex), effectIndex));
        return chain != nullptr ? chain->getPresetName() : juce::String();
    }

    bool Session::applyVocalChainPreset (int trackIndex, int effectIndex, int presetIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* chain = dynamic_cast<VocalChainPlugin*> (asEffect (getEffects (trackIndex), effectIndex));
        const auto& presets = VocalChainPlugin::getPresets();

        if (chain == nullptr)
            return fail ("That isn't a Vocal Chain any more");

        if (! juce::isPositiveAndBelow (presetIndex, (int) presets.size()))
            return fail ("No such preset");

        const auto& preset = presets[(size_t) presetIndex];

        for (int c = 0; c < VocalChainPlugin::numControls; ++c)   // every control a preset sets (the key isn't, but is harmless)
            if (auto* p = chain->getControl (c); p != nullptr && c != VocalChainPlugin::root && c != VocalChainPlugin::scale
                                                  && p->hasAutomationPoints() && isAutomationReadOn())
                return fail (p->getParameterName() + " is automated: clear its lane (or turn Read off) to change presets");

        beginUndoStep();

        for (int m = 0; m < VocalChainPlugin::numMacros; ++m)
            chain->set (VocalChainPlugin::firstMacro + m, preset.macros[m]);

        for (int m = 0; m < VocalChainPlugin::numModules; ++m)
        {
            const bool off = (preset.modulesOff & (1 << m)) != 0;
            chain->set (VocalChainPlugin::firstModule + m, off ? 0.0f : 1.0f);
        }

        chain->set (VocalChainPlugin::phone, preset.phone ? 1.0f : 0.0f);
        chain->setPresetName (preset.name);
        notifyStateChanged();
        return true;
    }

    float Session::getVocalChainReductionDb (int trackIndex, int effectIndex) const
    {
        auto* chain = dynamic_cast<VocalChainPlugin*> (asEffect (getEffects (trackIndex), effectIndex));
        return chain != nullptr ? chain->getPunchReductionDb() : 0.0f;
    }

    //==============================================================================
    TunerPlugin* Session::getTuner (int trackIndex) const
    {
        for (auto* p : getEffects (trackIndex))
            if (auto* tuner = dynamic_cast<TunerPlugin*> (p))
                return tuner;

        return nullptr;
    }

    TunerPlugin* Session::ensureTuner (int trackIndex)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (auto* existing = getTuner (trackIndex))
            return existing;

        auto* track = getTrack (trackIndex);

        if (track == nullptr)
        {
            fail ("No such track");
            return nullptr;
        }

        if (getTrackKind (trackIndex) != TrackKind::normal)
        {
            fail ("A tuner goes on a track you record into, not a bus or a return");
            return nullptr;
        }

        // First of the effects: it hears the guitar before an amp or anything else changes it
        const auto effects = getEffects (trackIndex);
        auto index = effects.isEmpty() ? effectInsertIndex (*track) : track->pluginList.indexOf (effects.getFirst());

        auto plugin = edit->getPluginCache().createNewPlugin (TunerPlugin::xmlTypeName, {});

        if (plugin == nullptr)
        {
            fail ("Could not make a tuner");
            return nullptr;
        }

        beginUndoStep();
        const auto before = track->pluginList.size();
        track->pluginList.insertPlugin (plugin, index, nullptr);

        if (track->pluginList.size() != before + 1)
        {
            edit->getUndoManager().undoCurrentTransactionOnly();
            fail ("This track is full: remove an effect first");
            return nullptr;
        }

        notifyStateChanged();
        return dynamic_cast<TunerPlugin*> (plugin.get());
    }
}
