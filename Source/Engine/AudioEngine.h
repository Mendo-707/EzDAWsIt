/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Owns the Tracktion Engine (audio devices, plugins, settings).
    One per app. Knows nothing about the UI or about songs.
*/

#pragma once

#include "../Core/Common.h"

namespace ez
{
    class AudioEngine
    {
    public:
        /** Our engine settings. Tracktion allows 16 plugins a track by default, counting the fader,
            the meter, the instrument and every send: a vocal track with a few sends ran out at 12
            effects, and the extra ones were silently dropped. Tests derive from this too. */
        struct Behaviour : public te::EngineBehaviour
        {
            static constexpr int maxPluginsOnTrack = 40;

            /** Plugins are scanned in a separate copy of EzDAWsIt: a plugin that crashes while being
                scanned takes down only that copy, and is put on the failed list instead. */
            bool canScanPluginsOutOfProcess() override         { return true; }

            te::EditLimits getEditLimits() override
            {
                te::EditLimits limits;
                limits.maxPluginsOnTrack = maxPluginsOnTrack;
                limits.maxNumMasterPlugins = 12;   // ours: mastering chain, meter, reference (Tracktion's default is 4)
                return limits;
            }
        };

        /** appName also names the settings folder, so tests use their own name and never
            touch the user's audio settings. A behaviour whose autoInitialiseDeviceManager()
            returns false gives a headless engine that opens no audio device. */
        AudioEngine (const juce::String& appName,
                     std::unique_ptr<te::UIBehaviour>,
                     std::unique_ptr<te::EngineBehaviour> = nullptr);
        ~AudioEngine();

        te::Engine& getEngine() noexcept                { return engine; }

        /** The first-run driver choice: ASIO when it has a device and we're on something else (else empty).
            Windows Audio adds a lot of delay to live playing; ASIO doesn't. */
        static juce::String pickLowLatencyDriver (const juce::StringArray& typesWithDevices, const juce::String& current);
        /** The buffer to use from what a device offers: the one nearest 128 samples (a few ms, safe on most machines). */
        static int pickBufferSize (const juce::Array<int>& available, int wanted = 128);

        /** Registers the plug-ins that ship with EzDAWsIt (every .vst3 in AppPaths::getBundledPluginsFolder()).
            Each is scanned only when new or changed since the last run, through Tracktion's scanner (out of
            process where available, so a broken one can't take the app down). Returns how many it scanned. */
        int registerBundledPlugins();
        juce::AudioDeviceManager& getDeviceManager();

        /** Turns on every audio and MIDI input so any of them can be picked on a track. */
        void enableAllInputs();

        //==============================================================================
        // Recording setup (Setup > Recording)

        /** The interface's audio inputs, as tracks see them. */
        juce::Array<te::WaveInputDevice*> getAudioInputs();

        /** Mono: one input per channel (a microphone or guitar each: recording a band). Off: stereo pairs
            (keyboards, a mixer's stereo out). Tracks on inputs that change are left with no input. */
        bool areInputsMono();
        void setInputsMono (bool);

        /** Moves recordings earlier (positive) to make up for the interface's delay, in ms. All inputs. */
        double getRecordOffsetMs();
        void setRecordOffsetMs (double);

        /** e.g. "MC-707 (ASIO) | 48000 Hz | 128 samples | 5.3 ms in / 5.3 ms out" */
        juce::String describeCurrentDevice();

        //==============================================================================
        // Outputs (each is a stereo pair or mono channel of the current audio device)
        struct Output
        {
            juce::String id, name;
        };

        /** Every enabled output of the current audio device. */
        juce::Array<Output> getOutputs();

        /** The output the song (master channel) plays through. */
        juce::String getMainOutputID();
        juce::String getMainOutputName();
        bool setMainOutput (const juce::String& outputID);

        /** Plays a short, quiet beep on one output only, so you can find out which
            output is your headphones / speakers. Returns false for an unknown output. */
        bool playTestTone (const juce::String& outputID);

        //==============================================================================
        // Preview: hear a sound file from the browser on the main output, outside the song (at its own
        // speed whatever the device rate; any number of previews, one at a time). Message thread.

        /** Starts playing the file from the top. False if it can't be read. */
        bool startPreview (const juce::File&);
        void stopPreview();
        bool isPreviewing() const;
        juce::File getPreviewFile() const;

    private:
        void chooseLowLatencyDriverOnFirstRun();
        /** The device list changed (inputs split differently, an interface plugged in): set the new inputs up. */
        struct DeviceListWatcher : public juce::ChangeListener
        {
            std::function<void()> onChange;
            void changeListenerCallback (juce::ChangeBroadcaster*) override   { if (onChange) onChange(); }
        };

        DeviceListWatcher deviceWatcher;
        double recordOffsetMs = 0.0;
        class TestTone;
        class Preview;

        te::Engine engine;
        std::unique_ptr<TestTone> testTone;
        std::unique_ptr<Preview> preview;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
    };
}
