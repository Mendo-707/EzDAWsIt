/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    How each built-in plugin's editor is laid out: pages of rows of sections (cards). A section
    has a title, maybe an on/off switch, maybe a display (PluginDisplays), and its controls.

    A control's id is a parameter (paramID) or a setting: a value Tracktion keeps only in the
    plugin's state (the synth's waveforms, the delay time, the chorus...). Settings are listed
    with their range so the editor can show them, and presets and Reset include them.
*/

#pragma once

#include "PluginDisplays.h"

namespace ez::PluginLayouts
{
    struct Setting
    {
        juce::String id, name;
        float min = 0.0f, max = 1.0f, defaultValue = 0.0f;
        float interval = 0.0f;              // 1: whole numbers
        juce::StringArray labels;           // a choice: shown by name
        std::vector<float> choiceValues;    // the stored value for each label (default: min, min + 1...)
        juce::StringArray textValues;       // stored as text instead (the filter's "lowpass"/"highpass")
        juce::String unit;                  // shown after the number
        int decimals = 0;
        float skewCentre = 0.0f;            // the value at the middle of a knob (0 = linear)
        float displayScale = 1.0f;          // shown as value * scale ("0.5" as "50 %")
        bool isToggle = false;
    };

    struct Item
    {
        enum class Size { small, normal, large };
        enum class Style { automatic, knob, toggle, menu, buttons };

        juce::String id, label;
        Size size = Size::normal;
        Style style = Style::automatic;
    };

    struct Section
    {
        Section() = default;
        explicit Section (juce::String titleToUse, juce::String powerToUse = {})
            : title (std::move (titleToUse)), powerId (std::move (powerToUse)) {}

        juce::String title;
        juce::String powerId;                       // a toggle in the title bar: the section is on or off
        std::optional<PluginDisplays::Type> display;
        juce::StringArray displayIds;
        int displayHeight = 64;
        std::vector<Item> items;
        int columns = 4;                            // knobs per row
        float weight = 1.0f;                        // share of the row's width
        juce::String custom;                        // a special body: "modMatrix", "delaySync"
        juce::String tip;                           // tooltip on the title
    };

    struct Page
    {
        juce::String name;
        std::vector<std::vector<Section>> rows;
    };

    struct Layout
    {
        std::vector<Page> pages;
        std::vector<Setting> settings;
        int width = 640;
        juce::String description;   // under the name in the header
    };

    /** The layout for a built-in plugin, or nothing (the editor then lays out every parameter). */
    std::optional<Layout> forPlugin (te::Plugin&);

    /** The ids of a layout's settings (what presets and Reset include besides parameters). */
    juce::StringArray settingIds (const Layout&);

    /** Tidies a plug-in's own value text for the knob label: "0.000dB" -> "0 dB",
        "0.000%" -> "0 %", "0R" -> "C", "17000 Hz" -> "17.0 kHz". Text that isn't a number is returned unchanged. */
    juce::String tidyValueText (const juce::String&);
}
