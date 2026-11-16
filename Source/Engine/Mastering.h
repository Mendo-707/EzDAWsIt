/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The master channel's own effects (the Master view):

    MasteringChainPlugin   one effect, four modules in a fixed order, each with an on switch:
        EQ          low shelf (100 Hz), low mids (300 Hz), presence (3 kHz), air shelf (12 kHz)
        Multiband   three-band glue compression (crossovers 150 Hz and 2.5 kHz), an amount per band
        Stereo      width, and "mono below 120 Hz" (bass in the middle, where club systems want it)
        Limiter     gain into a look-ahead limiter and its ceiling. The ceiling is a sample peak, kept
                    0.5 dB lower inside so the true peak (between samples) stays under it as well
      Presets set all of it; every control is saved, undoable and automatable (EffectPlugin).
      The whole chain can be switched off (A/B) with the plugin's own enabled switch.

    ReferencePlugin        plays a reference song in place of the mix while "B" is on (Reference A/B),
                           at the song position, looped, at a gain that matches the mix's loudness.
                           Off in exports (and when not playing it's silent while B is on). Not
                           undoable; the file is remembered with the song.

    The master plugin list is: chain, main meter, reference. The meter measures the mix after the chain
    (what you'll export), and keeps measuring it while the reference plays.
*/

#pragma once

#include "Effects.h"

namespace ez
{
    class MasteringChainPlugin : public EffectPlugin
    {
    public:
        enum Control
        {
            lowShelf, lowMids, presence, air,       // EQ, dB
            glueLow, glueMid, glueHigh,             // Multiband, 0..100 %
            width,                                  // Stereo, %
            monoBass,                               // Stereo switch
            gain, ceiling,                          // Limiter, dB
            eqOn, glueOn, stereoOn, limiterOn,      // module switches
            numControls
        };

        enum Module { eqModule, glueModule, stereoModule, limiterModule, numModules };
        static constexpr int firstModuleSwitch = eqOn;

        explicit MasteringChainPlugin (te::PluginCreationInfo);
        ~MasteringChainPlugin() override         { teardown(); }

        static const char* getPluginName()      { return NEEDS_TRANS ("Mastering chain"); }
        static const char* xmlTypeName;
        juce::String getName() const override               { return TRANS ("Mastering chain"); }
        juce::String getPluginType() override               { return xmlTypeName; }
        juce::String getShortName (int) override            { return "Master"; }
        juce::String getSelectableDescription() override    { return TRANS ("Mastering chain"); }

        static juce::String moduleName (int module);
        static juce::String moduleHint (int module);

        struct Preset
        {
            const char* name;
            const char* description;
            float values[firstModuleSwitch];   // every control before the switches, in enum order
            int modulesOff = 0;                // bit m set: module m starts off
        };

        static const std::vector<Preset>& getPresets();

        juce::String getPresetName() const                  { return presetName.get(); }
        void setPresetName (const juce::String& n)          { presetName = n; }

        /** How hard each part is working now, dB (<= 0): the multiband bands and the limiter. Any thread. */
        float getBandReductionDb (int band) const noexcept  { return juce::isPositiveAndBelow (band, 3) ? bandReduction[band].load() : 0.0f; }
        float getLimiterReductionDb() const noexcept        { return limiterReduction.load(); }

        /** The look-ahead is always there (even with the limiter off), so the delay never changes. */
        double getLatencySeconds() override     { return limiter.getLatencySamples() / juce::jmax (1.0, sampleRate); }

        static constexpr float lowCrossoverHz = 150.0f, highCrossoverHz = 2500.0f, monoBelowHz = 120.0f;
        static constexpr float truePeakMarginDb = 0.5f;

    protected:
        void prepare (double sampleRate, int maxBlockSize) override;
        void process (float* const*, int, int) override;

    private:
        juce::CachedValue<juce::String> presetName;
        dsp::Biquad eqLow, eqLowMids, eqPresence, eqAir, sideHp1, sideHp2;
        dsp::Crossover3 crossover;
        dsp::Compressor bandComp[3];
        dsp::Limiter limiter;
        juce::AudioBuffer<float> bands[3];
        std::atomic<float> bandReduction[3] { 0.0f, 0.0f, 0.0f }, limiterReduction { 0.0f };
        float lastEq[4] { -100, -100, -100, -100 };
        bool glueRan = false;

        bool on (int control) const noexcept     { return get (control) >= 0.5f; }
        void updateEq();
    };

    //==============================================================================
    class ReferencePlugin : public te::Plugin
    {
    public:
        explicit ReferencePlugin (te::PluginCreationInfo);
        ~ReferencePlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS ("Reference"); }
        static const char* xmlTypeName;
        juce::String getName() const override               { return TRANS ("Reference"); }
        juce::String getPluginType() override               { return xmlTypeName; }
        juce::String getShortName (int) override            { return "Ref"; }
        juce::String getSelectableDescription() override    { return TRANS ("Reference"); }
        bool takesMidiInput() override                      { return false; }
        bool takesAudioInput() override                     { return true; }
        bool producesAudioWhenNoAudioInput() override       { return true; }
        int getNumOutputChannelsGivenInputs (int) override  { return 2; }
        BusLayout getBusses() const override                { return BusLayout::singleStereoInOut(); }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override                        {}
        void applyToBuffer (const te::PluginRenderContext&) override;

        /** Message thread. An empty file clears it. False if the file can't be read. */
        bool setFile (const juce::File&);
        juce::File getFile() const;
        double getFileSampleRate() const noexcept           { return fileRate; }

        /** B: the reference plays instead of the mix. Any thread. */
        void setListening (bool b) noexcept                 { listening = b; }
        bool isListening() const noexcept                   { return listening; }
        void setGainDb (float db) noexcept                  { gain = juce::Decibels::decibelsToGain (db); }

    private:
        juce::TimeSliceThread readAhead { "EzDAWsIt reference" };
        std::unique_ptr<juce::BufferingAudioReader> reader;   // swapped under `swap`
        juce::SpinLock swap;
        juce::AudioBuffer<float> source;
        double fileRate = 0, playbackRate = 44100;
        juce::int64 fileLength = 0;
        std::atomic<bool> listening { false };
        std::atomic<float> gain { 1.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferencePlugin)
    };
}
