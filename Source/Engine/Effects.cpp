/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "Effects.h"
#include "VocalChain.h"
#include "Mastering.h"

namespace ez
{
    namespace
    {
        juce::NormalisableRange<float> rangeFor (const EffectPlugin::Control& c)
        {
            if (! c.choices.isEmpty())
                return { 0.0f, (float) (c.choices.size() - 1), 1.0f };

            juce::NormalisableRange<float> range (c.min, c.max);

            if (c.centre > c.min && c.centre < c.max)
                range.setSkewForCentre (c.centre);

            return range;
        }

        juce::String format (const EffectPlugin::Control& c, float value)
        {
            if (! c.choices.isEmpty())
                return c.choices[juce::jlimit (0, c.choices.size() - 1, juce::roundToInt (value))];

            if (c.atMin != nullptr && value <= c.min + (c.max - c.min) * 0.001f)
                return c.atMin;

            const juce::String unit (c.unit);

            if (unit == "%")
                return juce::String (juce::roundToInt (value)) + "%";

            // (juce::String (v, 0) means "every digit": whole numbers need their own path)
            const auto number = c.decimals <= 0 ? juce::String (juce::roundToInt (value)) : juce::String (value, c.decimals);
            return number + (unit.isEmpty() ? "" : " " + unit);
        }

        bool isOn (float v) noexcept     { return v >= 0.5f; }
    }

    //==============================================================================
    EffectPlugin::EffectPlugin (te::PluginCreationInfo info, std::vector<Control> list)
        : te::Plugin (info), controls (std::move (list))
    {
        auto* um = getUndoManager();

        for (const auto& c : controls)
        {
            // Write a missing value now, outside any undo step: an undo that removed the property would
            // make the parameter write the default back through the undo manager while it is undoing
            if (! state.hasProperty (juce::Identifier (c.id)))
                state.setProperty (juce::Identifier (c.id), c.defaultValue, nullptr);

            auto* value = values.add (new juce::CachedValue<float>());
            value->referTo (state, juce::Identifier (c.id), um, c.defaultValue);

            const auto copy = c;
            auto* p = addParam (c.id, TRANS (c.name), rangeFor (c),
                                [copy] (float v) { return format (copy, v); },
                                [copy] (const juce::String& s)
                                {
                                    for (int i = 0; i < copy.choices.size(); ++i)
                                        if (s.equalsIgnoreCase (copy.choices[i]))
                                            return (float) i;

                                    return s.getFloatValue();
                                });
            p->attachToCurrentValue (*value);
            params.push_back (p);
        }
    }

    EffectPlugin::~EffectPlugin()
    {
        teardown();   // (each subclass already did, in its own destructor, as Tracktion asks)
    }

    void EffectPlugin::teardown()
    {
        if (std::exchange (tornDown, true))
            return;

        // Detach first: notifyListenersOfDeletion deletes the plugin's parameters
        for (auto& p : params)
            p->detachFromCurrentValue();

        notifyListenersOfDeletion();
    }

    void EffectPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
    {
        // Undo/redo changes the stored value only: Tracktion leaves the parameter (what the sound uses)
        // where it was on purpose. Bring it along, or an undone knob would still sound moved
        if (v == state)
            if (auto* um = getUndoManager(); um != nullptr && um->isPerformingUndoRedo())
                for (size_t i = 0; i < params.size(); ++i)
                    if (values[(int) i]->getPropertyID() == id)
                    {
                        values[(int) i]->forceUpdateOfCachedValue();   // it may hear about the change after us
                        params[i]->updateFromAttachedValue();
                    }

        te::Plugin::valueTreePropertyChanged (v, id);
    }

    void EffectPlugin::set (int index, float value)
    {
        if (auto* p = getControl (index))
            p->setParameter (juce::jlimit (p->getValueRange().getStart(), p->getValueRange().getEnd(), value), juce::sendNotification);
    }

    void EffectPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        sampleRate = info.sampleRate > 0 ? info.sampleRate : 44100.0;
        prepare (sampleRate, juce::jmax (16, info.blockSizeSamples));
        prepared = true;
    }

    void EffectPlugin::applyToBuffer (const te::PluginRenderContext& rc)
    {
        if (rc.destBuffer == nullptr || rc.bufferNumSamples <= 0 || ! prepared.load())
            return;

        SCOPED_REALTIME_CHECK

        auto& buffer = *rc.destBuffer;
        const auto numChannels = juce::jmin (2, buffer.getNumChannels());

        if (numChannels <= 0)
            return;

        // Stereo effects: anything past two channels is silenced, as Tracktion's own effects do
        if (buffer.getNumChannels() > 2)
            for (int c = 2; c < buffer.getNumChannels(); ++c)
                buffer.clear (c, rc.bufferStartSample, rc.bufferNumSamples);

        float* channels[2] = { buffer.getWritePointer (0, rc.bufferStartSample),
                               numChannels > 1 ? buffer.getWritePointer (1, rc.bufferStartSample) : nullptr };
        // Peaks for the editor's meters: the highest since the UI last took them (no locks, no allocation)
        const auto peakOf = [&]
        {
            float peak = 0.0f;

            for (int c = 0; c < numChannels; ++c)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax (channels[c], rc.bufferNumSamples);
                peak = juce::jmax (peak, std::abs (range.getStart()), std::abs (range.getEnd()));
            }

            return peak;
        };

        const auto raise = [] (std::atomic<float>& target, float value)
        {
            auto current = target.load (std::memory_order_relaxed);
            while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed)) {}
        };

        raise (inputPeak, peakOf());
        process (channels, numChannels, rc.bufferNumSamples);
        raise (outputPeak, peakOf());
    }

    void EffectPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        for (int i = 0; i < values.size(); ++i)
        {
            auto& value = *values[i];

            if (auto* p = v.getPropertyPointer (value.getPropertyID()))
                value = (float) *p;
            else
                value.resetToDefault();

            params[(size_t) i]->updateFromAttachedValue();   // the sound follows, as in Tracktion's own plugins
        }
    }

    //==============================================================================
    const char* GatePlugin::xmlTypeName = "ezgate";

    GatePlugin::GatePlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, { { "threshold", "Threshold", -80.0f, 0.0f, -50.0f, -30.0f, "dB" },
                                { "floor",     "Floor",     -80.0f, 0.0f, -80.0f, -30.0f, "dB" },
                                { "attack",    "Attack",    0.1f, 50.0f, 1.0f, 5.0f, "ms" },
                                { "hold",      "Hold",      0.0f, 500.0f, 50.0f, 100.0f, "ms", 0 },
                                { "release",   "Release",   5.0f, 1000.0f, 120.0f, 150.0f, "ms", 0 } })
    {
    }

    void GatePlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        gate.thresholdDb = get (threshold);
        gate.rangeDb = get (floor);
        gate.attackMs = get (attack);
        gate.holdMs = get (hold);
        gate.releaseMs = get (release);
        gate.process (ch, numChannels, numSamples);
        gateGain = gate.getGain();
    }

    //==============================================================================
    const char* DeEsserPlugin::xmlTypeName = "ezdeesser";

    DeEsserPlugin::DeEsserPlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, { { "frequency", "Frequency", 3000.0f, 12000.0f, 6500.0f, 6000.0f, "Hz", 0 },
                                { "threshold", "Threshold", -60.0f, 0.0f, -30.0f, 0.0f, "dB" },
                                { "most",      "Most cut",  -24.0f, 0.0f, -12.0f, 0.0f, "dB" } })
    {
    }

    void DeEsserPlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        deEsser.freq = get (frequency);
        deEsser.thresholdDb = get (threshold);
        deEsser.maxReductionDb = get (most);
        deEsser.process (ch, numChannels, numSamples);
    }

    //==============================================================================
    const char* LimiterPlugin::xmlTypeName = "ezlimiter";

    LimiterPlugin::LimiterPlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, { { "input",   "Input",   0.0f, 24.0f, 0.0f, 6.0f, "dB" },
                                { "ceiling", "Ceiling", -12.0f, 0.0f, -1.0f, 0.0f, "dB" },
                                { "release", "Release", 10.0f, 500.0f, 80.0f, 100.0f, "ms", 0 } })
    {
    }

    void LimiterPlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        limiter.inputDb = get (input);
        limiter.ceilingDb = get (ceiling);
        limiter.releaseMs = get (release);
        limiter.process (ch, numChannels, numSamples);
    }

    //==============================================================================
    const char* CutFilterPlugin::xmlTypeName = "ezcutfilter";

    CutFilterPlugin::CutFilterPlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, { { "lowcut",  "Low cut",  20.0f, 1000.0f, 80.0f, 150.0f, "Hz", 0 },
                                { "highcut", "High cut", 1000.0f, 20000.0f, 20000.0f, 6000.0f, "Hz", 0 } })
    {
    }

    void CutFilterPlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        // At the ends of their ranges they are off: nothing is filtered
        const auto lowHz = get (lowCut), highHz = get (highCut);

        if (lowHz != lastLow)       { low.set (dsp::Biquad::Type::highPass, sampleRate, lowHz, 0.707f); lastLow = lowHz; }
        if (highHz != lastHigh)     { high.set (dsp::Biquad::Type::lowPass, sampleRate, highHz, 0.707f); lastHigh = highHz; }

        const bool useLow = lowHz > 20.5f, useHigh = highHz < 19900.0f;

        // A band coming back on starts from silence, not from where it was left (no click)
        if (useLow && ! lowWasOn)     low.reset();
        if (useHigh && ! highWasOn)   high.reset();

        lowWasOn = useLow;
        highWasOn = useHigh;

        for (int c = 0; c < numChannels; ++c)
            for (int i = 0; i < numSamples; ++i)
            {
                auto x = ch[c][i];

                if (useLow)     x = low.process (x, c);
                if (useHigh)    x = high.process (x, c);

                ch[c][i] = x;
            }
    }

    //==============================================================================
    const char* SaturatorPlugin::xmlTypeName = "ezsaturator";

    SaturatorPlugin::SaturatorPlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, { { "drive",  "Drive",  0.0f, 36.0f, 6.0f, 9.0f, "dB" },
                                { "curve",  "Type",   0.0f, 2.0f, 0.0f, 0.0f, "", 0, { "Tape", "Tube", "Hard" } },
                                { "tone",   "Tone",   1000.0f, 20000.0f, 12000.0f, 6000.0f, "Hz", 0 },
                                { "mix",    "Mix",    0.0f, 100.0f, 100.0f, 0.0f, "%" },
                                { "output", "Output", -24.0f, 12.0f, 0.0f, 0.0f, "dB" } })
    {
    }

    void SaturatorPlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        saturator.driveDb = get (drive);
        saturator.curve = (dsp::Saturator::Curve) juce::jlimit (0, 2, juce::roundToInt (get (curve)));
        saturator.toneHz = get (tone);
        saturator.mix = get (mix) * 0.01f;
        saturator.outputDb = get (output);
        saturator.process (ch, numChannels, numSamples);
    }

    //==============================================================================
    const char* TunePlugin::xmlTypeName = "eztune";

    TunePlugin::TunePlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, { { "amount", "Amount", 0.0f, 100.0f, 100.0f, 0.0f, "%" },
                                { "speed",  "Speed",  1.0f, 400.0f, 40.0f, 60.0f, "ms", 0 },
                                { "root",   "Key",    0.0f, 11.0f, 0.0f, 0.0f, "", 0, { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" } },
                                { "scale",  "Scale",  0.0f, 2.0f, 0.0f, 0.0f, "", 0, { "Chromatic", "Major", "Minor" } } })
    {
    }

    void TunePlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        corrector.amount = get (amount) * 0.01f;
        corrector.speedMs = get (speed);
        corrector.root = juce::jlimit (0, 11, juce::roundToInt (get (root)));
        corrector.scale = (dsp::PitchCorrector::Scale) juce::jlimit (0, 2, juce::roundToInt (get (scale)));
        corrector.process (ch, numChannels, numSamples);
        detected = corrector.getDetectedHz();
    }

    //==============================================================================
    const char* TremoloPlugin::xmlTypeName = "eztremolo";

    TremoloPlugin::TremoloPlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, { { "rate",  "Rate",  0.1f, 20.0f, 4.0f, 3.0f, "Hz", 2 },
                                { "depth", "Depth", 0.0f, 100.0f, 50.0f, 0.0f, "%" },
                                { "mode",  "Mode",  0.0f, 1.0f, 0.0f, 0.0f, "", 0, { "Tremolo", "Auto-pan" } } })
    {
    }

    void TremoloPlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        tremolo.rateHz = get (rate);
        tremolo.depth = get (depth) * 0.01f;
        tremolo.autoPan = isOn (get (mode));
        tremolo.process (ch, numChannels, numSamples);
    }

    //==============================================================================
    const char* UtilityPlugin::xmlTypeName = "ezutility";

    UtilityPlugin::UtilityPlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, { { "gain",  "Gain",  -36.0f, 24.0f, 0.0f, 0.0f, "dB" },
                                { "width", "Width", 0.0f, 200.0f, 100.0f, 0.0f, "%" },
                                { "mono",  "Mono",  0.0f, 1.0f, 0.0f, 0.0f, "", 0, { "Off", "On" } },
                                { "flipl", "Flip left",  0.0f, 1.0f, 0.0f, 0.0f, "", 0, { "Off", "On" } },
                                { "flipr", "Flip right", 0.0f, 1.0f, 0.0f, 0.0f, "", 0, { "Off", "On" } } })
    {
    }

    void UtilityPlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        utility.gainDb = get (gain);
        utility.width = get (width) * 0.01f;
        utility.mono = isOn (get (mono));
        utility.flipLeft = isOn (get (flipLeft));
        utility.flipRight = isOn (get (flipRight));
        utility.process (ch, numChannels, numSamples);
    }

    //==============================================================================
    //==============================================================================
    const char* AmpPlugin::xmlTypeName = "ezamp";

    AmpPlugin::AmpPlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, { { "mode",     "Amp",      0.0f, 5.0f, 1.0f, 0.0f, "", 0, modeNames() },
                                { "gate",     "Gate",     -96.0f, -20.0f, -96.0f, -60.0f, "dB", 0, {}, "Off" },
                                { "gain",     "Gain",     0.0f, 10.0f, 5.0f, 0.0f, "", 1 },
                                { "tight",    "Tight",    20.0f, 300.0f, 60.0f, 90.0f, "Hz", 0 },
                                { "bass",     "Bass",     -12.0f, 12.0f, 0.0f, 0.0f, "dB" },
                                { "mid",      "Mid",      -12.0f, 12.0f, 0.0f, 0.0f, "dB" },
                                { "treble",   "Treble",   -12.0f, 12.0f, 0.0f, 0.0f, "dB" },
                                { "presence", "Presence", -12.0f, 12.0f, 0.0f, 0.0f, "dB" },
                                { "cab",      "Cabinet",  0.0f, 4.0f, 1.0f, 0.0f, "", 0, cabNames() },
                                { "volume",   "Volume",   -24.0f, 12.0f, 0.0f, 0.0f, "dB" },
                                { "blend",    "Clean blend", 0.0f, 100.0f, 0.0f, 0.0f, "%", 0 } })
    {
    }

    void AmpPlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        amp.mode = (dsp::Amp::Mode) juce::jlimit (0, 5, juce::roundToInt (get (mode)));
        amp.cab = (dsp::Amp::Cab) juce::jlimit (0, 4, juce::roundToInt (get (cab)));
        amp.gateDb = get (gate);
        amp.gain = get (gain);
        amp.tightHz = get (tight);
        amp.bassDb = get (bass);
        amp.midDb = get (mid);
        amp.trebleDb = get (treble);
        amp.presenceDb = get (presence);
        amp.volumeDb = get (volume);
        amp.blend = get (blend) * 0.01f;
        amp.process (ch, numChannels, numSamples);
        gateGain = amp.getGateGain();
    }

    //==============================================================================
    const char* TunerPlugin::xmlTypeName = "eztuner";

    TunerPlugin::TunerPlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, { { "reference", "A =", 430.0f, 450.0f, 440.0f, 0.0f, "Hz", 0 } })
    {
    }

    void TunerPlugin::prepare (double, int)
    {
        decimation = juce::jmax (1, juce::roundToInt (sampleRate / analysisRate));
        actualRate = sampleRate / decimation;
        antiAlias.set (dsp::Biquad::Type::lowPass, sampleRate, (float) juce::jmin (4000.0, actualRate * 0.45), 0.707f);
        antiAlias.reset();
        decimCount = 0;
        decimSum = 0;
    }

    void TunerPlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        auto pos = written.load (std::memory_order_relaxed);
        const auto start = pos;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto mono = numChannels > 1 ? (ch[0][i] + ch[1][i]) * 0.5f : ch[0][i];
            decimSum += antiAlias.process (mono, 0);

            if (++decimCount >= decimation)
            {
                ring[(size_t) (pos & (ringSize - 1))].store (decimSum / (float) decimation, std::memory_order_relaxed);
                ++pos;
                decimCount = 0;
                decimSum = 0;
            }
        }

        written.store (pos, std::memory_order_release);

        if (pos - start > 0 && pos >= 2048)
            primed.store (true, std::memory_order_relaxed);

        if (muted.load())
            for (int c = 0; c < numChannels; ++c)
                juce::FloatVectorOperations::clear (ch[c], numSamples);
    }

    TunerPlugin::Reading TunerPlugin::analyse (const float* samples, int numSamples, double rate, float referenceHz)
    {
        Reading r;

        if (samples == nullptr || numSamples <= 0)
            return r;

        double sum = 0;

        for (int i = 0; i < numSamples; ++i)
            sum += (double) samples[i] * samples[i];

        r.levelDb = dsp::gainToDb ((float) std::sqrt (sum / numSamples));

        if (r.levelDb < -55.0f)
            return r;   // nothing playing

        std::vector<float> scratch ((size_t) (rate / 25.0) + 4);
        // 27 Hz (a 5-string bass's low B is 31 Hz) to 1500 Hz (a guitar's high frets)
        r.hz = dsp::detectPitch (samples, numSamples, rate, 27.0f, 1500.0f, scratch.data(), (int) scratch.size());

        if (r.hz <= 0)
            return r;

        const auto ref = juce::jlimit (400.0f, 480.0f, referenceHz);
        const auto exact = 69.0f + 12.0f * std::log2 (r.hz / ref);
        r.midiNote = juce::roundToInt (exact);
        r.cents = (exact - (float) r.midiNote) * 100.0f;
        r.hasNote = r.midiNote >= 12 && r.midiNote <= 120;

        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        r.noteName = names[((r.midiNote % 12) + 12) % 12];
        r.octave = r.midiNote / 12 - 1;
        return r;
    }

    TunerPlugin::Reading TunerPlugin::read()
    {
        // The newest 2048 analysis samples (186 ms: four cycles of a bass's low B)
        constexpr int length = 2048;
        const auto end = written.load (std::memory_order_acquire);

        if (! primed.load (std::memory_order_relaxed))
            return {};

        // The track stopped playing into it (disarmed, a bypass, playback stopped): nothing to show
        const auto now = juce::Time::getMillisecondCounter();

        if (end != lastEnd)
        {
            lastEnd = end;
            lastChange = now;
        }
        else if (now - lastChange > 300)
        {
            return {};
        }

        copy.resize ((size_t) length);

        for (int i = 0; i < length; ++i)
            copy[(size_t) i] = ring[(size_t) ((end - (juce::uint32) length + (juce::uint32) i) & (ringSize - 1))].load (std::memory_order_relaxed);

        return analyse (copy.data(), length, actualRate, getStored (reference));
    }

    void Effects::registerAll (te::Engine& engine)
    {
        auto& pm = engine.getPluginManager();
        pm.createBuiltInType<GatePlugin>();
        pm.createBuiltInType<DeEsserPlugin>();
        pm.createBuiltInType<LimiterPlugin>();
        pm.createBuiltInType<CutFilterPlugin>();
        pm.createBuiltInType<SaturatorPlugin>();
        pm.createBuiltInType<TunePlugin>();
        pm.createBuiltInType<TremoloPlugin>();
        pm.createBuiltInType<UtilityPlugin>();
        pm.createBuiltInType<AmpPlugin>();
        pm.createBuiltInType<TunerPlugin>();
        pm.createBuiltInType<VocalChainPlugin>();
        pm.createBuiltInType<MasteringChainPlugin>();   // the master's own (Master view), never in the effect lists
        pm.createBuiltInType<ReferencePlugin>();
    }

    juce::StringArray Effects::categories()
    {
        return { "Chains", "Guitar & bass", "Dynamics", "EQ & Filter", "Space", "Colour", "Pitch", "Modulation", "Utility" };
    }

    juce::String Effects::categoryOf (const juce::String& type)
    {
        if (type == te::CompressorPlugin::xmlTypeName || type == GatePlugin::xmlTypeName
             || type == DeEsserPlugin::xmlTypeName || type == LimiterPlugin::xmlTypeName)     return "Dynamics";
        if (type == te::EqualiserPlugin::xmlTypeName || type == te::LowPassPlugin::xmlTypeName
             || type == CutFilterPlugin::xmlTypeName)                                          return "EQ & Filter";
        if (type == te::ReverbPlugin::xmlTypeName || type == te::DelayPlugin::xmlTypeName)     return "Space";
        if (type == SaturatorPlugin::xmlTypeName)                                              return "Colour";
        if (type == TunePlugin::xmlTypeName)                                                   return "Pitch";
        if (type == te::ChorusPlugin::xmlTypeName || type == te::PhaserPlugin::xmlTypeName
             || type == TremoloPlugin::xmlTypeName)                                            return "Modulation";
        if (type == UtilityPlugin::xmlTypeName)                                                return "Utility";
        if (type == VocalChainPlugin::xmlTypeName)                                             return "Chains";
        if (type == AmpPlugin::xmlTypeName || type == TunerPlugin::xmlTypeName)               return "Guitar & bass";
        return {};
    }
}
