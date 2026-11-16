/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "VocalChain.h"

namespace ez
{
    namespace
    {
        const juce::StringArray offOn { "Off", "On" };

        float lerp (float a, float b, float t) noexcept     { return a + (b - a) * t; }
        float logLerp (float a, float b, float t) noexcept  { return a * std::pow (b / a, t); }

        std::vector<EffectPlugin::Control> makeControls()
        {
            const auto& first = VocalChainPlugin::getPresets().front();
            std::vector<EffectPlugin::Control> c;

            for (int m = 0; m < VocalChainPlugin::numMacros; ++m)
            {
                static const char* ids[] = { "clean", "tune", "punch", "bright", "space", "width" };
                static const char* names[] = { "Clean", "Tune", "Punch", "Bright", "Space", "Width" };
                c.push_back ({ ids[m], names[m], 0.0f, 100.0f, first.macros[m], 0.0f, "%" });
            }

            static const char* moduleIds[] = { "cleanon", "tuneon", "toneon", "comp1on", "deesson", "comp2on", "colouron", "airon", "spaceon", "widthon" };
            static const char* moduleNames[] = { "Clean-up on", "Tune on", "Tone EQ on", "Comp 1 on", "De-esser on", "Comp 2 on", "Colour on", "Air EQ on", "Space on", "Width on" };

            for (int m = 0; m < VocalChainPlugin::numModules; ++m)
                c.push_back ({ moduleIds[m], moduleNames[m], 0.0f, 1.0f, 1.0f, 0.0f, "", 0, offOn });

            c.push_back ({ "root",  "Key",   0.0f, 11.0f, 0.0f, 0.0f, "", 0, { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" } });
            c.push_back ({ "scale", "Scale", 0.0f, 2.0f, 0.0f, 0.0f, "", 0, { "Chromatic", "Major", "Minor" } });
            c.push_back ({ "phone", "Phone tone", 0.0f, 1.0f, 0.0f, 0.0f, "", 0, offOn });
            return c;
        }
    }

    const char* VocalChainPlugin::xmlTypeName = "ezvocalchain";

    const std::vector<VocalChainPlugin::Preset>& VocalChainPlugin::getPresets()
    {
        //                                                                         Clean Tune Punch Bright Space Width
        static const std::vector<Preset> presets
        {
            { "Bright, upfront rap vocal", "Lead vocal, hip hop: close and clear",  { 40, 25, 60, 55, 20, 30 } },
            { "Warm R&B lead",             "Lead vocal, smooth and roomy",          { 30, 35, 40, 30, 45, 25 } },
            { "Tight hook double",         "Hooks and doubles: tuned, wide",        { 45, 60, 70, 50, 15, 70 } },
            { "Robot tune",                "The hard-tuned hip hop sound",          { 40, 100, 55, 50, 15, 20 } },
            { "Phone-filter adlib",        "Lo-fi, narrow, for adlibs",             { 50, 0, 60, 20, 10, 0 }, true, 1 << 9 },
            { "Wide backing vocals",       "Group vocals, spread out",              { 35, 30, 45, 40, 40, 90 } },
            { "Natural (start here)",      "Light touch: clean and even",           { 25, 0, 35, 30, 15, 0 }, false, 1 << 1 },
            { "Podcast voice",             "Speech: clear, even, dry",              { 55, 0, 65, 35, 0, 0 }, false, (1 << 1) | (1 << 8) | (1 << 9) },
        };

        return presets;
    }

    juce::String VocalChainPlugin::macroName (int m)
    {
        static const char* names[] = { "Clean", "Tune", "Punch", "Bright", "Space", "Width" };
        return juce::isPositiveAndBelow (m, numMacros) ? names[m] : "";
    }

    juce::String VocalChainPlugin::macroHint (int m)
    {
        static const char* hints[] = { "Cuts rumble and room noise", "Pulls notes to the key", "Evens out loud and quiet words",
                                       "Adds clarity and presence", "Puts the voice in a room", "Spreads it left and right" };
        return juce::isPositiveAndBelow (m, numMacros) ? hints[m] : "";
    }

    juce::String VocalChainPlugin::moduleName (int m)
    {
        static const char* names[] = { "Clean-up (HPF, gate)", "Pitch correction", "Tone EQ", "Comp 1", "De-esser", "Comp 2", "Colour", "Air EQ", "Space", "Width" };
        return juce::isPositiveAndBelow (m, numModules) ? names[m] : "";
    }

    juce::String VocalChainPlugin::moduleHint (int m)
    {
        static const char* hints[] = { "High-pass filter and a gentle gate (Clean)", "Pulls each note to the key (Tune)",
                                       "Takes out boxiness, adds presence (Bright)", "Fast compressor: catches peaks (Punch)",
                                       "Turns down harsh \"s\" sounds (Bright)", "Slow compressor: levels phrases (Punch)",
                                       "Saturation: warmth and grit (Punch)", "High shelf: air on top (Bright)",
                                       "Reverb inside the chain (Space)", "Doubling delays left and right (Width)" };
        return juce::isPositiveAndBelow (m, numModules) ? hints[m] : "";
    }

    //==============================================================================
    VocalChainPlugin::VocalChainPlugin (te::PluginCreationInfo info)
        : EffectPlugin (info, makeControls())
    {
        if (! state.hasProperty ("ezPreset"))
            state.setProperty ("ezPreset", getPresets().front().name, nullptr);   // never removed by an undo (see EffectPlugin)

        presetName.referTo (state, "ezPreset", getUndoManager(), getPresets().front().name);
    }

    void VocalChainPlugin::prepare (double sr, int maxBlock)
    {
        highPass.reset(); phoneLowPass.reset(); lowMid.reset(); presence.reset(); air.reset();
        gate.prepare (sr);
        corrector.prepare (sr);
        comp1.prepare (sr);
        comp2.prepare (sr);
        deEsser.prepare (sr);
        colour.prepare (sr);
        reverb.setSampleRate (sr);
        reverb.reset();
        wetBuffer.setSize (2, juce::jmax (64, maxBlock), false, true, false);
        doubleLine.setSize (1, (int) std::ceil (sr * 0.05) + 4, false, true, false);
        doubleLine.clear();
        doubleWrite = 0;
        doublePhase = 0;
        lastClean = lastBright = lastPhone = -1;
    }

    void VocalChainPlugin::updateFilters()
    {
        const auto c = macro (clean), b = macro (bright), ph = on (phone) ? 1.0f : 0.0f;

        if (c == lastClean && b == lastBright && ph == lastPhone)
            return;

        lastClean = c; lastBright = b; lastPhone = ph;
        highPass.set (dsp::Biquad::Type::highPass, sampleRate, ph > 0 ? 450.0f : logLerp (40.0f, 160.0f, c), 0.707f);
        phoneLowPass.set (dsp::Biquad::Type::lowPass, sampleRate, 3200.0f, 0.9f);
        lowMid.set (dsp::Biquad::Type::peak, sampleRate, 320.0f, 1.0f, -3.0f * b);
        presence.set (dsp::Biquad::Type::peak, sampleRate, 3500.0f, 0.8f, 4.5f * b);
        air.set (dsp::Biquad::Type::highShelf, sampleRate, 10000.0f, 0.7f, 6.0f * b);
    }

    void VocalChainPlugin::process (float* const* ch, int numChannels, int numSamples)
    {
        updateFilters();
        const auto c = macro (clean), t = macro (tune), p = macro (punch), b = macro (bright), s = macro (space), w = macro (width);
        const bool phoneTone = on (phone);
        bool module[numModules];

        for (int m = 0; m < numModules; ++m)
            module[m] = on (firstModule + m);   // read each switch once a block, not per sample

        const auto isOn = [&module] (int control) { return module[control - firstModule]; };

        // Settings for this block, from the macros
        gate.thresholdDb = c <= 0.01f ? -120.0f : lerp (-70.0f, -45.0f, c);
        gate.rangeDb = lerp (-6.0f, -30.0f, c);
        gate.attackMs = 2; gate.holdMs = 80; gate.releaseMs = 150;
        corrector.amount = isOn (tuneOn) ? t : 0.0f;   // off: it still runs, passing the sound (a constant delay)
        corrector.speedMs = lerp (150.0f, 5.0f, t);
        corrector.root = juce::jlimit (0, 11, juce::roundToInt (get (root)));
        corrector.scale = (dsp::PitchCorrector::Scale) juce::jlimit (0, 2, juce::roundToInt (get (scale)));
        comp1.thresholdDb = lerp (-8.0f, -28.0f, p); comp1.ratio = lerp (2.0f, 6.0f, p);
        comp1.attackMs = 3; comp1.releaseMs = 60; comp1.kneeDb = 6; comp1.makeupDb = lerp (0.0f, 6.0f, p);
        comp2.thresholdDb = lerp (-12.0f, -22.0f, p); comp2.ratio = 2; comp2.attackMs = 30; comp2.releaseMs = 250;
        comp2.kneeDb = 10; comp2.makeupDb = lerp (0.0f, 3.0f, p);
        deEsser.freq = 6500; deEsser.thresholdDb = lerp (-22.0f, -34.0f, b); deEsser.maxReductionDb = -10;
        colour.driveDb = phoneTone ? 18.0f : lerp (1.0f, 8.0f, p); colour.mix = phoneTone ? 1.0f : 0.5f;
        colour.toneHz = phoneTone ? 4000.0f : 14000.0f; colour.curve = dsp::Saturator::Curve::tape;

        juce::Reverb::Parameters verb;
        verb.roomSize = lerp (0.3f, 0.8f, s);
        verb.damping = 0.5f;
        verb.wetLevel = 1.0f;
        verb.dryLevel = 0.0f;
        verb.width = 1.0f;
        reverb.setParameters (verb);

        // Filters and the gate
        for (int k = 0; k < numChannels; ++k)
            for (int i = 0; i < numSamples; ++i)
            {
                auto x = ch[k][i];

                if (isOn (cleanOn))   x = highPass.process (x, k);
                if (phoneTone)      x = phoneLowPass.process (x, k);

                ch[k][i] = x;
            }

        // A part switched back on starts fresh: no stale reverb tail, delay line or frozen gate
        const bool spaceRuns = isOn (spaceOn) && s > 0.001f, widthRuns = isOn (widthOn) && w > 0.001f, gateRuns = isOn (cleanOn) && c > 0.01f;

        if (spaceRuns && ! spaceRan)    reverb.reset();
        if (widthRuns && ! widthRan)    doubleLine.clear();
        if (gateRuns && ! gateRan)      gate.prepare (sampleRate);

        spaceRan = spaceRuns;
        widthRan = widthRuns;
        gateRan = gateRuns;

        if (gateRuns)  gate.process (ch, numChannels, numSamples);
        corrector.process (ch, numChannels, numSamples);

        if (isOn (toneOn))
            for (int k = 0; k < numChannels; ++k)
                for (int i = 0; i < numSamples; ++i)
                    ch[k][i] = presence.process (lowMid.process (ch[k][i], k), k);

        float reduction = 0;

        if (isOn (comp1On))   { comp1.process (ch, numChannels, numSamples); reduction += comp1.getReductionDb(); }
        if (isOn (deEssOn))   deEsser.process (ch, numChannels, numSamples);
        if (isOn (comp2On))   { comp2.process (ch, numChannels, numSamples); reduction += comp2.getReductionDb(); }
        if (isOn (colourOn))  colour.process (ch, numChannels, numSamples);

        punchReduction = reduction;

        if (isOn (airOn))
            for (int k = 0; k < numChannels; ++k)
                for (int i = 0; i < numSamples; ++i)
                    ch[k][i] = air.process (ch[k][i], k);

        // Space: a reverb mixed in (in chunks, never more than the buffer we made)
        if (spaceRuns)
        {
            const auto wet = 0.35f * s;

            for (int start = 0; start < numSamples; start += wetBuffer.getNumSamples())
            {
                const auto n = juce::jmin (wetBuffer.getNumSamples(), numSamples - start);
                auto* l = wetBuffer.getWritePointer (0);
                auto* r = wetBuffer.getWritePointer (1);

                for (int i = 0; i < n; ++i)
                {
                    l[i] = ch[0][start + i];
                    r[i] = numChannels > 1 ? ch[1][start + i] : ch[0][start + i];
                }

                if (numChannels > 1)    reverb.processStereo (l, r, n);
                else                    reverb.processMono (l, n);

                for (int i = 0; i < n; ++i)
                {
                    ch[0][start + i] += l[i] * wet;

                    if (numChannels > 1)
                        ch[1][start + i] += r[i] * wet;
                }
            }
        }

        // Width: two short, slowly moving delays of the (mono) voice, one each side
        if (widthRuns && numChannels > 1)
        {
            const auto length = doubleLine.getNumSamples();
            const auto step = 0.3f / (float) sampleRate;
            const auto amount = 0.4f * w;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto mono = (ch[0][i] + ch[1][i]) * 0.5f;
                doubleLine.setSample (0, doubleWrite, mono);
                doublePhase += step;
                doublePhase -= std::floor (doublePhase);
                const auto wobble = 0.5f + 0.5f * std::sin (2.0f * dsp::pi * doublePhase);

                const auto read = [&] (float ms)
                {
                    auto pos = (float) doubleWrite - ms * 0.001f * (float) sampleRate;

                    while (pos < 0)
                        pos += (float) length;

                    const auto i0 = (int) pos % length, i1 = (i0 + 1) % length;
                    const auto frac = pos - std::floor (pos);
                    return doubleLine.getSample (0, i0) + (doubleLine.getSample (0, i1) - doubleLine.getSample (0, i0)) * frac;
                };

                ch[0][i] += amount * read (11.0f + 2.0f * wobble);
                ch[1][i] += amount * read (17.0f + 2.0f * (1.0f - wobble));
                doubleWrite = (doubleWrite + 1) % length;
            }
        }
    }
}
