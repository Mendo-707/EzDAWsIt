/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Built-in MIDI patterns for the browser's "MIDI patterns" category: chord progressions, basslines,
    melodies and arps, and drum beats by style. Melodic ones are written from the root (0 = the key's
    root) and land in the song's key; drum beats play the Drum Machine's pads (DrumMachinePlugin::noteForPad).

    No audio, no UI: just the notes.
*/

#pragma once

#include "../Core/Common.h"

namespace ez::MidiPatterns
{
    struct Note
    {
        int pitch;          // melodic: semitones from the key's root, 0 = the root in octave 4 (MIDI 60); drums: the pad (0-15)
        double start;       // beats from the pattern's start
        double length;      // beats
        int velocity = 100;
    };

    enum class Kind { chords, bass, melody, drums };

    struct Pattern
    {
        juce::String name, description;
        Kind kind = Kind::chords;
        bool minor = false;          // melodic: written in a minor key
        double lengthBeats = 16.0;   // in 4/4: 16 = four bars
        std::vector<Note> notes;

        juce::String categoryName() const;
    };

    /** Every built-in pattern, grouped by kind (chords, basslines, melodies, drums), in a fixed order. */
    const std::vector<Pattern>& getAll();

    /** By name (nullptr if there's none). */
    const Pattern* find (const juce::String& name);

    /** The kinds in browser order, with their section titles. */
    juce::String kindName (Kind);

    /** How far to move a melodic pattern so its root is the song's key root (0-11; -1 = no key: C).
        Stays within a few semitones of where it was written (never more than a fifth up). */
    int transposeFor (int keyRoot);

    /** The MIDI note of a pattern note, as it lands: melodic ones transposed, drum pads to the Drum Machine's notes. */
    int midiNoteFor (const Pattern&, const Note&, int keyRoot);

    /** Which built-in synth sound suits a melodic pattern on a new track (PluginPresets name). */
    juce::String suggestedSound (const Pattern&);
}
