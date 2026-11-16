/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The built-in drum kits. Every sound is made by code (no sample files are
    shipped, nothing is borrowed), written as a .wav the first time a kit is
    used, into Documents\EzDAWsIt\Drum Kits\. The same kit always gives the same
    sounds, so a deleted file is simply made again.

    The 16 pads have the same roles in every kit (pad 1 is always the kick),
    so a beat keeps working when you swap kits.
*/

#pragma once

#include "../Core/Common.h"

namespace ez::DrumKits
{
    constexpr int numPads = 16;

    struct KitInfo
    {
        const char* name;
        const char* genre;
        const char* description;
    };

    int getNumKits();
    const KitInfo& getKit (int kitIndex);      // kitIndex must be valid
    int findKit (const juce::String& name);    // -1 if there is no kit with that name

    /** "Hip hop", "Drum & bass", ... in the order the kits list them. */
    juce::StringArray getGenres();

    juce::String getPadName (int pad);
    juce::Colour getPadColour (int pad);

    /** Pads that cut each other off (closed hat stops the open hat): 0 = none. */
    int getDefaultChokeGroup (int pad);

    /** Where the kits are written. */
    juce::File getFolder();

    /** The .wav for one pad of one kit, made now if it is missing. Returns {} and sets error if it
        can't be written. Message thread. */
    juce::File getSampleFile (int kitIndex, int pad, juce::String& error);
}
