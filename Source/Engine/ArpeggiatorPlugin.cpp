/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "ArpeggiatorPlugin.h"

namespace ez
{
    const char* ArpeggiatorPlugin::xmlTypeName = "ezarpeggiator";

    ArpeggiatorPlugin::ArpeggiatorPlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, {
              { "rate",     "Rate",     0.0f, 5.0f, 2.0f, 0, "", 0, { "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T" } },
              { "mode",     "Mode",     0.0f, 4.0f, 0.0f, 0, "", 0, { "Up", "Down", "Up & down", "As played", "Random" } },
              { "octaves",  "Octaves",  1.0f, 4.0f, 1.0f, 0, "", 0 },
              { "gate",     "Gate",     5.0f, 100.0f, 60.0f, 0, "%", 0 },
              { "latch",    "Latch",    0.0f, 1.0f, 0.0f, 0, "", 0, { "Off", "On" } },
              { "velocity", "Velocity", 0.0f, 127.0f, 0.0f, 0, "", 0, {}, "As played" } })
    {
    }

    double ArpeggiatorPlugin::stepBeatsFor (int rateChoice) noexcept
    {
        static constexpr double steps[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
        return steps[juce::jlimit (0, (int) std::size (steps) - 1, rateChoice)];
    }

    void ArpeggiatorPlugin::applyToBuffer (const te::PluginRenderContext& rc)
    {
        if (rc.bufferForMidiMessages == nullptr || rc.bufferNumSamples <= 0)
            return;

        SCOPED_REALTIME_CHECK

        auto& midi = *rc.bufferForMidiMessages;
        const auto numSamples = rc.bufferNumSamples;
        const auto toSample = [&] (double seconds)
        {
            return juce::jlimit (0, numSamples - 1, (int) std::floor ((seconds - rc.midiBufferOffset) * sampleRate));
        };
        const auto emit = [&] (const Arpeggiator::Event& e)
        {
            const auto time = rc.midiBufferOffset + e.sampleOffset / sampleRate;
            midi.addMidiMessage (e.velocity > 0 ? juce::MidiMessage::noteOn (channel, e.note, (juce::uint8) e.velocity)
                                                : juce::MidiMessage::noteOff (channel, e.note),
                                 time, {});
        };

        // The keys go to the arpeggiator; everything else stays in the stream
        if (midi.isAllNotesOff)
        {
            if (const auto note = arp.getSoundingNote(); note >= 0)
                emit ({ 0, note, 0 });

            arp.reset();
        }

        for (auto& m : midi)
        {
            if (m.isNoteOn())
            {
                channel = m.getChannel();
                arp.queueInput (toSample (m.getTimeStamp()), m.getNoteNumber(), m.getVelocity());
            }
            else if (m.isNoteOff())
            {
                arp.queueInput (toSample (m.getTimeStamp()), m.getNoteNumber(), 0);
            }
        }

        midi.removeIf ([] (const te::MidiMessageWithSource& m) { return m.isNoteOnOrOff(); });

        Arpeggiator::Settings s;
        s.stepBeats = stepBeatsFor (juce::roundToInt (get (rate)));
        s.mode = juce::roundToInt (get (mode));
        s.octaves = juce::roundToInt (get (octaves));
        s.gate = get (gate) / 100.0f;
        s.latch = get (latch) >= 0.5f;
        s.fixedVelocity = juce::roundToInt (get (velocity));

        // Beats: the song's while playing (steps on the grid); a free clock at the song's tempo when stopped
        double startBeat, endBeat;
        tempoPosition.set (rc.editTime.getStart());

        if (rc.isPlaying)
        {
            startBeat = tempoPosition.getBeats().inBeats();
            tempoPosition.set (rc.editTime.getEnd());
            endBeat = tempoPosition.getBeats().inBeats();
        }
        else
        {
            startBeat = freeBeat;
            endBeat = freeBeat + numSamples / sampleRate * tempoPosition.getTempo() / 60.0;
        }

        freeBeat = endBeat;
        arp.process (startBeat, endBeat, numSamples, s, emit);

        // Back in time order (stable, so an off stays before an on at the same moment); a few messages, no allocation
        for (int i = 1; i < midi.size(); ++i)
            for (int j = i; j > 0 && midi[j - 1].getTimeStamp() > midi[j].getTimeStamp(); --j)
                std::swap (midi[j - 1], midi[j]);
    }
}
