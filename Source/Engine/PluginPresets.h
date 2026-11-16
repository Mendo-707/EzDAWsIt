/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Presets for a single plugin (an effect or the synth): factory sounds built in, and the
    user's own, saved as small XML files in Documents\EzDAWsIt\Presets\Plugins\<type>.

    A preset is a list of values by id. An id is either one of the plugin's automatable
    parameters (paramID) or a setting stored only in its state (Tracktion keeps some, like the
    synth's waveforms or the delay time, as plain properties). Applying one is a single undo step.
*/

#pragma once

#include "../Core/Common.h"

namespace ez::PluginPresets
{
    struct Value
    {
        Value() = default;
        Value (juce::String idToUse, float valueToUse, juce::String textToUse = {})
            : id (std::move (idToUse)), value (valueToUse), text (std::move (textToUse)) {}

        juce::String id;
        float value = 0.0f;
        juce::String text;   // for settings stored as text (the filter's "lowpass"/"highpass")
    };

    struct Preset
    {
        juce::String name;
        std::vector<Value> values;
        bool isFactory = false;
        juce::File file;   // user presets
    };

    /** Every parameter's value, plus the given state-only settings. */
    std::vector<Value> capture (te::Plugin&, const juce::StringArray& settingIds);

    /** Sets each value (parameters, then settings). With resetFirst, everything named in
        settingIds and every parameter goes back to its default first, so a preset sounds the
        same whatever came before. Call inside an undo transaction. */
    void apply (te::Plugin&, const std::vector<Value>&, const juce::StringArray& settingIds, bool resetFirst);

    /** Every parameter to its default and every listed setting removed (its default applies). */
    void resetToDefaults (te::Plugin&, const juce::StringArray& settingIds);

    /** Built-in sounds for this plugin type (may be empty). */
    std::vector<Preset> getFactory (const juce::String& pluginType);

    /** The user's presets for this plugin type, by name. */
    std::vector<Preset> getUser (const juce::String& pluginType);

    juce::File getUserFolder (const juce::String& pluginType);

    /** Saves the plugin's current sound under a name (replacing one of the same name). */
    bool saveUser (te::Plugin&, const juce::String& name, const juce::StringArray& settingIds, juce::String& error);

    bool deleteUser (const Preset&);

    /** The preset last loaded into this plugin, kept in its state (not an undo step). */
    juce::String getCurrentName (te::Plugin&);
    void setCurrentName (te::Plugin&, const juce::String&);
}
