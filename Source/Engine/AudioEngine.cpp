/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "AudioEngine.h"
#include "../Core/AppPaths.h"
#include "DrumMachinePlugin.h"
#include "Effects.h"
#include "Meters.h"

namespace ez
{
    //==============================================================================
    /** A second audio callback (JUCE sums it with the engine's) that plays a 1 second,
        -20 dB beep with soft edges on two chosen output channels. Lock-free. */
    class AudioEngine::TestTone : public juce::AudioIODeviceCallback
    {
    public:
        /** Channels are device channel numbers: the same indexing Tracktion uses for the song,
            so the beep comes out exactly where the song would. Thread-safe. */
        void play (int leftChannel, int rightChannel)
        {
            left.store (leftChannel);
            right.store (rightChannel);
            startRequested.store (true);
        }

        void audioDeviceIOCallbackWithContext (const float* const*, int,
                                               float* const* outputs, int numOutputs, int numSamples,
                                               const juce::AudioIODeviceCallbackContext&) override
        {
            // JUCE sums this into the real outputs (or, if first, Tracktion is added on top):
            // either way this buffer must be fully written
            for (int ch = 0; ch < numOutputs; ++ch)
                if (outputs[ch] != nullptr)
                    juce::FloatVectorOperations::clear (outputs[ch], numSamples);

            const auto total = juce::roundToInt (sampleRate);   // 1 second

            if (startRequested.exchange (false))
                samplesRemaining = total;

            if (samplesRemaining <= 0)
                return;

            const auto l = left.load(), r = right.load();
            const auto fadeSamples = juce::jmax (1, total / 20);   // 50 ms fades: no clicks

            for (int i = 0; i < numSamples && samplesRemaining > 0; ++i, --samplesRemaining)
            {
                const auto position = total - samplesRemaining;
                const auto fade = (float) juce::jmin (1.0, juce::jmin (position, samplesRemaining) / (double) fadeSamples);
                const auto sample = 0.1f * fade * (float) std::sin (phase);   // -20 dB
                phase += juce::MathConstants<double>::twoPi * 880.0 / sampleRate;

                for (auto ch : { l, r })
                    if (ch >= 0 && ch < numOutputs && outputs[ch] != nullptr)
                        outputs[ch][i] = sample;
            }

            phase = std::fmod (phase, juce::MathConstants<double>::twoPi);
        }

        void audioDeviceAboutToStart (juce::AudioIODevice* device) override
        {
            sampleRate = device->getCurrentSampleRate() > 0 ? device->getCurrentSampleRate() : 44100.0;
            samplesRemaining = 0;
        }

        void audioDeviceStopped() override      { samplesRemaining = 0; }

    private:
        std::atomic<int> left { -1 }, right { -1 };
        std::atomic<bool> startRequested { false };

        // Audio thread only (audioDeviceAboutToStart/Stopped run while the callback is stopped)
        int samplesRemaining = 0;
        double sampleRate = 44100.0, phase = 0.0;
    };

    //==============================================================================
    /** Plays one file on two device channels (the main output's), mixed in with the song. */
    class AudioEngine::Preview : public juce::AudioIODeviceCallback
    {
    public:
        Preview()   { readAhead.startThread(); }

        ~Preview() override
        {
            transport.setSource (nullptr);
            readAhead.stopThread (2000);
        }

        bool start (juce::AudioFormatManager& formats, const juce::File& file, int leftChannel, int rightChannel, float gain, bool loop)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

            if (reader == nullptr || reader->lengthInSamples <= 0)
                return false;

            const auto rate = reader->sampleRate;
            auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
            newSource->setLooping (loop);   // before it's playing: the audio thread reads it
            transport.setSource (nullptr);   // the old one stops before it's deleted
            source = std::move (newSource);
            left.store (leftChannel);
            right.store (rightChannel);
            transport.setSource (source.get(), 32768, &readAhead, rate, 2);   // resampled to the device rate
            transport.setPosition (0.0);
            transport.setGain (gain);
            transport.start();
            playing = file;
            return true;
        }

        void stop()
        {
            // transport.stop() waits for the audio callback to confirm: with no device running that's a
            // one-second freeze, so only ask a running device (setSource (nullptr) stops it either way)
            if (deviceRunning)
                transport.stop();

            transport.setSource (nullptr);
            source.reset();
            playing = juce::File();
        }

        bool isPlaying() const              { return deviceRunning && source != nullptr && transport.isPlaying(); }
        void setGain (float g)              { transport.setGain (g); }   // AudioTransportSource smooths it

        double getProgress() const
        {
            if (! isPlaying())
                return -1.0;

            const auto length = transport.getLengthInSeconds();
            return length > 0.0 ? std::fmod (transport.getCurrentPosition(), length) / length : -1.0;
        }
        juce::File getFile() const          { return isPlaying() ? playing : juce::File(); }

        void audioDeviceIOCallbackWithContext (const float* const*, int,
                                               float* const* outputs, int numOutputs, int numSamples,
                                               const juce::AudioIODeviceCallbackContext&) override
        {
            for (int ch = 0; ch < numOutputs; ++ch)
                if (outputs[ch] != nullptr)
                    juce::FloatVectorOperations::clear (outputs[ch], numSamples);

            const auto l = left.load(), r = right.load();

            // In pieces the size of our buffer: some drivers send bigger blocks than they announce
            for (int done = 0; done < numSamples;)
            {
                const auto n = juce::jmin (numSamples - done, buffer.getNumSamples());
                juce::AudioSourceChannelInfo info (&buffer, 0, n);
                transport.getNextAudioBlock (info);   // silence when stopped

                if (r == l)   // a mono output: both sides in the one channel
                    buffer.addFrom (0, 0, buffer, 1, 0, n);

                if (juce::isPositiveAndBelow (l, numOutputs) && outputs[l] != nullptr)
                    juce::FloatVectorOperations::copy (outputs[l] + done, buffer.getReadPointer (0), n);

                if (r != l && juce::isPositiveAndBelow (r, numOutputs) && outputs[r] != nullptr)
                    juce::FloatVectorOperations::copy (outputs[r] + done, buffer.getReadPointer (1), n);

                done += n;
            }
        }

        void audioDeviceAboutToStart (juce::AudioIODevice* device) override
        {
            const auto blockSize = juce::jmax (512, device->getCurrentBufferSizeSamples() * 2);
            buffer.setSize (2, blockSize);
            transport.prepareToPlay (blockSize, device->getCurrentSampleRate() > 0 ? device->getCurrentSampleRate() : 44100.0);
            deviceRunning = true;
        }

        void audioDeviceStopped() override
        {
            deviceRunning = false;
            transport.releaseResources();
        }

    private:
        juce::TimeSliceThread readAhead { "EzDAWsIt preview" };
        juce::AudioTransportSource transport;
        std::unique_ptr<juce::AudioFormatReaderSource> source;
        juce::AudioBuffer<float> buffer { 2, 4096 };
        std::atomic<int> left { -1 }, right { -1 };
        std::atomic<bool> deviceRunning { false };
        juce::File playing;
    };

    //==============================================================================
    AudioEngine::AudioEngine (const juce::String& appName,
                              std::unique_ptr<te::UIBehaviour> uiBehaviour,
                              std::unique_ptr<te::EngineBehaviour> engineBehaviour)
        : engine (appName, std::move (uiBehaviour), engineBehaviour != nullptr ? std::move (engineBehaviour) : std::make_unique<Behaviour>()),
          testTone (std::make_unique<TestTone>()),
          preview (std::make_unique<Preview>())
    {
        engine.getPluginManager().createBuiltInType<DrumMachinePlugin>();   // before any song loads one
        engine.getPluginManager().createBuiltInType<MasterTapPlugin>();
        Effects::registerAll (engine);   // our own effects (Effects.h)
        registerBundledPlugins();         // the EzDAW plug-ins in the Plugins folder
        getDeviceManager().addAudioCallback (testTone.get());
        getDeviceManager().addAudioCallback (preview.get());

        recordOffsetMs = juce::jlimit (-500.0, 500.0, engine.getPropertyStorage().getPropertiesFile().getDoubleValue ("ezRecordOffsetMs", 0.0));
        deviceWatcher.onChange = [this]
        {
            for (auto* input : getAudioInputs())
            {
                input->setMonitorMode (te::InputDevice::MonitorMode::automatic);
                input->setRecordAdjustmentMs (recordOffsetMs);
            }
        };
        engine.getDeviceManager().addChangeListener (&deviceWatcher);
        deviceWatcher.onChange();

        chooseLowLatencyDriverOnFirstRun();
        log ("Audio engine started: " + describeCurrentDevice() + " | main output: " + getMainOutputName());
    }

    void AudioEngine::chooseLowLatencyDriverOnFirstRun()
    {
        auto& props = engine.getPropertyStorage().getPropertiesFile();

        // Once only (a driver picked in Setup afterwards is never overridden), and never for a headless engine
        if (props.containsKey ("ezDriverChosen") || ! engine.getEngineBehaviour().autoInitialiseDeviceManager())
            return;

        props.setValue ("ezDriverChosen", true);
        auto& dm = getDeviceManager();
        juce::StringArray withDevices;

        for (auto* type : dm.getAvailableDeviceTypes())
        {
            type->scanForDevices();

            if (! type->getDeviceNames().isEmpty())
                withDevices.add (type->getTypeName());
        }

        const auto current = dm.getCurrentAudioDevice() != nullptr ? dm.getCurrentAudioDevice()->getTypeName() : juce::String();
        const auto pick = pickLowLatencyDriver (withDevices, current);

        if (pick.isEmpty())
            return;

        dm.setCurrentAudioDeviceType (pick, true);

        if (auto* device = dm.getCurrentAudioDevice())
        {
            auto setup = dm.getAudioDeviceSetup();
            setup.bufferSize = pickBufferSize (device->getAvailableBufferSizes());
            dm.setAudioDeviceSetup (setup, true);
        }

        log ("First run: picked " + pick + " for low-latency playing: " + describeCurrentDevice());
    }

    int AudioEngine::registerBundledPlugins()
    {
        const auto folder = AppPaths::getBundledPluginsFolder();

        if (! folder.isDirectory())
            return 0;

        auto& pm = engine.getPluginManager();
        juce::AudioPluginFormat* vst3 = nullptr;

        for (int i = 0; i < pm.pluginFormatManager.getNumFormats(); ++i)
            if (pm.pluginFormatManager.getFormat (i)->getName() == "VST3")
                vst3 = pm.pluginFormatManager.getFormat (i);

        if (vst3 == nullptr)
            return 0;

        int scanned = 0;

        for (const auto& bundle : folder.findChildFiles (juce::File::findFilesAndDirectories, false, "*.vst3"))
        {
            const auto path = bundle.getFullPathName();

            if (pm.knownPluginList.isListingUpToDate (path, *vst3))
                continue;   // known and unchanged: nothing to do at start-up

            juce::OwnedArray<juce::PluginDescription> found;
            pm.knownPluginList.scanAndAddFile (path, true, found, *vst3);
            ++scanned;
            log ("Bundled plug-in " + bundle.getFileName() + (found.isEmpty() ? juce::String (": couldn't be loaded")
                                                                             : ": " + found.getFirst()->name));
        }

        return scanned;
    }

    juce::String AudioEngine::pickLowLatencyDriver (const juce::StringArray& typesWithDevices, const juce::String& current)
    {
        return current != "ASIO" && typesWithDevices.contains ("ASIO") ? juce::String ("ASIO") : juce::String();
    }

    int AudioEngine::pickBufferSize (const juce::Array<int>& available, int wanted)
    {
        int best = 0;

        for (auto size : available)
            if (best == 0 || std::abs (size - wanted) < std::abs (best - wanted)
                 || (std::abs (size - wanted) == std::abs (best - wanted) && size < best))   // a tie: the smaller, less delay
                best = size;

        return best;
    }

    AudioEngine::~AudioEngine()
    {
        engine.getDeviceManager().removeChangeListener (&deviceWatcher);
        getDeviceManager().removeAudioCallback (preview.get());
        getDeviceManager().removeAudioCallback (testTone.get());
        log ("Audio engine shutting down");
    }

    //==============================================================================
    juce::Array<AudioEngine::Output> AudioEngine::getOutputs()
    {
        juce::Array<Output> outputs;

        for (auto* device : engine.getDeviceManager().getWaveOutputDevices())
            if (device != nullptr && device->isEnabled())
                outputs.add ({ device->getDeviceID(), device->getAliasOrName() });

        return outputs;
    }

    juce::String AudioEngine::getMainOutputID()
    {
        auto* device = engine.getDeviceManager().getDefaultWaveOutDevice();
        return device != nullptr ? device->getDeviceID() : juce::String();
    }

    juce::String AudioEngine::getMainOutputName()
    {
        auto* device = engine.getDeviceManager().getDefaultWaveOutDevice();
        return device != nullptr ? device->getAliasOrName() : juce::String ("none");
    }

    bool AudioEngine::setMainOutput (const juce::String& outputID)
    {
        for (auto& output : getOutputs())
        {
            if (output.id == outputID)
            {
                engine.getDeviceManager().setDefaultWaveOutDevice (outputID);
                log ("Main output set to " + output.name);
                return getMainOutputID() == outputID;
            }
        }

        log ("Error: unknown output " + outputID);
        return false;
    }

    bool AudioEngine::startPreview (const juce::File& file)
    {
        // The main output's two device channels: where the song plays
        int l = 0, r = 1;

        if (auto* device = engine.getDeviceManager().getDefaultWaveOutDevice())
        {
            const auto& channels = device->getChannels();

            if (channels.getNumChannels() > 0)
            {
                l = channels[0].indexInDevice;
                r = channels.getNumChannels() > 1 ? channels[1].indexInDevice : l;
            }
        }

        return preview->start (engine.getAudioFileFormatManager().readFormatManager, file, l, r,
                               juce::Decibels::decibelsToGain (getPreviewLevel(), -60.0f), isPreviewLooping());
    }

    namespace
    {
        const char* previewLevelKey = "ezPreviewLevel";
        const char* previewLoopKey = "ezPreviewLoop";
    }

    void AudioEngine::setPreviewLevel (float dB)
    {
        dB = juce::jlimit (-60.0f, 0.0f, dB);
        engine.getPropertyStorage().getPropertiesFile().setValue (previewLevelKey, dB);
        preview->setGain (juce::Decibels::decibelsToGain (dB, -60.0f));
    }

    float AudioEngine::getPreviewLevel() const
    {
        return juce::jlimit (-60.0f, 0.0f, (float) engine.getPropertyStorage().getPropertiesFile().getDoubleValue (previewLevelKey, -6.0));
    }

    void AudioEngine::setPreviewLooping (bool shouldLoop)
    {
        engine.getPropertyStorage().getPropertiesFile().setValue (previewLoopKey, shouldLoop);
    }

    bool AudioEngine::isPreviewLooping() const
    {
        return engine.getPropertyStorage().getPropertiesFile().getBoolValue (previewLoopKey, false);
    }

    double AudioEngine::getPreviewProgress() const     { return preview->getProgress(); }

    void AudioEngine::stopPreview()                    { preview->stop(); }
    bool AudioEngine::isPreviewing() const             { return preview->isPlaying(); }
    juce::File AudioEngine::getPreviewFile() const     { return preview->getFile(); }

    bool AudioEngine::playTestTone (const juce::String& outputID)
    {
        if (getDeviceManager().getCurrentAudioDevice() == nullptr)
            return false;

        for (auto* device : engine.getDeviceManager().getWaveOutputDevices())
        {
            if (device != nullptr && device->getDeviceID() == outputID)
            {
                const auto& channels = device->getChannels();

                if (channels.getNumChannels() == 0)
                    return false;

                const auto l = channels[0].indexInDevice;
                const auto r = channels.getNumChannels() > 1 ? channels[1].indexInDevice : l;
                testTone->play (l, r);
                return true;
            }
        }

        return false;
    }


    juce::AudioDeviceManager& AudioEngine::getDeviceManager()
    {
        return engine.getDeviceManager().deviceManager;
    }

    void AudioEngine::enableAllInputs()
    {
        auto& dm = engine.getDeviceManager();

        for (auto& midiInput : dm.getMidiInDevices())
        {
            midiInput->setMonitorMode (te::InputDevice::MonitorMode::automatic);   // hear it when the track is armed
            midiInput->setEnabled (true);
        }

        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        {
            if (auto* input = dm.getWaveInDevice (i))
            {
                input->setMonitorMode (te::InputDevice::MonitorMode::automatic);
                input->setEnabled (true);
            }
        }
    }

    juce::Array<te::WaveInputDevice*> AudioEngine::getAudioInputs()
    {
        juce::Array<te::WaveInputDevice*> inputs;
        auto& dm = engine.getDeviceManager();

        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            if (auto* input = dm.getWaveInDevice (i); input != nullptr && ! input->isTrackDevice())
                inputs.add (input);

        return inputs;
    }

    bool AudioEngine::areInputsMono()
    {
        const auto inputs = getAudioInputs();

        if (inputs.isEmpty())
            return false;

        for (auto* input : inputs)
            if (input->getChannels().getNumChannels() > 1)
                return false;

        return true;
    }

    void AudioEngine::setInputsMono (bool mono)
    {
        // The device list is rebuilt later (asynchronously): the new inputs are set up in deviceListChanged
        engine.getDeviceManager().setAllWaveInputsToNumChannels (mono ? 1u : 2u);
        log (juce::String ("Audio inputs: ") + (mono ? "one per channel" : "stereo pairs"));
    }

    double AudioEngine::getRecordOffsetMs()
    {
        return recordOffsetMs;
    }

    void AudioEngine::setRecordOffsetMs (double ms)
    {
        // Tracktion's adjustment drops that much from the start of a recording when positive: it lands earlier
        recordOffsetMs = juce::jlimit (-500.0, 500.0, ms);
        engine.getPropertyStorage().getPropertiesFile().setValue ("ezRecordOffsetMs", recordOffsetMs);

        for (auto* input : getAudioInputs())
            input->setRecordAdjustmentMs (recordOffsetMs);
    }

    juce::String AudioEngine::describeCurrentDevice()
    {
        auto* device = getDeviceManager().getCurrentAudioDevice();

        if (device == nullptr)
            return "No audio device";

        const auto rate = device->getCurrentSampleRate();
        const auto toMs = [rate] (int samples) { return rate > 0 ? juce::String (1000.0 * samples / rate, 1) : juce::String ("?"); };

        return device->getName() + " (" + device->getTypeName() + ") | "
             + juce::String ((int) rate) + " Hz | "
             + juce::String (device->getCurrentBufferSizeSamples()) + " samples | "
             + toMs (device->getInputLatencyInSamples()) + " ms in / "
             + toMs (device->getOutputLatencyInSamples()) + " ms out";
    }
}
