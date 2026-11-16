/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The pictures in plugin editors: envelopes, filter and EQ curves, dynamics curves, waveforms,
    delay taps, reverb tails, a keyboard for the tuner's scale. Each reads the plugin's values by
    id through Values (the editor's own lookup) and redraws only when one of them changes.

    Some can be dragged, changing the same values the knobs do (one undo step per drag):
      Envelope      drag a point: attack, decay + sustain, release
      EQ            drag a band: frequency and gain; wheel: Q; double-click: gain back to 0
      Filter        drag: cut-off (left/right) and resonance (up/down)
*/

#pragma once

#include "../Core/Common.h"

namespace ez::PluginDisplays
{
    /** How a display reads and changes the plugin (implemented by the editor). */
    struct Values
    {
        virtual ~Values() = default;
        virtual float get (const juce::String& id) const = 0;
        virtual juce::NormalisableRange<float> rangeOf (const juce::String& id) const = 0;
        virtual void beginDrag (const juce::String& id) = 0;
        virtual void set (const juce::String& id, float value) = 0;
        virtual void endDrag (const juce::String& id) = 0;
        virtual double getBpm() const = 0;
    };

    enum class Type
    {
        envelope,       // ids: attack, decay, sustain (0-100), release; times in seconds
        synthFilter,    // ids: type (0 off, 1 LP, 2 HP, 3 BP, 4 notch), freq (MIDI note), resonance (0-100), slope (12/24)
        oscWave,        // ids: shape (0 off, 1 sine, 2 square, 3 saw, 4 triangle, 5 noise), pulse width, voices, detune
        lfo,            // ids: shape (FourOsc LFO: 0 off, 1 sine, 2 tri, 3 saw up, 4 saw down, 5 square, 6 random), rate (Hz), depth (0-1)
        wobble,         // ids: rate (Hz), depth (0-1 or 0-100), [mode]: chorus, phaser, tremolo
        eq4,            // Tracktion's 4-band EQ (its paramIDs)
        compressor,     // ids: threshold (linear), ratio (0-0.95), output gain (dB)
        gate,           // ids: threshold, floor (dB)
        limiter,        // ids: input, ceiling (dB)
        deEsser,        // ids: frequency, threshold, most
        cutFilter,      // ids: low cut, high cut (Hz)
        lowpass,        // ids: frequency (Hz), mode setting ("lowpass"/"highpass")
        saturator,      // ids: drive (dB), curve (0 tape, 1 tube, 2 hard), mix (0-100)
        delay,          // ids: length (ms), feedback (dB), mix (0-1)
        reverb,         // ids: room size, damping, wet, width, freeze
        keys,           // ids: root (0-11), scale (0 chromatic, 1 major, 2 minor)
        stereo,         // ids: width (0-200), mono, flip left, flip right
        ampTone         // ids: tight, bass, mid, treble, presence
    };

    /** A display that redraws when the values it shows change (call refresh() from a timer). */
    class Display : public juce::Component
    {
    public:
        Display (Values&, juce::StringArray ids);
        void refresh();

    protected:
        Values& values;
        juce::StringArray ids;
        float v (int index) const     { return juce::isPositiveAndBelow (index, ids.size()) ? values.get (ids[index]) : 0.0f; }

        /** The plot area inside the well, and the well itself. */
        juce::Rectangle<float> plotArea() const;
        void drawBackground (juce::Graphics&);
        void drawFrequencyGrid (juce::Graphics&, juce::Rectangle<float>, float dbRange);
        void drawDbGrid (juce::Graphics&, juce::Rectangle<float>, float minDb, float maxDb);

        static float xForHz (juce::Rectangle<float> area, float hz);
        static float hzForX (juce::Rectangle<float> area, float x);

    private:
        std::vector<float> last;
    };

    std::unique_ptr<Display> create (Type, Values&, juce::StringArray ids);

    //==============================================================================
    /** Biquad magnitudes (RBJ cookbook) in dB at a frequency, for the curve displays and tests. */
    namespace Response
    {
        float lowShelf (float hz, float f0, float gainDb, float q);
        float highShelf (float hz, float f0, float gainDb, float q);
        float peak (float hz, float f0, float gainDb, float q);
        float lowPass (float hz, float f0, float q);
        float highPass (float hz, float f0, float q);
        float bandPass (float hz, float f0, float q);
        float notch (float hz, float f0, float q);
    }
}
