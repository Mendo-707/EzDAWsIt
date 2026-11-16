/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#pragma once

#include "../Core/Common.h"

namespace ez
{
    /** Measures a file's loudness on its own thread (a four-minute song takes about a second):
        an export's result, a reference track. */
    class LoudnessScan : public juce::Thread
    {
    public:
        /** asStereo: a mono file measured as it will be heard, in both speakers (+3 LU), like the mix is. */
        LoudnessScan (juce::AudioFormatManager& f, const juce::File& file, bool asStereo = false)
            : juce::Thread ("EzDAWsIt loudness scan"), formats (f), fileToScan (file), stereo (asStereo)
        {
        }

        ~LoudnessScan() override     { stopThread (5000); }

        std::atomic<bool> finished { false };
        bool valid = false;                   // read only after finished
        te::LoudnessMeter::Readings readings;

        void run() override
        {
            if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (fileToScan)))
            {
                const auto channels = stereo ? 2 : juce::jlimit (1, 2, (int) reader->numChannels);   // JUCE copies mono into both
                constexpr int blockSize = 8192;
                juce::AudioBuffer<float> buffer (channels, blockSize);
                te::LoudnessMeter meter;
                meter.prepare (reader->sampleRate, channels, blockSize);

                for (juce::int64 pos = 0; pos < reader->lengthInSamples && ! threadShouldExit(); pos += blockSize)
                {
                    const auto num = (int) juce::jmin ((juce::int64) blockSize, reader->lengthInSamples - pos);
                    reader->read (&buffer, 0, num, pos, true, channels > 1);
                    meter.process (buffer.getArrayOfReadPointers(), channels, num);
                }

                meter.flush();
                readings = meter.getReadings();
                valid = ! threadShouldExit() && readings.integratedValid;
            }

            finished = true;
        }

    private:
        juce::AudioFormatManager& formats;
        const juce::File fileToScan;
        const bool stereo;
    };

}
