/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Arpeggiator: a MIDI effect that sits before the track's instrument and turns held notes into
    a pattern in time with the song. Up, down, up and down, as played or random; one to four
    octaves; 1/4 to 1/32 and triplets; gate; latch; fixed or played velocity.

    Steps lock to the song's beats while playing and run free at the song's tempo when stopped, so
    it can be played live. Other MIDI (pitch bend, sustain, controllers) passes through.
*/

#pragma once

#include "Effects.h"
#include "Arpeggiator.h"

namespace ez
{
    class ArpeggiatorPlugin : public EffectPlugin
    {
    public:
        enum { rate, mode, octaves, gate, latch, velocity };

        explicit ArpeggiatorPlugin (te::PluginCreationInfo);
        ~ArpeggiatorPlugin() override                      { teardown(); }
        static const char* getPluginName()                  { return NEEDS_TRANS ("Arpeggiator"); }
        static const char* xmlTypeName;
        juce::String getName() const override               { return TRANS ("Arpeggiator"); }
        juce::String getPluginType() override               { return xmlTypeName; }
        juce::String getShortName (int) override            { return "Arp"; }
        juce::String getSelectableDescription() override    { return TRANS ("Arpeggiator"); }

        /** The step length, in beats, for a Rate choice (1/4 .. 1/32, then triplets). */
        static double stepBeatsFor (int rateChoice) noexcept;

        bool takesMidiInput() override                      { return true; }
        void applyToBuffer (const te::PluginRenderContext&) override;

    protected:
        void prepare (double, int) override                 { arp.reset(); }
        void process (float* const*, int, int) override     {}   // audio passes through untouched

    private:
        Arpeggiator arp;
        te::tempo::Sequence::Position tempoPosition { te::createPosition (edit.tempoSequence) };
        double freeBeat = 0.0;   // the clock when the song is stopped
        int channel = 1;
    };
}
