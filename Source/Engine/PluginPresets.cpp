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

        /** Whole numbers stay whole: the synth keeps its waveforms and switches as ints. */
        juce::var asVar (float value)
        {
            if (std::abs (value - std::round (value)) < 1.0e-6f && std::abs (value) < 1.0e6f)
                return juce::var ((int) std::round (value));

            return juce::var ((double) value);
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
        if (resetFirst)
            resetToDefaults (plugin, settingIds);

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
    std::vector<Preset> getFactory (const juce::String& type)
    {
        if (type == te::FourOscPlugin::xmlTypeName)
            return {
                make ("Warm pad",   { { "waveShape1", 3 }, { "voices1", 4 }, { "detune1", 0.15f }, { "spread1", 50 },
                                      { "waveShape2", 3 }, { "tune2", -12 }, { "level2", -6 },
                                      { "ampAttack", 0.8f }, { "ampDecay", 1.0f }, { "ampSustain", 80 }, { "ampRelease", 1.5f },
                                      { "filterType", 1 }, { "filterFreq", 72 }, { "filterResonance", 10 },
                                      { "chorusOn", 1 }, { "chorusMix", 0.3f }, { "reverbOn", 1 }, { "reverbSize", 0.7f }, { "reverbMix", 0.3f },
                                      { "masterLevel", -6 } }),
                make ("Pluck",      { { "waveShape1", 3 }, { "ampAttack", 0.001f }, { "ampDecay", 0.35f }, { "ampSustain", 0 }, { "ampRelease", 0.2f },
                                      { "filterType", 1 }, { "filterFreq", 60 }, { "filterResonance", 20 }, { "filterAmount", 0.5f },
                                      { "filterAttack", 0.001f }, { "filterDecay", 0.25f }, { "filterSustain", 0 },
                                      { "delayOn", 1 }, { "delayMix", 0.2f }, { "masterLevel", -4 } }),
                make ("Keys",       { { "waveShape1", 4 }, { "waveShape2", 1 }, { "tune2", 12 }, { "level2", -10 },
                                      { "ampAttack", 0.003f }, { "ampDecay", 1.2f }, { "ampSustain", 30 }, { "ampRelease", 0.4f },
                                      { "reverbOn", 1 }, { "reverbSize", 0.4f }, { "reverbMix", 0.2f }, { "masterLevel", -4 } }),
                make ("Lead",       { { "waveShape1", 2 }, { "waveShape2", 3 }, { "tune2", 12 }, { "level2", -8 },
                                      { "voiceMode", 1 }, { "legato", 40 }, { "ampAttack", 0.01f }, { "ampSustain", 90 },
                                      { "filterType", 1 }, { "filterFreq", 95 }, { "filterResonance", 25 },
                                      { "delayOn", 1 }, { "delayMix", 0.15f }, { "masterLevel", -6 } }),
                make ("Sub bass",   { { "waveShape1", 1 }, { "ampAttack", 0.005f }, { "ampDecay", 0.3f }, { "ampSustain", 100 }, { "ampRelease", 0.1f },
                                      { "voiceMode", 0 }, { "masterLevel", -3 } }),
                make ("808 slide",  { { "waveShape1", 1 }, { "level1", 0 }, { "waveShape2", 0 },
                                      { "ampAttack", 0.001f }, { "ampDecay", 2.5f }, { "ampSustain", 35 }, { "ampRelease", 0.3f },
                                      { "voiceMode", 1 }, { "legato", 90 }, { "distortionOn", 1 }, { "distortion", 0.25f },
                                      { "filterType", 0 }, { "masterLevel", -4 } }),
                make ("Reese bass", { { "waveShape1", 3 }, { "voices1", 4 }, { "detune1", 0.22f }, { "spread1", 60 }, { "level1", -3 },
                                      { "waveShape2", 3 }, { "tune2", -12 }, { "voices2", 2 }, { "detune2", 0.12f }, { "level2", -6 },
                                      { "ampAttack", 0.005f }, { "ampSustain", 100 }, { "ampRelease", 0.15f },
                                      { "filterType", 1 }, { "filterFreq", 78 }, { "filterResonance", 25 },
                                      { "voiceMode", 1 }, { "legato", 40 }, { "masterLevel", -8 } }),
            };

        if (type == te::CompressorPlugin::xmlTypeName)
            return {
                make ("Gentle glue",  { { "threshold", 0.5f },  { "ratio", 0.5f },  { "attack", 30 }, { "release", 150 }, { "output gain", 1 } }),
                make ("Vocal",        { { "threshold", 0.25f }, { "ratio", 0.67f }, { "attack", 10 }, { "release", 80 },  { "output gain", 3 } }),
                make ("Punchy drums", { { "threshold", 0.3f },  { "ratio", 0.75f }, { "attack", 25 }, { "release", 60 },  { "output gain", 2 } }),
                make ("Squash",       { { "threshold", 0.1f },  { "ratio", 0.9f },  { "attack", 1 },  { "release", 50 },  { "output gain", 6 } }),
            };

        if (type == te::EqualiserPlugin::xmlTypeName)
            return {
                make ("Vocal presence", { { "Low-pass freq", 100 }, { "Low-pass gain", -3 }, { "Mid freq 1", 3000 }, { "Mid gain 1", 2 }, { "Mid Q 1", 0.8f },
                                          { "High-pass freq", 12000 }, { "High-pass gain", 3 } }),
                make ("Warm",           { { "Low-pass freq", 120 }, { "Low-pass gain", 2 }, { "Mid freq 2", 5000 }, { "Mid gain 2", -2 } }),
                make ("Mud cut",        { { "Mid freq 1", 300 }, { "Mid gain 1", -4 }, { "Mid Q 1", 1.0f } }),
                make ("Air",            { { "High-pass freq", 14000 }, { "High-pass gain", 4 } }),
            };

        if (type == te::ReverbPlugin::xmlTypeName)
            return {
                make ("Small room", { { "room size", 0.3f }, { "damping", 0.5f }, { "wet level", 0.25f }, { "dry level", 0.7f }, { "width", 0.8f } }),
                make ("Plate",      { { "room size", 0.6f }, { "damping", 0.2f }, { "wet level", 0.3f },  { "dry level", 0.7f }, { "width", 1.0f } }),
                make ("Hall",       { { "room size", 0.85f }, { "damping", 0.4f }, { "wet level", 0.3f }, { "dry level", 0.7f }, { "width", 1.0f } }),
                make ("Ambience",   { { "room size", 0.2f }, { "damping", 0.6f }, { "wet level", 0.15f }, { "dry level", 0.8f }, { "width", 0.6f } }),
            };

        if (type == te::DelayPlugin::xmlTypeName)
            return {
                make ("Slapback", { { "length", 110 }, { "feedback", -30 }, { "mix proportion", 0.25f } }),
                make ("Echo",     { { "length", 375 }, { "feedback", -8 },  { "mix proportion", 0.3f } }),
                make ("Long",     { { "length", 750 }, { "feedback", -4 },  { "mix proportion", 0.25f } }),
            };

        if (type == te::ChorusPlugin::xmlTypeName)
            return {
                make ("Subtle",  { { "depthMs", 2 }, { "speedHz", 0.5f }, { "width", 0.6f }, { "mixProportion", 0.3f } }),
                make ("Wide",    { { "depthMs", 4 }, { "speedHz", 0.8f }, { "width", 1.0f }, { "mixProportion", 0.5f } }),
                make ("Vibrato", { { "depthMs", 6 }, { "speedHz", 5.0f }, { "width", 0.5f }, { "mixProportion", 1.0f } }),
            };

        if (type == te::PhaserPlugin::xmlTypeName)
            return {
                make ("Slow sweep", { { "depth", 5 }, { "rate", 0.2f }, { "feedback", 0.6f } }),
                make ("Fast",       { { "depth", 6 }, { "rate", 2.0f }, { "feedback", 0.7f } }),
            };

        if (type == "ezgate")
            return {
                make ("Tight drums",  { { "threshold", -35 }, { "floor", -80 }, { "attack", 0.5f }, { "hold", 20 }, { "release", 60 } }),
                make ("Gentle vocal", { { "threshold", -50 }, { "floor", -20 }, { "attack", 2 },    { "hold", 80 }, { "release", 200 } }),
            };

        if (type == "ezlimiter")
            return {
                make ("Safety", { { "input", 0 }, { "ceiling", -1.0f }, { "release", 80 } }),
                make ("Loud",   { { "input", 6 }, { "ceiling", -0.3f }, { "release", 50 } }),
            };

        if (type == "ezdeesser")
            return {
                make ("Gentle", { { "frequency", 6500 }, { "threshold", -30 }, { "most", -6 } }),
                make ("Strong", { { "frequency", 7000 }, { "threshold", -38 }, { "most", -12 } }),
            };

        if (type == "ezcutfilter")
            return {
                make ("Rumble only", { { "lowcut", 80 },  { "highcut", 20000 } }),
                make ("Telephone",   { { "lowcut", 400 }, { "highcut", 3500 } }),
                make ("Lo-fi",       { { "lowcut", 150 }, { "highcut", 8000 } }),
            };

        if (type == "ezsaturator")
            return {
                make ("Warm tape", { { "drive", 6 },  { "curve", 0 }, { "tone", 12000 }, { "mix", 100 }, { "output", -2 } }),
                make ("Tube",      { { "drive", 10 }, { "curve", 1 }, { "tone", 10000 }, { "mix", 80 },  { "output", -4 } }),
                make ("Crush",     { { "drive", 24 }, { "curve", 2 }, { "tone", 8000 },  { "mix", 100 }, { "output", -10 } }),
            };

        if (type == "ezamp")
            return {
                make ("Clean",  { { "mode", 0 }, { "gain", 3 } }),
                make ("Crunch", { { "mode", 1 }, { "gain", 5 } }),
                make ("Punk",   { { "mode", 2 }, { "gain", 7 } }),
                make ("Lead",   { { "mode", 3 }, { "gain", 7.5f }, { "mid", 2 } }),
                make ("Metal",  { { "mode", 4 }, { "gain", 8 }, { "gate", -55 }, { "tight", 120 }, { "mid", -3 } }),
                make ("Bass",   { { "mode", 5 }, { "gain", 4 }, { "cab", 4 } }),
            };

        if (type == "eztremolo")
            return {
                make ("Classic", { { "rate", 5 }, { "depth", 50 }, { "mode", 0 } }),
                make ("Autopan", { { "rate", 2 }, { "depth", 80 }, { "mode", 1 } }),
            };

        return {};
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

    void setCurrentName (te::Plugin& plugin, const juce::String& name)
    {
        // Not an undo step: undoing a sound change shouldn't need a second undo for its label
        plugin.state.setProperty (currentNameProp, name, nullptr);
    }
}
