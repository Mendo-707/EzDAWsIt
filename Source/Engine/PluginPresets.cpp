/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "PluginPresets.h"
#include "../Core/AppPaths.h"

namespace ez::PluginPresets
{
    namespace
    {
        const juce::Identifier currentNameProp { "ezPresetName" };

        te::AutomatableParameter* findParam (te::Plugin& plugin, const juce::String& id)
        {
            for (auto* p : plugin.getAutomatableParameters())
                if (p->paramID == id)
                    return p;

            return nullptr;
        }


        Preset make (const char* name, std::vector<Value> values)
        {
            Preset p;
            p.name = name;
            p.values = std::move (values);
            p.isFactory = true;
            return p;
        }
    }

    juce::var asVar (float value)
    {
        if (std::abs (value - std::round (value)) < 1.0e-6f && std::abs (value) < 1.0e6f)
            return juce::var ((int) std::round (value));

        return juce::var ((double) value);
    }

    //==============================================================================
    std::vector<Value> capture (te::Plugin& plugin, const juce::StringArray& settingIds)
    {
        std::vector<Value> values;

        for (auto* p : plugin.getAutomatableParameters())
            values.push_back ({ p->paramID, p->getCurrentBaseValue(), {} });

        for (const auto& id : settingIds)
        {
            const auto v = plugin.state[juce::Identifier (id)];

            if (v.isVoid())
                continue;   // still at its default: nothing to store

            if (v.isString() && ! v.toString().containsOnly ("0123456789.-e"))
                values.push_back ({ id, 0.0f, v.toString() });
            else
                values.push_back ({ id, (float) v, {} });
        }

        return values;
    }

    void resetToDefaults (te::Plugin& plugin, const juce::StringArray& settingIds)
    {
        for (auto* p : plugin.getAutomatableParameters())
            if (const auto def = p->getDefaultValue())
                p->setParameter (*def, juce::sendNotification);

        auto* um = &plugin.edit.getUndoManager();

        for (const auto& id : settingIds)
            plugin.state.removeProperty (juce::Identifier (id), um);
    }

    void apply (te::Plugin& plugin, const std::vector<Value>& values, const juce::StringArray& settingIds, bool resetFirst)
    {
        // What a preset never changes: Tune's key and scale come from the song
        const juce::StringArray kept = plugin.getPluginType() == "eztune" ? juce::StringArray { "root", "scale" } : juce::StringArray();
        std::vector<std::pair<te::AutomatableParameter*, float>> keep;

        for (const auto& id : kept)
            if (auto* p = findParam (plugin, id))
                keep.push_back ({ p, p->getCurrentBaseValue() });

        if (resetFirst)
            resetToDefaults (plugin, settingIds);

        for (auto [p, v] : keep)
            p->setParameter (v, juce::sendNotification);

        auto* um = &plugin.edit.getUndoManager();

        for (const auto& v : values)
        {
            if (auto* p = findParam (plugin, v.id))
            {
                p->setParameter (p->valueRange.snapToLegalValue (v.value), juce::sendNotification);
            }
            else if (v.id.isNotEmpty())
            {
                // A setting stored as a property (the synth's waveforms, the delay time...)
                plugin.state.setProperty (juce::Identifier (v.id), v.text.isNotEmpty() ? juce::var (v.text) : asVar (v.value), um);
            }
        }
    }

    //==============================================================================
    namespace
    {
        /** A synth sound: the category, then the values. */
        Preset sound (const char* category, const char* name, std::vector<Value> values)
        {
            auto p = make (name, std::move (values));
            p.category = category;
            return p;
        }

        // Four Osc: osc waves 0 off, 1 sine, 2 square, 3 saw, 4 triangle, 5 noise. Filter 1 low-pass,
        // 2 high-pass, 3 band-pass; filterFreq is a note number (60 = middle C, 96 = 2 kHz, 120 = 8 kHz).
        // voiceMode 0 mono, 1 legato, 2 poly.
        std::vector<Preset> fourOscSounds()
        {
            return {
                // Bass
                sound ("Bass", "Sub bass",   { { "waveShape1", 1 }, { "ampAttack", 0.005f }, { "ampDecay", 0.3f }, { "ampSustain", 100 }, { "ampRelease", 0.1f },
                                               { "voiceMode", 0 }, { "masterLevel", -3 } }),
                sound ("Bass", "808 slide",  { { "waveShape1", 1 }, { "level1", 0 }, { "waveShape2", 0 },
                                               { "ampAttack", 0.001f }, { "ampDecay", 2.5f }, { "ampSustain", 35 }, { "ampRelease", 0.3f },
                                               { "voiceMode", 1 }, { "legato", 90 }, { "distortionOn", 1 }, { "distortion", 0.25f },
                                               { "filterType", 0 }, { "masterLevel", -4 } }),
                sound ("Bass", "Reese bass", { { "waveShape1", 3 }, { "voices1", 4 }, { "detune1", 0.22f }, { "spread1", 60 }, { "level1", -3 },
                                               { "waveShape2", 3 }, { "tune2", -12 }, { "voices2", 2 }, { "detune2", 0.12f }, { "level2", -6 },
                                               { "ampAttack", 0.005f }, { "ampSustain", 100 }, { "ampRelease", 0.15f },
                                               { "filterType", 1 }, { "filterFreq", 78 }, { "filterResonance", 25 },
                                               { "voiceMode", 1 }, { "legato", 40 }, { "masterLevel", -8 } }),
                sound ("Bass", "Acid bass",  { { "waveShape1", 3 }, { "ampAttack", 0.002f }, { "ampDecay", 0.3f }, { "ampSustain", 80 }, { "ampRelease", 0.08f },
                                               { "filterType", 1 }, { "filterSlope", 24 }, { "filterFreq", 48 }, { "filterResonance", 75 },
                                               { "filterAmount", 0.6f }, { "filterAttack", 0.0f }, { "filterDecay", 0.25f }, { "filterSustain", 0 },
                                               { "distortionOn", 1 }, { "distortion", 0.3f }, { "voiceMode", 1 }, { "legato", 60 }, { "masterLevel", -8 } }),
                sound ("Bass", "Fat saw bass", { { "waveShape1", 3 }, { "voices1", 2 }, { "detune1", 0.08f },
                                                 { "waveShape2", 2 }, { "tune2", -12 }, { "level2", -6 },
                                                 { "ampAttack", 0.003f }, { "ampSustain", 100 }, { "ampRelease", 0.1f },
                                                 { "filterType", 1 }, { "filterFreq", 70 }, { "filterResonance", 15 },
                                                 { "filterAmount", 0.3f }, { "filterDecay", 0.3f }, { "filterSustain", 20 },
                                                 { "voiceMode", 0 }, { "masterLevel", -7 } }),
                sound ("Bass", "Pluck bass", { { "waveShape1", 2 }, { "ampAttack", 0.001f }, { "ampDecay", 0.4f }, { "ampSustain", 0 }, { "ampRelease", 0.1f },
                                               { "filterType", 1 }, { "filterFreq", 55 }, { "filterAmount", 0.5f }, { "filterDecay", 0.15f }, { "filterSustain", 0 },
                                               { "voiceMode", 0 }, { "masterLevel", -5 } }),
                sound ("Bass", "Growl bass", { { "waveShape1", 3 }, { "voices1", 3 }, { "detune1", 0.2f }, { "waveShape2", 2 }, { "tune2", -12 }, { "level2", -8 },
                                               { "ampAttack", 0.005f }, { "ampSustain", 100 }, { "ampRelease", 0.12f },
                                               { "filterType", 3 }, { "filterFreq", 70 }, { "filterResonance", 50 },
                                               { "distortionOn", 1 }, { "distortion", 0.5f }, { "voiceMode", 1 }, { "legato", 30 }, { "masterLevel", -10 } }),
                sound ("Bass", "Deep house bass", { { "waveShape1", 1 }, { "waveShape2", 4 }, { "tune2", 12 }, { "level2", -14 },
                                                    { "ampAttack", 0.003f }, { "ampDecay", 0.5f }, { "ampSustain", 40 }, { "ampRelease", 0.1f },
                                                    { "voiceMode", 0 }, { "masterLevel", -4 } }),

                // Keys
                sound ("Keys", "Keys",       { { "waveShape1", 4 }, { "waveShape2", 1 }, { "tune2", 12 }, { "level2", -10 },
                                               { "ampAttack", 0.003f }, { "ampDecay", 1.2f }, { "ampSustain", 30 }, { "ampRelease", 0.4f },
                                               { "reverbOn", 1 }, { "reverbSize", 0.4f }, { "reverbMix", 0.2f }, { "masterLevel", -4 } }),
                sound ("Keys", "Electric piano", { { "waveShape1", 1 }, { "waveShape2", 4 }, { "tune2", 24 }, { "level2", -18 },
                                                   { "ampAttack", 0.002f }, { "ampDecay", 1.8f }, { "ampSustain", 20 }, { "ampRelease", 0.5f }, { "ampVelocity", 60 },
                                                   { "chorusOn", 1 }, { "chorusMix", 0.25f }, { "masterLevel", -5 } }),
                sound ("Keys", "Organ",      { { "waveShape1", 1 }, { "waveShape2", 1 }, { "tune2", 12 }, { "level2", -4 },
                                               { "waveShape3", 1 }, { "tune3", 19 }, { "level3", -10 }, { "waveShape4", 1 }, { "tune4", 24 }, { "level4", -12 },
                                               { "ampAttack", 0.005f }, { "ampSustain", 100 }, { "ampRelease", 0.08f },
                                               { "chorusOn", 1 }, { "chorusSpeed", 6 }, { "chorusDepth", 2 }, { "chorusMix", 0.4f }, { "masterLevel", -9 } }),
                sound ("Keys", "Bell",       { { "waveShape1", 1 }, { "waveShape2", 1 }, { "tune2", 24 }, { "fineTune2", 14 }, { "level2", -8 },
                                               { "waveShape3", 1 }, { "tune3", 31 }, { "level3", -16 },
                                               { "ampAttack", 0.001f }, { "ampDecay", 2.5f }, { "ampSustain", 0 }, { "ampRelease", 1.5f },
                                               { "reverbOn", 1 }, { "reverbSize", 0.6f }, { "reverbMix", 0.25f }, { "masterLevel", -6 } }),
                sound ("Keys", "Lo-fi keys", { { "waveShape1", 4 }, { "waveShape2", 1 }, { "tune2", 12 }, { "level2", -12 },
                                               { "ampAttack", 0.004f }, { "ampDecay", 1.5f }, { "ampSustain", 25 }, { "ampRelease", 0.4f },
                                               { "filterType", 1 }, { "filterFreq", 88 },
                                               { "distortionOn", 1 }, { "distortion", 0.15f },
                                               { "chorusOn", 1 }, { "chorusSpeed", 0.4f }, { "chorusDepth", 6 }, { "chorusMix", 0.35f }, { "masterLevel", -5 } }),
                sound ("Keys", "Clav",       { { "waveShape1", 2 }, { "pulseWidth1", 0.25f },
                                               { "ampAttack", 0.001f }, { "ampDecay", 0.6f }, { "ampSustain", 10 }, { "ampRelease", 0.1f },
                                               { "filterType", 1 }, { "filterFreq", 100 }, { "filterAmount", 0.3f }, { "filterDecay", 0.2f }, { "filterSustain", 0 },
                                               { "masterLevel", -8 } }),

                // Pad
                sound ("Pad", "Warm pad",    { { "waveShape1", 3 }, { "voices1", 4 }, { "detune1", 0.15f }, { "spread1", 50 },
                                               { "waveShape2", 3 }, { "tune2", -12 }, { "level2", -6 },
                                               { "ampAttack", 0.8f }, { "ampDecay", 1.0f }, { "ampSustain", 80 }, { "ampRelease", 1.5f },
                                               { "filterType", 1 }, { "filterFreq", 72 }, { "filterResonance", 10 },
                                               { "chorusOn", 1 }, { "chorusMix", 0.3f }, { "reverbOn", 1 }, { "reverbSize", 0.7f }, { "reverbMix", 0.3f },
                                               { "masterLevel", -6 } }),
                sound ("Pad", "Dark pad",    { { "waveShape1", 3 }, { "voices1", 3 }, { "detune1", 0.1f }, { "waveShape2", 3 }, { "voices2", 3 }, { "detune2", 0.12f },
                                               { "tune2", -12 }, { "level2", -4 },
                                               { "ampAttack", 1.5f }, { "ampSustain", 90 }, { "ampRelease", 3.0f },
                                               { "filterType", 1 }, { "filterFreq", 60 }, { "filterResonance", 10 },
                                               { "reverbOn", 1 }, { "reverbSize", 0.8f }, { "reverbMix", 0.4f }, { "masterLevel", -9 } }),
                sound ("Pad", "Glass pad",   { { "waveShape1", 4 }, { "waveShape2", 1 }, { "tune2", 12 }, { "level2", -6 },
                                               { "ampAttack", 1.0f }, { "ampSustain", 85 }, { "ampRelease", 2.5f },
                                               { "filterType", 2 }, { "filterFreq", 50 },
                                               { "chorusOn", 1 }, { "chorusMix", 0.4f },
                                               { "reverbOn", 1 }, { "reverbSize", 0.85f }, { "reverbMix", 0.45f }, { "masterLevel", -6 } }),
                sound ("Pad", "Strings",     { { "waveShape1", 3 }, { "voices1", 4 }, { "detune1", 0.12f }, { "spread1", 70 },
                                               { "waveShape2", 3 }, { "tune2", 12 }, { "voices2", 2 }, { "detune2", 0.1f }, { "level2", -10 },
                                               { "ampAttack", 0.4f }, { "ampSustain", 90 }, { "ampRelease", 0.9f },
                                               { "filterType", 1 }, { "filterFreq", 96 },
                                               { "chorusOn", 1 }, { "chorusMix", 0.3f }, { "reverbOn", 1 }, { "reverbSize", 0.6f }, { "reverbMix", 0.3f },
                                               { "masterLevel", -9 } }),
                sound ("Pad", "Sweep pad",   { { "waveShape1", 3 }, { "voices1", 4 }, { "detune1", 0.18f }, { "spread1", 80 },
                                               { "ampAttack", 1.2f }, { "ampSustain", 90 }, { "ampRelease", 2.0f },
                                               { "filterType", 1 }, { "filterFreq", 50 }, { "filterResonance", 30 },
                                               { "filterAmount", 0.5f }, { "filterAttack", 4.0f }, { "filterDecay", 4.0f }, { "filterSustain", 60 },
                                               { "reverbOn", 1 }, { "reverbSize", 0.8f }, { "reverbMix", 0.35f }, { "masterLevel", -9 } }),
                sound ("Pad", "Ambient wash", { { "waveShape1", 3 }, { "voices1", 6 }, { "detune1", 0.3f }, { "spread1", 100 },
                                                { "ampAttack", 3.0f }, { "ampSustain", 90 }, { "ampRelease", 6.0f },
                                                { "filterType", 1 }, { "filterFreq", 70 },
                                                { "delayOn", 1 }, { "delayMix", 0.3f },
                                                { "reverbOn", 1 }, { "reverbSize", 0.95f }, { "reverbMix", 0.55f }, { "masterLevel", -10 } }),

                // Lead
                sound ("Lead", "Lead",       { { "waveShape1", 2 }, { "waveShape2", 3 }, { "tune2", 12 }, { "level2", -8 },
                                               { "voiceMode", 1 }, { "legato", 40 }, { "ampAttack", 0.01f }, { "ampSustain", 90 },
                                               { "filterType", 1 }, { "filterFreq", 95 }, { "filterResonance", 25 },
                                               { "delayOn", 1 }, { "delayMix", 0.15f }, { "masterLevel", -6 } }),
                sound ("Lead", "Square lead", { { "waveShape1", 2 }, { "waveShape2", 2 }, { "tune2", 12 }, { "fineTune2", 7 }, { "level2", -10 },
                                                { "voiceMode", 1 }, { "legato", 30 }, { "ampAttack", 0.005f }, { "ampSustain", 100 }, { "ampRelease", 0.1f },
                                                { "filterType", 1 }, { "filterFreq", 100 }, { "filterResonance", 20 },
                                                { "delayOn", 1 }, { "delayMix", 0.2f }, { "masterLevel", -9 } }),
                sound ("Lead", "Supersaw",   { { "waveShape1", 3 }, { "voices1", 7 }, { "detune1", 0.25f }, { "spread1", 90 },
                                               { "waveShape2", 3 }, { "tune2", 12 }, { "voices2", 4 }, { "detune2", 0.2f }, { "level2", -8 },
                                               { "ampAttack", 0.01f }, { "ampSustain", 100 }, { "ampRelease", 0.3f },
                                               { "filterType", 1 }, { "filterFreq", 110 },
                                               { "reverbOn", 1 }, { "reverbMix", 0.25f }, { "masterLevel", -12 } }),
                sound ("Lead", "Sine lead",  { { "waveShape1", 1 }, { "voiceMode", 1 }, { "legato", 80 },
                                               { "ampAttack", 0.02f }, { "ampSustain", 100 }, { "ampRelease", 0.2f },
                                               { "delayOn", 1 }, { "delayMix", 0.25f }, { "reverbOn", 1 }, { "reverbMix", 0.2f }, { "masterLevel", -4 } }),
                sound ("Lead", "Screamer",   { { "waveShape1", 3 }, { "voices1", 2 }, { "detune1", 0.05f },
                                               { "voiceMode", 1 }, { "legato", 50 }, { "ampAttack", 0.005f }, { "ampSustain", 100 },
                                               { "filterType", 3 }, { "filterFreq", 90 }, { "filterResonance", 45 },
                                               { "distortionOn", 1 }, { "distortion", 0.7f }, { "delayOn", 1 }, { "delayMix", 0.15f }, { "masterLevel", -12 } }),
                sound ("Lead", "Chiptune",   { { "waveShape1", 2 }, { "voiceMode", 0 },
                                               { "ampAttack", 0.001f }, { "ampSustain", 100 }, { "ampRelease", 0.05f }, { "masterLevel", -10 } }),

                // Pluck
                sound ("Pluck", "Pluck",     { { "waveShape1", 3 }, { "ampAttack", 0.001f }, { "ampDecay", 0.35f }, { "ampSustain", 0 }, { "ampRelease", 0.2f },
                                               { "filterType", 1 }, { "filterFreq", 60 }, { "filterResonance", 20 }, { "filterAmount", 0.5f },
                                               { "filterAttack", 0.001f }, { "filterDecay", 0.25f }, { "filterSustain", 0 },
                                               { "delayOn", 1 }, { "delayMix", 0.2f }, { "masterLevel", -4 } }),
                sound ("Pluck", "Marimba",   { { "waveShape1", 1 }, { "waveShape2", 4 }, { "tune2", 24 }, { "level2", -16 },
                                               { "ampAttack", 0.001f }, { "ampDecay", 0.35f }, { "ampSustain", 0 }, { "ampRelease", 0.3f },
                                               { "reverbOn", 1 }, { "reverbMix", 0.15f }, { "masterLevel", -3 } }),
                sound ("Pluck", "Harp",      { { "waveShape1", 4 }, { "ampAttack", 0.001f }, { "ampDecay", 1.2f }, { "ampSustain", 0 }, { "ampRelease", 1.0f },
                                               { "reverbOn", 1 }, { "reverbSize", 0.6f }, { "reverbMix", 0.3f }, { "masterLevel", -4 } }),
                sound ("Pluck", "Future bass chord", { { "waveShape1", 3 }, { "voices1", 5 }, { "detune1", 0.2f }, { "spread1", 80 },
                                                       { "ampAttack", 0.005f }, { "ampDecay", 0.6f }, { "ampSustain", 40 }, { "ampRelease", 0.3f },
                                                       { "filterType", 1 }, { "filterFreq", 80 }, { "filterAmount", 0.35f }, { "filterDecay", 0.3f }, { "filterSustain", 30 },
                                                       { "reverbOn", 1 }, { "reverbMix", 0.3f }, { "masterLevel", -11 } }),
                sound ("Pluck", "Stab",      { { "waveShape1", 3 }, { "waveShape2", 2 }, { "level2", -6 },
                                               { "ampAttack", 0.001f }, { "ampDecay", 0.25f }, { "ampSustain", 0 }, { "ampRelease", 0.15f },
                                               { "filterType", 1 }, { "filterFreq", 90 }, { "filterAmount", 0.3f }, { "filterDecay", 0.15f }, { "filterSustain", 0 },
                                               { "reverbOn", 1 }, { "reverbMix", 0.2f }, { "masterLevel", -8 } }),

                // Brass & FX
                sound ("Brass & FX", "Brass", { { "waveShape1", 3 }, { "voices1", 2 }, { "detune1", 0.06f },
                                                { "ampAttack", 0.05f }, { "ampSustain", 90 }, { "ampRelease", 0.2f },
                                                { "filterType", 1 }, { "filterFreq", 70 }, { "filterAmount", 0.45f },
                                                { "filterAttack", 0.08f }, { "filterDecay", 0.4f }, { "filterSustain", 60 }, { "masterLevel", -9 } }),
                sound ("Brass & FX", "Riser", { { "waveShape1", 5 }, { "ampAttack", 6.0f }, { "ampSustain", 100 }, { "ampRelease", 2.0f },
                                                { "filterType", 2 }, { "filterFreq", 40 }, { "filterAmount", 0.6f }, { "filterAttack", 8.0f }, { "filterSustain", 100 },
                                                { "reverbOn", 1 }, { "reverbSize", 0.8f }, { "reverbMix", 0.4f }, { "masterLevel", -14 } }),
                sound ("Brass & FX", "Noise sweep", { { "waveShape1", 5 }, { "ampAttack", 0.5f }, { "ampDecay", 3.0f }, { "ampSustain", 0 }, { "ampRelease", 1.0f },
                                                      { "filterType", 3 }, { "filterFreq", 70 }, { "filterResonance", 60 },
                                                      { "filterAmount", 0.5f }, { "filterAttack", 2.0f }, { "filterDecay", 2.0f }, { "filterSustain", 0 },
                                                      { "reverbOn", 1 }, { "reverbMix", 0.35f }, { "masterLevel", -14 } }),
            };
        }
    }

    std::vector<Preset> getFactory (const juce::String& type)
    {
        if (type == te::FourOscPlugin::xmlTypeName)
            return fourOscSounds();

        // The compressor's ratio is a slope: 0.5 is 2:1, 0.25 is 4:1, 0.1 is 10:1. Threshold is a level (0.5 = -6 dB)
        if (type == te::CompressorPlugin::xmlTypeName)
            return {
                make ("Gentle glue",    { { "threshold", 0.5f },  { "ratio", 0.5f },  { "attack", 30 }, { "release", 150 }, { "output gain", 1 } }),
                make ("Vocal",          { { "threshold", 0.25f }, { "ratio", 0.33f }, { "attack", 10 }, { "release", 80 },  { "output gain", 3 } }),
                make ("Punchy drums",   { { "threshold", 0.3f },  { "ratio", 0.25f }, { "attack", 25 }, { "release", 60 },  { "output gain", 2 } }),
                make ("Squash",         { { "threshold", 0.1f },  { "ratio", 0.1f },  { "attack", 1 },  { "release", 50 },  { "output gain", 6 } }),
                make ("Bass",           { { "threshold", 0.3f },  { "ratio", 0.25f }, { "attack", 15 }, { "release", 100 }, { "output gain", 3 } }),
                make ("Acoustic guitar", { { "threshold", 0.35f }, { "ratio", 0.4f }, { "attack", 20 }, { "release", 120 }, { "output gain", 2 } }),
                make ("Snare snap",     { { "threshold", 0.25f }, { "ratio", 0.25f }, { "attack", 30 }, { "release", 50 },  { "output gain", 3 } }),
                make ("Podcast voice",  { { "threshold", 0.2f },  { "ratio", 0.25f }, { "attack", 5 },  { "release", 120 }, { "output gain", 5 } }),
                make ("Mix bus",        { { "threshold", 0.7f },  { "ratio", 0.6f },  { "attack", 30 }, { "release", 300 }, { "output gain", 1 } }),
                make ("Brickwall",      { { "threshold", 0.2f },  { "ratio", 0.05f }, { "attack", 0.5f }, { "release", 40 }, { "output gain", 5 } }),
            };

        if (type == te::EqualiserPlugin::xmlTypeName)   // "Low-pass" and "High-pass" here are its low and high shelves
            return {
                make ("Vocal presence", { { "Low-pass freq", 100 }, { "Low-pass gain", -3 }, { "Mid freq 1", 3000 }, { "Mid gain 1", 2 }, { "Mid Q 1", 0.8f },
                                          { "High-pass freq", 12000 }, { "High-pass gain", 3 } }),
                make ("Warm",           { { "Low-pass freq", 120 }, { "Low-pass gain", 2 }, { "Mid freq 2", 5000 }, { "Mid gain 2", -2 } }),
                make ("Mud cut",        { { "Mid freq 1", 300 }, { "Mid gain 1", -4 }, { "Mid Q 1", 1.0f } }),
                make ("Air",            { { "High-pass freq", 14000 }, { "High-pass gain", 4 } }),
                make ("Kick",           { { "Low-pass freq", 60 }, { "Low-pass gain", 3 }, { "Mid freq 1", 350 }, { "Mid gain 1", -5 }, { "Mid Q 1", 1.2f },
                                          { "Mid freq 2", 4000 }, { "Mid gain 2", 3 }, { "Mid Q 2", 1.0f } }),
                make ("Snare",          { { "Low-pass freq", 200 }, { "Low-pass gain", 2 }, { "Mid freq 1", 800 }, { "Mid gain 1", -3 },
                                          { "Mid freq 2", 5000 }, { "Mid gain 2", 3 }, { "High-pass freq", 10000 }, { "High-pass gain", 2 } }),
                make ("Bass guitar",    { { "Low-pass freq", 80 }, { "Low-pass gain", 2 }, { "Mid freq 1", 250 }, { "Mid gain 1", -3 },
                                          { "Mid freq 2", 800 }, { "Mid gain 2", 2 }, { "Mid Q 2", 1.0f } }),
                make ("Guitar bite",    { { "Low-pass freq", 120 }, { "Low-pass gain", -4 }, { "Mid freq 1", 400 }, { "Mid gain 1", -2 },
                                          { "Mid freq 2", 2500 }, { "Mid gain 2", 3 }, { "Mid Q 2", 0.8f } }),
                make ("Acoustic sparkle", { { "Low-pass freq", 150 }, { "Low-pass gain", -3 }, { "High-pass freq", 10000 }, { "High-pass gain", 4 } }),
                make ("Telephone",      { { "Low-pass freq", 400 }, { "Low-pass gain", -15 }, { "Mid freq 1", 1500 }, { "Mid gain 1", 6 }, { "Mid Q 1", 0.7f },
                                          { "High-pass freq", 4000 }, { "High-pass gain", -15 } }),
                make ("Master smile",   { { "Low-pass freq", 90 }, { "Low-pass gain", 1.5f }, { "Mid freq 1", 400 }, { "Mid gain 1", -1 }, { "Mid Q 1", 0.6f },
                                          { "High-pass freq", 12000 }, { "High-pass gain", 1.5f } }),
            };

        if (type == te::ReverbPlugin::xmlTypeName)
            return {
                make ("Small room",  { { "room size", 0.3f },  { "damping", 0.5f },  { "wet level", 0.25f }, { "dry level", 0.7f },  { "width", 0.8f } }),
                make ("Plate",       { { "room size", 0.6f },  { "damping", 0.2f },  { "wet level", 0.3f },  { "dry level", 0.7f },  { "width", 1.0f } }),
                make ("Hall",        { { "room size", 0.85f }, { "damping", 0.4f },  { "wet level", 0.3f },  { "dry level", 0.7f },  { "width", 1.0f } }),
                make ("Ambience",    { { "room size", 0.2f },  { "damping", 0.6f },  { "wet level", 0.15f }, { "dry level", 0.8f },  { "width", 0.6f } }),
                make ("Vocal plate", { { "room size", 0.55f }, { "damping", 0.35f }, { "wet level", 0.22f }, { "dry level", 0.75f }, { "width", 0.9f } }),
                make ("Drum room",   { { "room size", 0.35f }, { "damping", 0.7f },  { "wet level", 0.2f },  { "dry level", 0.8f },  { "width", 1.0f } }),
                make ("Cathedral",   { { "room size", 0.97f }, { "damping", 0.3f },  { "wet level", 0.4f },  { "dry level", 0.6f },  { "width", 1.0f } }),
                make ("Dark space",  { { "room size", 0.9f },  { "damping", 0.9f },  { "wet level", 0.35f }, { "dry level", 0.65f }, { "width", 1.0f } }),
                make ("Return (all wet)", { { "room size", 0.7f }, { "damping", 0.4f }, { "wet level", 0.8f }, { "dry level", 0.0f }, { "width", 1.0f } }),
            };

        if (type == te::DelayPlugin::xmlTypeName)
            return {
                make ("Slapback",   { { "length", 110 }, { "feedback", -30 }, { "mix proportion", 0.25f } }),
                make ("Echo",       { { "length", 375 }, { "feedback", -8 },  { "mix proportion", 0.3f } }),
                make ("Long",       { { "length", 750 }, { "feedback", -4 },  { "mix proportion", 0.25f } }),
                make ("Doubler",    { { "length", 25 },  { "feedback", -30 }, { "mix proportion", 0.35f } }),
                make ("Dub echo",   { { "length", 500 }, { "feedback", -2 },  { "mix proportion", 0.35f } }),
                make ("Vocal throw", { { "length", 300 }, { "feedback", -6 }, { "mix proportion", 0.2f } }),
                make ("Return (all wet)", { { "length", 375 }, { "feedback", -6 }, { "mix proportion", 1.0f } }),
            };

        if (type == te::ChorusPlugin::xmlTypeName)
            return {
                make ("Subtle",  { { "depthMs", 2 }, { "speedHz", 0.5f }, { "width", 0.6f }, { "mixProportion", 0.3f } }),
                make ("Wide",    { { "depthMs", 4 }, { "speedHz", 0.8f }, { "width", 1.0f }, { "mixProportion", 0.5f } }),
                make ("Vibrato", { { "depthMs", 6 }, { "speedHz", 5.0f }, { "width", 0.5f }, { "mixProportion", 1.0f } }),
                make ("Guitar shimmer", { { "depthMs", 3 }, { "speedHz", 1.2f }, { "width", 0.9f }, { "mixProportion", 0.4f } }),
                make ("Slow wobble",    { { "depthMs", 8 }, { "speedHz", 0.25f }, { "width", 0.8f }, { "mixProportion", 0.5f } }),
            };

        if (type == te::PhaserPlugin::xmlTypeName)
            return {
                make ("Slow sweep", { { "depth", 5 }, { "rate", 0.2f }, { "feedback", 0.6f } }),
                make ("Fast",       { { "depth", 6 }, { "rate", 2.0f }, { "feedback", 0.7f } }),
                make ("Gentle",     { { "depth", 3 }, { "rate", 0.4f }, { "feedback", 0.3f } }),
                make ("Jet",        { { "depth", 8 }, { "rate", 0.1f }, { "feedback", 0.9f } }),
            };

        if (type == te::LowPassPlugin::xmlTypeName)
            return {
                make ("Muffled",       { { "frequency", 800 },  { "mode", 0, "lowpass" } }),
                make ("Through a wall", { { "frequency", 350 }, { "mode", 0, "lowpass" } }),
                make ("Dull top",      { { "frequency", 6000 }, { "mode", 0, "lowpass" } }),
                make ("Thin",          { { "frequency", 500 },  { "mode", 0, "highpass" } }),
                make ("Rumble off",    { { "frequency", 80 },   { "mode", 0, "highpass" } }),
            };

        if (type == "ezgate")
            return {
                make ("Tight drums",  { { "threshold", -35 }, { "floor", -80 }, { "attack", 0.5f }, { "hold", 20 }, { "release", 60 } }),
                make ("Gentle vocal", { { "threshold", -50 }, { "floor", -20 }, { "attack", 2 },    { "hold", 80 }, { "release", 200 } }),
                make ("Kick",         { { "threshold", -30 }, { "floor", -80 }, { "attack", 0.3f }, { "hold", 30 }, { "release", 80 } }),
                make ("Toms",         { { "threshold", -30 }, { "floor", -60 }, { "attack", 0.5f }, { "hold", 100 }, { "release", 250 } }),
                make ("Guitar noise", { { "threshold", -55 }, { "floor", -80 }, { "attack", 1 },    { "hold", 30 }, { "release", 100 } }),
            };

        if (type == "ezlimiter")
            return {
                make ("Safety",  { { "input", 0 },  { "ceiling", -1.0f }, { "release", 80 } }),
                make ("Loud",    { { "input", 6 },  { "ceiling", -0.3f }, { "release", 50 } }),
                make ("Gentle",  { { "input", 2 },  { "ceiling", -1.0f }, { "release", 150 } }),
                make ("Streaming master", { { "input", 4 }, { "ceiling", -1.0f }, { "release", 100 } }),
                make ("Smash",   { { "input", 12 }, { "ceiling", -0.5f }, { "release", 30 } }),
            };

        if (type == "ezdeesser")
            return {
                make ("Gentle",     { { "frequency", 6500 }, { "threshold", -30 }, { "most", -6 } }),
                make ("Strong",     { { "frequency", 7000 }, { "threshold", -38 }, { "most", -12 } }),
                make ("Male voice", { { "frequency", 5500 }, { "threshold", -32 }, { "most", -8 } }),
                make ("Female voice", { { "frequency", 7500 }, { "threshold", -32 }, { "most", -8 } }),
                make ("Cymbals",    { { "frequency", 9000 }, { "threshold", -28 }, { "most", -6 } }),
            };

        if (type == "ezcutfilter")
            return {
                make ("Rumble only", { { "lowcut", 80 },  { "highcut", 20000 } }),
                make ("Telephone",   { { "lowcut", 400 }, { "highcut", 3500 } }),
                make ("Lo-fi",       { { "lowcut", 150 }, { "highcut", 8000 } }),
                make ("Vocal",       { { "lowcut", 100 }, { "highcut", 18000 } }),
                make ("Guitar",      { { "lowcut", 80 },  { "highcut", 12000 } }),
                make ("Hi-hats",     { { "lowcut", 400 }, { "highcut", 20000 } }),
                make ("Radio",       { { "lowcut", 300 }, { "highcut", 5000 } }),
            };

        if (type == "ezsaturator")
            return {
                make ("Warm tape", { { "drive", 6 },  { "curve", 0 }, { "tone", 12000 }, { "mix", 100 }, { "output", -2 } }),
                make ("Tube",      { { "drive", 10 }, { "curve", 1 }, { "tone", 10000 }, { "mix", 80 },  { "output", -4 } }),
                make ("Crush",     { { "drive", 24 }, { "curve", 2 }, { "tone", 8000 },  { "mix", 100 }, { "output", -10 } }),
                make ("Subtle glue", { { "drive", 3 }, { "curve", 0 }, { "tone", 16000 }, { "mix", 100 }, { "output", -1 } }),
                make ("Bass grit",   { { "drive", 12 }, { "curve", 1 }, { "tone", 6000 }, { "mix", 50 }, { "output", -4 } }),
                make ("Drum smash",  { { "drive", 18 }, { "curve", 2 }, { "tone", 12000 }, { "mix", 40 }, { "output", -4 } }),
                make ("Lo-fi",       { { "drive", 14 }, { "curve", 0 }, { "tone", 4000 }, { "mix", 100 }, { "output", -6 } }),
            };

        if (type == "ezamp")
            return {
                make ("Clean",  { { "mode", 0 }, { "gain", 3 } }),
                make ("Crunch", { { "mode", 1 }, { "gain", 5 } }),
                make ("Punk",   { { "mode", 2 }, { "gain", 7 } }),
                make ("Lead",   { { "mode", 3 }, { "gain", 7.5f }, { "mid", 2 } }),
                make ("Metal",  { { "mode", 4 }, { "gain", 8 }, { "gate", -55 }, { "tight", 120 }, { "mid", -3 } }),
                make ("Bass",   { { "mode", 5 }, { "gain", 4 }, { "cab", 4 } }),
                make ("Jazz clean", { { "mode", 0 }, { "gain", 2 }, { "bass", 2 }, { "treble", -3 }, { "cab", 3 } }),
                make ("Blues",      { { "mode", 1 }, { "gain", 6 }, { "mid", 3 }, { "cab", 2 } }),
                make ("Djent",      { { "mode", 4 }, { "gain", 9 }, { "gate", -50 }, { "tight", 180 }, { "bass", -2 }, { "mid", -4 }, { "presence", 4 } }),
                make ("Bass grind", { { "mode", 5 }, { "gain", 7 }, { "cab", 4 }, { "mid", 3 }, { "blend", 40 } }),
                make ("DI (no cab)", { { "mode", 0 }, { "gain", 2 }, { "cab", 0 } }),
            };

        if (type == "eztremolo")
            return {
                make ("Classic",  { { "rate", 5 },   { "depth", 50 }, { "mode", 0 } }),
                make ("Autopan",  { { "rate", 2 },   { "depth", 80 }, { "mode", 1 } }),
                make ("Slow pulse", { { "rate", 1 }, { "depth", 40 }, { "mode", 0 } }),
                make ("Helicopter", { { "rate", 12 }, { "depth", 90 }, { "mode", 0 } }),
                make ("Gentle pan", { { "rate", 0.3f }, { "depth", 40 }, { "mode", 1 } }),
            };

        if (type == "ezutility")
            return {
                make ("Mono",          { { "mono", 1 } }),
                make ("Wide",          { { "width", 150 } }),
                make ("Narrow",        { { "width", 60 } }),
                make ("Quieter (-6 dB)", { { "gain", -6 } }),
                make ("Louder (+6 dB)",  { { "gain", 6 } }),
                make ("Flip phase",    { { "flipl", 1 }, { "flipr", 1 } }),
            };

        if (type == "eztune")   // key and scale follow your song: these only set how hard and fast
            return {
                make ("Natural",  { { "amount", 60 },  { "speed", 80 } }),
                make ("Tight",    { { "amount", 90 },  { "speed", 30 } }),
                make ("Robot (hard tune)", { { "amount", 100 }, { "speed", 1 } }),
                make ("Light touch", { { "amount", 35 }, { "speed", 150 } }),
            };

        if (type == "ezarpeggiator")
            return {
                make ("Up 1/16",       { { "rate", 2 }, { "mode", 0 }, { "octaves", 1 }, { "gate", 60 } }),
                make ("Trance 2 octaves", { { "rate", 2 }, { "mode", 0 }, { "octaves", 2 }, { "gate", 40 } }),
                make ("Up & down 1/8", { { "rate", 1 }, { "mode", 2 }, { "octaves", 2 }, { "gate", 70 } }),
                make ("Random sparkle", { { "rate", 3 }, { "mode", 4 }, { "octaves", 3 }, { "gate", 30 } }),
                make ("Triplet roll",  { { "rate", 5 }, { "mode", 0 }, { "octaves", 1 }, { "gate", 50 } }),
                make ("Held chords (latch)", { { "rate", 2 }, { "mode", 3 }, { "octaves", 1 }, { "gate", 60 }, { "latch", 1 } }),
            };

        return {};
    }

    juce::StringArray getTypesWithFactoryPresets()
    {
        return { te::FourOscPlugin::xmlTypeName, te::CompressorPlugin::xmlTypeName, te::EqualiserPlugin::xmlTypeName,
                 te::ReverbPlugin::xmlTypeName, te::DelayPlugin::xmlTypeName, te::ChorusPlugin::xmlTypeName,
                 te::PhaserPlugin::xmlTypeName, te::LowPassPlugin::xmlTypeName,
                 "ezgate", "ezlimiter", "ezdeesser", "ezcutfilter", "ezsaturator", "ezamp", "eztremolo", "ezutility", "eztune", "ezarpeggiator" };
    }

    std::optional<Preset> findFactory (const juce::String& type, const juce::String& name)
    {
        for (auto& p : getFactory (type))
            if (p.name == name)
                return p;

        return std::nullopt;
    }

    //==============================================================================
    juce::File getUserFolder (const juce::String& type)
    {
        return AppPaths::getRootFolder().getChildFile ("Presets").getChildFile ("Plugins")
                   .getChildFile (juce::File::createLegalFileName (type));
    }

    std::vector<Preset> getUser (const juce::String& type)
    {
        std::vector<Preset> presets;

        for (const auto& file : getUserFolder (type).findChildFiles (juce::File::findFiles, false, "*.ezpreset"))
        {
            const auto xml = juce::XmlDocument::parse (file);

            if (xml == nullptr || ! xml->hasTagName ("EZPRESET"))
                continue;

            Preset p;
            p.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
            p.file = file;

            for (auto* e : xml->getChildWithTagNameIterator ("VALUE"))
                p.values.push_back ({ e->getStringAttribute ("id"), (float) e->getDoubleAttribute ("value"), e->getStringAttribute ("text") });

            presets.push_back (std::move (p));
        }

        std::sort (presets.begin(), presets.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
        return presets;
    }

    bool saveUser (te::Plugin& plugin, const juce::String& name, const juce::StringArray& settingIds, juce::String& error)
    {
        const auto clean = name.trim();

        if (clean.isEmpty())
        {
            error = "Give the preset a name";
            return false;
        }

        const auto folder = getUserFolder (plugin.getPluginType());

        if (! folder.createDirectory())
        {
            error = "Could not create " + folder.getFullPathName();
            return false;
        }

        juce::XmlElement xml ("EZPRESET");
        xml.setAttribute ("name", clean);
        xml.setAttribute ("type", plugin.getPluginType());

        for (const auto& v : capture (plugin, settingIds))
        {
            auto* e = xml.createNewChildElement ("VALUE");
            e->setAttribute ("id", v.id);
            e->setAttribute ("value", (double) v.value);

            if (v.text.isNotEmpty())
                e->setAttribute ("text", v.text);
        }

        const auto file = folder.getChildFile (juce::File::createLegalFileName (clean) + ".ezpreset");

        if (! xml.writeTo (file))
        {
            error = "Could not write " + file.getFullPathName();
            return false;
        }

        return true;
    }

    bool deleteUser (const Preset& preset)
    {
        return ! preset.isFactory && preset.file.existsAsFile() && preset.file.moveToTrash();
    }

    juce::String getCurrentName (te::Plugin& plugin)
    {
        return plugin.state[currentNameProp].toString();
    }

    void setCurrentName (te::Plugin& plugin, const juce::String& name, bool withTheSound)
    {
        // Not an undo step of its own: undoing a sound change shouldn't need a second undo for its label
        plugin.state.setProperty (currentNameProp, name, withTheSound ? &plugin.edit.getUndoManager() : nullptr);
    }
}
