/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Level meters.

    MasterTapPlugin  sits last in the master plugin chain (added to every song,
                     never shown as an effect): sample peak per channel and EBU
                     R128 loudness (momentary, short-term, integrated, true peak)
                     with Tracktion's LoudnessMeter. Tracktion applies the master
                     volume after the master plugins, so MeterBank adds the master
                     volume to the readings (master pan and fades are not included).
                     Real-time safe: atomics and the meter's lock-free readings.

    MeterBank        reads every meter 30 times a second in one place and keeps
                     the values views draw: a falling level, a held peak, a clip
                     light that stays on until reset, and the highest level since
                     reset. Several views (track headers, mixer) read the same
                     values, so none of them steals another's peaks.

    Tracks are measured by their LevelMeterPlugin (after volume and pan). The
    bank holds those plugins by reference only between song open and song close.
*/

#pragma once

#include "../Core/Common.h"

namespace ez
{
    class Session;

    //==============================================================================
    class MasterTapPlugin : public te::Plugin
    {
    public:
        explicit MasterTapPlugin (te::PluginCreationInfo);
        ~MasterTapPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS ("Main meter"); }
        static const char* xmlTypeName;

        /** Highest sample (linear, per channel) since the last call. Message thread. */
        float takePeak (int channel) noexcept;

        te::LoudnessMeter::Readings getLoudness() const noexcept    { return loudness.getReadings(); }

        /** The latest samples (left and right summed), for the spectrum. Message thread; returns how many were read.
            The audio thread drops samples when nobody reads them: it never waits. */
        int readSamples (float* dest, int maxSamples) noexcept;
        double getSampleRate() const noexcept                       { return sampleRate.load(); }
        void resetLoudness() noexcept                               { loudness.requestReset(); }

        juce::String getName() const override                       { return TRANS ("Main meter"); }
        juce::String getPluginType() override                       { return xmlTypeName; }
        juce::String getShortName (int) override                    { return "Meter"; }
        juce::String getSelectableDescription() override            { return TRANS ("Main meter"); }
        bool takesMidiInput() override                              { return false; }
        bool takesAudioInput() override                             { return true; }
        bool producesAudioWhenNoAudioInput() override               { return false; }
        int getNumOutputChannelsGivenInputs (int numInputs) override { return numInputs; }   // passes the audio through untouched
        BusLayout getBusses() const override                        { return BusLayout::singleStereoInOut(); }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override                                {}
        void applyToBuffer (const te::PluginRenderContext&) override;

    private:
        te::LoudnessMeter loudness;
        std::atomic<float> peaks[2] { 0.0f, 0.0f };
        std::atomic<bool> prepared { false };
        std::atomic<double> sampleRate { 48000.0 };

        static constexpr int fifoSize = 16384;
        juce::AbstractFifo fifo { fifoSize };   // single writer (audio), single reader (message thread): lock-free
        std::array<float, fifoSize> fifoData {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterTapPlugin)
    };

    //==============================================================================
    class MeterBank : private juce::Timer
    {
    public:
        explicit MeterBank (Session&);
        ~MeterBank() override;

        static constexpr float floorDb = -60.0f;
        static constexpr int pollHz = 30;

        enum class Hold { off, short_, long_, untilReset };

        struct Reading
        {
            float levelDb[2] { floorDb, floorDb };   // falls 24 dB a second after a peak
            float holdDb[2] { floorDb, floorDb };    // held peak (by the hold setting)
            float maxDb = floorDb;                   // highest since reset
            bool clipped = false;                    // at or over 0 dB since reset
            int channels = 0;                        // 0: nothing measured yet
        };

        struct MainReading : Reading
        {
            te::LoudnessMeter::Readings loudness;
        };

        /** Latest values for an audio track (0-based). */
        Reading getTrack (int trackIndex) const;
        MainReading getMain() const;

        /** The main output's spectrum: dB per band, spectrumBands bands spaced evenly in log frequency
            from 20 Hz to 20 kHz, falling back slowly (about 20 dB a second). Empty until audio has played. */
        static constexpr int spectrumBands = 160;
        const std::vector<float>& getSpectrum() const noexcept  { return spectrum; }

        /** Runs the spectrum on these samples now (the timer feeds it from the main output; tests call it). */
        void analyseSamples (const float* samples, int numSamples, double sampleRate, float elapsedSeconds);

        void resetTrack (int trackIndex);
        void resetMain();
        void resetAll();

        /** Peak or RMS for every track meter (the main meter is always peak; loudness is its average). */
        void setMode (te::LevelMeasurer::Mode);
        te::LevelMeasurer::Mode getMode() const noexcept    { return mode; }

        Hold getHold() const noexcept       { return hold; }
        void setHold (Hold);

        /** Reads every meter now (the timer does this; tests call it directly). */
        void poll();

        /** Drops every reference into the song. Session calls this before the song is destroyed. */
        void releaseSong();

    private:
        struct TrackMeter
        {
            te::EditItemID trackID;
            te::Plugin::Ptr plugin;   // keeps the measurer alive while the client is registered
            std::unique_ptr<te::LevelMeasurer::Client> client;
            Reading reading;
            juce::uint32 holdUntil[2] { 0, 0 };
        };

        Session& session;
        std::map<te::EditItemID, std::unique_ptr<TrackMeter>> tracks;   // by track: survives moves and deletes of others
        std::vector<te::EditItemID> order;                               // track index -> ID, as of the last poll
        MainReading main;
        juce::uint32 mainHoldUntil[2] { 0, 0 };
        juce::uint32 lastPollMs = 0;
        Hold hold = Hold::short_;
        te::LevelMeasurer::Mode mode = te::LevelMeasurer::peakMode;

        static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;   // 4096 points: about 12 Hz per bin at 48 kHz
        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
        std::vector<float> history = std::vector<float> ((size_t) fftSize, 0.0f), fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f),
                           readBuffer = std::vector<float> (8192, 0.0f), spectrum;

        void update (Reading&, juce::uint32* holdUntil, int channel, float peakDb, float elapsedSeconds, juce::uint32 now);
        void unbind (TrackMeter&);
        void timerCallback() override       { poll(); }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterBank)
    };
}
