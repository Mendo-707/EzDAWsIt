/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Our own built-in effects (on top of Tracktion's EQ, compressor, reverb, delay, chorus,
    phaser and filter). Each control is a saved, undoable, automatable parameter, so every
    effect works with the generic editor window, automation lanes and the Vocal Chain.

      Gate          Dynamics      cuts noise between phrases
      De-esser      Dynamics      tames harsh "s" sounds
      Limiter       Dynamics      nothing goes over the ceiling (1.5 ms look-ahead)
      Low/high cut  EQ & Filter   removes rumble and/or top end
      Saturator     Colour        tape / tube / hard drive, tone, mix
      Tune          Pitch         pulls notes to the song's key
      Tremolo       Modulation    level or left-right wobble
      Utility       Utility       gain, stereo width, mono, phase flip
      Amp           Guitar & bass a guitar or bass amp and cabinet: clean to metal, with a gate
      Tuner         Guitar & bass shows the note you play (the Tuner window); the sound passes untouched
      Vocal Chain   Chains        a whole vocal sound run by six macros (VocalChain.h)

    Registered in AudioEngine (createBuiltInType) before any song loads one.
*/

#pragma once

#include "Dsp.h"

namespace ez
{
    /** Base for our effects: a table of controls (each a CachedValue + AutomatableParameter) and a
        process() that sees at most 2 channels. Subclasses only describe controls and process. */
    class EffectPlugin : public te::Plugin
    {
    public:
        struct Control
        {
            const char* id;
            const char* name;
            float min, max, defaultValue;
            float centre = 0;                // skew: the value at the middle of a knob (0 = linear)
            const char* unit = "";
            int decimals = 1;
            juce::StringArray choices = {};  // a switch: values 0..n-1 shown by name
            const char* atMin = nullptr;     // shown instead of the number at the bottom of the range ("Off")
        };

        EffectPlugin (te::PluginCreationInfo, std::vector<Control>);
        ~EffectPlugin() override;

        /** The control's value now (follows automation). Any thread. */
        float get (int index) const noexcept                { return params[(size_t) index]->getCurrentValue(); }   // (index from the subclass's enum)
        /** Stored value (what the user set); message thread, undoable. */
        void set (int index, float value);
        /** The stored value now (what is saved; the same as get() unless a lane is playing). */
        float getStored (int index) const       { return juce::isPositiveAndBelow (index, values.size()) ? values[index]->get() : 0.0f; }
        te::AutomatableParameter* getControl (int index)    { return juce::isPositiveAndBelow (index, (int) params.size()) ? params[(size_t) index].get() : nullptr; }
        int getNumControls() const noexcept                 { return (int) params.size(); }

        bool takesMidiInput() override                      { return false; }
        bool takesAudioInput() override                     { return true; }
        bool producesAudioWhenNoAudioInput() override       { return false; }
        int getNumOutputChannelsGivenInputs (int numInputs) override { return juce::jmin (numInputs, 2); }
        BusLayout getBusses() const override                { return BusLayout::singleStereoInOut(); }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override                        {}
        void applyToBuffer (const te::PluginRenderContext&) override;
        void restorePluginStateFromValueTree (const juce::ValueTree&) override;
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    protected:
        double sampleRate = 44100.0;

        /** Call first in every subclass's destructor: Tracktion wants deletion announced by the innermost class. */
        void teardown();

        virtual void prepare (double sampleRate, int maxBlockSize) = 0;
        virtual void process (float* const* channels, int numChannels, int numSamples) = 0;

    private:
        std::vector<Control> controls;
        juce::OwnedArray<juce::CachedValue<float>> values;
        std::vector<te::AutomatableParameter::Ptr> params;   // a strong ref: te::Plugin drops its own before our destructor ends
        std::atomic<bool> prepared { false };
        bool tornDown = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectPlugin)
    };

    //==============================================================================
    #define EZ_EFFECT_NAMES(className, typeName, displayName, shortName) \
        ~className() override                   { teardown(); } \
        static const char* getPluginName()      { return NEEDS_TRANS (displayName); } \
        static const char* xmlTypeName; \
        juce::String getName() const override              { return TRANS (displayName); } \
        juce::String getPluginType() override              { return xmlTypeName; } \
        juce::String getShortName (int) override           { return shortName; } \
        juce::String getSelectableDescription() override   { return TRANS (displayName); }

    class GatePlugin : public EffectPlugin
    {
    public:
        enum { threshold, floor, attack, hold, release };
        explicit GatePlugin (te::PluginCreationInfo);
        EZ_EFFECT_NAMES (GatePlugin, "ezgate", "Gate", "Gate")
        float getGateGain() const noexcept     { return gateGain.load(); }
    protected:
        void prepare (double, int) override     { gate.prepare (sampleRate); }
        void process (float* const*, int, int) override;
    private:
        dsp::Gate gate;
        std::atomic<float> gateGain { 1.0f };
    };

    class DeEsserPlugin : public EffectPlugin
    {
    public:
        enum { frequency, threshold, most };
        explicit DeEsserPlugin (te::PluginCreationInfo);
        EZ_EFFECT_NAMES (DeEsserPlugin, "ezdeesser", "De-esser", "De-ess")
    protected:
        void prepare (double, int) override     { deEsser.prepare (sampleRate); }
        void process (float* const*, int, int) override;
    private:
        dsp::DeEsser deEsser;
    };

    class LimiterPlugin : public EffectPlugin
    {
    public:
        enum { input, ceiling, release };
        explicit LimiterPlugin (te::PluginCreationInfo);
        EZ_EFFECT_NAMES (LimiterPlugin, "ezlimiter", "Limiter", "Limit")
        double getLatencySeconds() override     { return limiter.getLatencySamples() / juce::jmax (1.0, sampleRate); }
    protected:
        void prepare (double, int block) override  { limiter.prepare (sampleRate, block); }
        void process (float* const*, int, int) override;
    private:
        dsp::Limiter limiter;
    };

    class CutFilterPlugin : public EffectPlugin
    {
    public:
        enum { lowCut, highCut };
        explicit CutFilterPlugin (te::PluginCreationInfo);
        EZ_EFFECT_NAMES (CutFilterPlugin, "ezcutfilter", "Low/high cut", "Cut")
    protected:
        void prepare (double, int) override     { low.reset(); high.reset(); lastLow = lastHigh = -1; }
        void process (float* const*, int, int) override;
    private:
        dsp::Biquad low, high;
        float lastLow = -1, lastHigh = -1;
        bool lowWasOn = false, highWasOn = false;
    };

    class SaturatorPlugin : public EffectPlugin
    {
    public:
        enum { drive, curve, tone, mix, output };
        explicit SaturatorPlugin (te::PluginCreationInfo);
        EZ_EFFECT_NAMES (SaturatorPlugin, "ezsaturator", "Saturator", "Sat")
    protected:
        void prepare (double, int) override     { saturator.prepare (sampleRate); }
        void process (float* const*, int, int) override;
    private:
        dsp::Saturator saturator;
    };

    class TunePlugin : public EffectPlugin
    {
    public:
        enum { amount, speed, root, scale };
        explicit TunePlugin (te::PluginCreationInfo);
        EZ_EFFECT_NAMES (TunePlugin, "eztune", "Tune", "Tune")
        float getDetectedHz() const noexcept    { return detected.load(); }
        double getLatencySeconds() override     { return corrector.getLatencySamples() / juce::jmax (1.0, sampleRate); }
    protected:
        void prepare (double, int) override     { corrector.prepare (sampleRate); }
        void process (float* const*, int, int) override;
    private:
        dsp::PitchCorrector corrector;
        std::atomic<float> detected { 0.0f };
    };

    class TremoloPlugin : public EffectPlugin
    {
    public:
        enum { rate, depth, mode };
        explicit TremoloPlugin (te::PluginCreationInfo);
        EZ_EFFECT_NAMES (TremoloPlugin, "eztremolo", "Tremolo", "Trem")
    protected:
        void prepare (double, int) override     { tremolo.prepare (sampleRate); }
        void process (float* const*, int, int) override;
    private:
        dsp::Tremolo tremolo;
    };

    class UtilityPlugin : public EffectPlugin
    {
    public:
        enum { gain, width, mono, flipLeft, flipRight };
        explicit UtilityPlugin (te::PluginCreationInfo);
        EZ_EFFECT_NAMES (UtilityPlugin, "ezutility", "Utility", "Util")
    protected:
        void prepare (double, int) override     {}
        void process (float* const*, int, int) override;
    private:
        dsp::Utility utility;
    };

    class AmpPlugin : public EffectPlugin
    {
    public:
        enum { mode, gate, gain, tight, bass, mid, treble, presence, cab, volume, blend };
        explicit AmpPlugin (te::PluginCreationInfo);
        EZ_EFFECT_NAMES (AmpPlugin, "ezamp", "Amp", "Amp")
        float getGateGain() const noexcept     { return gateGain.load(); }

        static juce::StringArray modeNames()   { return { "Clean", "Crunch", "Punk", "Lead", "Metal", "Bass" }; }
        static juce::StringArray cabNames()    { return { "No cabinet (DI)", "4x12 closed", "2x12 open", "1x12 combo", "Bass 8x10" }; }
    protected:
        void prepare (double, int) override     { amp.prepare (sampleRate); }
        void process (float* const*, int, int) override;
    private:
        dsp::Amp amp;
        std::atomic<float> gateGain { 1.0f };
    };

    /** Passes the sound through and keeps the last moments of it, for the Tuner window to read. */
    class TunerPlugin : public EffectPlugin
    {
    public:
        enum { reference };
        explicit TunerPlugin (te::PluginCreationInfo);
        EZ_EFFECT_NAMES (TunerPlugin, "eztuner", "Tuner", "Tune")

        struct Reading
        {
            bool hasNote = false;
            float hz = 0, cents = 0;   // cents: -50..50 from the nearest note
            int midiNote = -1;
            float levelDb = -120;
            juce::String noteName;     // "E", "F#" ...
            int octave = 0;
        };

        /** What's playing now. Message thread (the work is done here, never on the audio thread). */
        Reading read();

        /** Silences the track while you tune (the Tuner window turns this on and back off). */
        void setMuted (bool shouldMute) noexcept   { muted = shouldMute; }
        bool isMuted() const noexcept               { return muted.load(); }

        /** For tests: the same analysis on any audio. */
        static Reading analyse (const float* samples, int numSamples, double rate, float referenceHz);

        static constexpr int ringSize = 4096;
        static constexpr double analysisRate = 11025.0;
    protected:
        void prepare (double, int) override;
        void process (float* const*, int, int) override;
    private:
        std::array<std::atomic<float>, ringSize> ring {};
        std::atomic<juce::uint32> written { 0 };   // wraps (ringSize divides 2^32)
        std::atomic<bool> primed { false };
        juce::uint32 lastEnd = 0, lastChange = 0;   // message thread: is the sound still coming?
        std::atomic<bool> muted { false };
        dsp::Biquad antiAlias;
        int decimation = 4, decimCount = 0;
        float decimSum = 0;
        double actualRate = analysisRate;
        std::vector<float> copy, scratch;   // message thread
    };

    #undef EZ_EFFECT_NAMES

    namespace Effects
    {
        /** Registers every effect above with the engine (once, at start-up). */
        void registerAll (te::Engine&);

        /** The menu group an effect type belongs to ("Dynamics", "EQ & Filter", ...); {} if unknown. */
        juce::String categoryOf (const juce::String& pluginType);

        /** Every category in menu order. */
        juce::StringArray categories();
    }
}
