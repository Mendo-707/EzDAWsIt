/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The Vocal Chain: a whole vocal sound in one effect, run by six macros.

      Clean   cuts rumble and room noise between phrases      (high-pass, gate)
      Tune    pulls notes to the song's key                   (pitch correction; more = faster)
      Punch   evens out loud and quiet words                  (two compressors, colour)
      Bright  adds clarity and presence, keeps "s" smooth     (tone EQ, de-esser, air)
      Space   puts the voice in a room                        (a reverb inside the chain)
      Width   spreads it left and right                       (two short doubling delays)

    Inside (the "pro view"), in order: Clean-up, Tune, Tone EQ, Comp 1, De-esser, Comp 2, Colour,
    Air EQ, Space, Width; each can be switched off on its own. Presets set all of it at once.
    Every macro, switch and the key are ordinary controls: saved, undoable and automatable.
*/

#pragma once

#include "Effects.h"

namespace ez
{
    class VocalChainPlugin : public EffectPlugin
    {
    public:
        enum Control
        {
            clean, tune, punch, bright, space, width,                          // macros, 0..100 %
            cleanOn, tuneOn, toneOn, comp1On, deEssOn, comp2On, colourOn, airOn, spaceOn, widthOn,
            root, scale, phone,                                                // tune key; telephone tone
            numControls
        };

        static constexpr int firstMacro = clean, numMacros = 6, firstModule = cleanOn, numModules = 10;

        explicit VocalChainPlugin (te::PluginCreationInfo);
        ~VocalChainPlugin() override             { teardown(); }

        static const char* getPluginName()      { return NEEDS_TRANS ("Vocal Chain"); }
        static const char* xmlTypeName;
        juce::String getName() const override               { return TRANS ("Vocal Chain"); }
        juce::String getPluginType() override               { return xmlTypeName; }
        juce::String getShortName (int) override            { return "Vox"; }
        juce::String getSelectableDescription() override    { return TRANS ("Vocal Chain"); }

        /** What each macro and module is called, and what it does (for the card and tooltips). */
        static juce::String macroName (int macro);
        static juce::String macroHint (int macro);
        static juce::String moduleName (int module);
        static juce::String moduleHint (int module);

        /** The last preset applied (saved with the song); shown on the card. */
        juce::String getPresetName() const                  { return presetName.get(); }
        void setPresetName (const juce::String& n)          { presetName = n; }

        /** How hard the compressors are working now (dB, <= 0), for the card. Any thread. */
        float getPunchReductionDb() const noexcept          { return punchReduction.load(); }

        struct Preset
        {
            const char* name;
            const char* description;
            float macros[numMacros];   // 0..100
            bool phone = false;
            int modulesOff = 0;        // bit m set: module m starts off (never an initializer_list: it would dangle)
        };

        static const std::vector<Preset>& getPresets();

        /** Tune always runs (passing the sound when off), so the delay it adds never changes. */
        double getLatencySeconds() override     { return corrector.getLatencySamples() / juce::jmax (1.0, sampleRate); }

    protected:
        void prepare (double sampleRate, int maxBlockSize) override;
        void process (float* const*, int, int) override;

    private:
        juce::CachedValue<juce::String> presetName;

        dsp::Biquad highPass, phoneLowPass, lowMid, presence, air;
        dsp::Gate gate;
        dsp::PitchCorrector corrector;
        dsp::Compressor comp1, comp2;
        dsp::DeEsser deEsser;
        dsp::Saturator colour;
        juce::Reverb reverb;
        juce::AudioBuffer<float> wetBuffer, doubleLine;
        int doubleWrite = 0;
        float doublePhase = 0;
        std::atomic<float> punchReduction { 0.0f };
        float lastClean = -1, lastBright = -1, lastPhone = -1;
        bool spaceRan = false, widthRan = false, gateRan = false;

        bool on (int module) const noexcept      { return get (module) >= 0.5f; }
        float macro (int m) const noexcept       { return juce::jlimit (0.0f, 1.0f, get (m) * 0.01f); }
        void updateFilters();
    };
}
