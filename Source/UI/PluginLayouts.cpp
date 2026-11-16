/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "PluginLayouts.h"
#include "../Engine/Effects.h"

namespace ez::PluginLayouts
{
    namespace
    {
        using Size = Item::Size;
        using Style = Item::Style;
        using D = PluginDisplays::Type;

        Item knob (const juce::String& id, const juce::String& label, Size size = Size::normal)  { return { id, label, size, Style::knob }; }
        Item big (const juce::String& id, const juce::String& label)                              { return { id, label, Size::large, Style::knob }; }
        Item small (const juce::String& id, const juce::String& label)                            { return { id, label, Size::small, Style::knob }; }
        Item toggle (const juce::String& id, const juce::String& label)                           { return { id, label, Size::normal, Style::toggle }; }
        Item menu (const juce::String& id, const juce::String& label)                             { return { id, label, Size::normal, Style::menu }; }
        Item buttons (const juce::String& id, const juce::String& label)                          { return { id, label, Size::normal, Style::buttons }; }

        Setting choice (const juce::String& id, const juce::String& name, juce::StringArray labels, float defaultValue, std::vector<float> values = {})
        {
            Setting s;
            s.id = id;
            s.name = name;
            s.labels = std::move (labels);
            s.choiceValues = std::move (values);
            s.min = 0.0f;
            s.max = (float) s.labels.size() - 1.0f;
            s.interval = 1.0f;
            s.defaultValue = defaultValue;
            return s;
        }

        Setting onOff (const juce::String& id, const juce::String& name, bool defaultOn = false)
        {
            Setting s = choice (id, name, { "Off", "On" }, defaultOn ? 1.0f : 0.0f);
            s.isToggle = true;
            return s;
        }

        Setting number (const juce::String& id, const juce::String& name, float min, float max, float def, const char* unit,
                        int decimals, float centre = 0.0f, float scale = 1.0f)
        {
            Setting s;
            s.id = id;
            s.name = name;
            s.min = min;
            s.max = max;
            s.defaultValue = def;
            s.unit = unit;
            s.decimals = decimals;
            s.skewCentre = centre;
            s.displayScale = scale;
            return s;
        }

        juce::String n (const char* base, int i)   { return base + juce::String (i); }

        //==============================================================================
        Layout fourOsc()
        {
            Layout l;
            l.width = 1000;
            l.description = "Four oscillators, a filter, two LFOs, envelopes and effects";

            const juce::StringArray waves { "Off", "Sine", "Square", "Saw", "Triangle", "Noise" };

            for (int i = 1; i <= 4; ++i)
            {
                l.settings.push_back (choice (n ("waveShape", i), "Wave " + juce::String (i), waves, i == 1 ? 1.0f : 0.0f));
                l.settings.push_back (choice (n ("voices", i), "Voices " + juce::String (i), { "1", "2", "3", "4", "5", "6", "7", "8" }, 1.0f,
                                              { 1, 2, 3, 4, 5, 6, 7, 8 }));
            }

            const juce::StringArray lfoWaves { "Off", "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random" };
            const juce::StringArray beatNames { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16" };
            const std::vector<float> beats { 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };

            for (int i = 1; i <= 2; ++i)
            {
                l.settings.push_back (choice (n ("lfoWaveShape", i), "LFO " + juce::String (i) + " wave", lfoWaves, i == 1 ? 1.0f : 0.0f));
                l.settings.push_back (onOff (n ("lfoSync", i), "LFO " + juce::String (i) + " sync"));
                l.settings.push_back (choice (n ("lfoBeat", i), "LFO " + juce::String (i) + " length", beatNames, 1.0f, beats));
            }

            l.settings.push_back (choice ("filterType", "Filter type", { "Off", "Low-pass", "High-pass", "Band-pass", "Notch" }, 0.0f));
            l.settings.push_back (choice ("filterSlope", "Slope", { "12 dB", "24 dB" }, 12.0f, { 12.0f, 24.0f }));
            l.settings.push_back (onOff ("ampAnalog", "Analog", true));
            l.settings.push_back (onOff ("distortionOn", "Distortion"));
            l.settings.push_back (onOff ("chorusOn", "Chorus"));
            l.settings.push_back (onOff ("delayOn", "Delay"));
            l.settings.push_back (onOff ("reverbOn", "Reverb"));
            l.settings.push_back (choice ("delay", "Delay time", { "1/16", "1/8", "1/8 dotted", "1/4", "1/4 dotted", "1/2", "1 bar" }, 1.0f,
                                          { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f }));
            l.settings.push_back (choice ("voiceMode", "Voices", { "Mono", "Legato", "Poly" }, 2.0f));
            l.settings.push_back (choice ("voices", "Polyphony", { "1", "2", "4", "8", "16", "32" }, 32.0f, { 1, 2, 4, 8, 16, 32 }));

            // Page 1: the four oscillators
            Page oscillators { "Oscillators", {} };
            std::vector<Section> oscRow;

            for (int i = 1; i <= 4; ++i)
            {
                Section s;
                s.title = "Osc " + juce::String (i);
                s.display = D::oscWave;
                s.displayIds = { n ("waveShape", i), n ("pulseWidth", i), n ("voices", i), n ("detune", i) };
                s.displayHeight = 58;
                s.items = { menu (n ("waveShape", i), "Wave"), menu (n ("voices", i), "Unison"),
                            small (n ("tune", i), "Tune"), small (n ("fineTune", i), "Fine"),
                            small (n ("level", i), "Level"), small (n ("pulseWidth", i), "Width"),
                            small (n ("detune", i), "Detune"), small (n ("spread", i), "Spread"),
                            small (n ("pan", i), "Pan") };
                s.columns = 4;
                s.tip = "Oscillator " + juce::String (i) + ": pick a wave to switch it on. Unison stacks detuned copies for a thicker sound.";
                oscRow.push_back (s);
            }

            oscillators.rows.push_back (oscRow);
            l.pages.push_back (oscillators);

            // Page 2: filter and envelopes
            Page shape { "Filter & Amp", {} };
            Section filter;
            filter.title = "Filter";
            filter.display = D::synthFilter;
            filter.displayIds = { "filterType", "filterFreq", "filterResonance", "filterSlope" };
            filter.displayHeight = 96;
            filter.items = { buttons ("filterType", "Type"), buttons ("filterSlope", "Slope"),
                             big ("filterFreq", "Cut-off"), knob ("filterResonance", "Resonance"), knob ("filterAmount", "Env amount"),
                             knob ("filterKey", "Key track"), knob ("filterVelocity", "Velocity") };
            filter.columns = 5;
            filter.weight = 1.5f;
            filter.tip = "Drag the curve: left and right for the cut-off, up and down for resonance";

            Section filterEnv;
            filterEnv.title = "Filter envelope";
            filterEnv.display = D::envelope;
            filterEnv.displayIds = { "filterAttack", "filterDecay", "filterSustain", "filterRelease" };
            filterEnv.displayHeight = 96;
            filterEnv.items = { small ("filterAttack", "Attack"), small ("filterDecay", "Decay"), small ("filterSustain", "Sustain"), small ("filterRelease", "Release") };
            filterEnv.tip = "How the cut-off moves during a note (set Env amount on the filter). Drag the points.";

            Section amp;
            amp.title = "Amp envelope";
            amp.display = D::envelope;
            amp.displayIds = { "ampAttack", "ampDecay", "ampSustain", "ampRelease" };
            amp.displayHeight = 96;
            amp.items = { small ("ampAttack", "Attack"), small ("ampDecay", "Decay"), small ("ampSustain", "Sustain"), small ("ampRelease", "Release"),
                          small ("ampVelocity", "Velocity"), toggle ("ampAnalog", "Analog") };
            amp.tip = "The volume of each note over time. Drag the points.";

            shape.rows.push_back ({ filter, filterEnv, amp });
            l.pages.push_back (shape);

            // Page 3: modulation
            Page modulation { "Modulation", {} };
            std::vector<Section> lfoRow;

            for (int i = 1; i <= 2; ++i)
            {
                Section lfo;
                lfo.title = "LFO " + juce::String (i);
                lfo.display = D::lfo;
                lfo.displayIds = { n ("lfoWaveShape", i), n ("lfoRate", i), n ("lfoDepth", i) };
                lfo.displayHeight = 54;
                lfo.items = { menu (n ("lfoWaveShape", i), "Wave"), toggle (n ("lfoSync", i), "Sync to tempo"),
                              menu (n ("lfoBeat", i), "Length"),
                              small (n ("lfoRate", i), "Rate"), small (n ("lfoDepth", i), "Depth") };
                lfo.columns = 2;
                lfo.tip = "A slow wave that moves other controls: route it in Modulation";
                lfoRow.push_back (lfo);
            }

            for (int i = 1; i <= 2; ++i)
            {
                Section env;
                env.title = "Mod env " + juce::String (i);
                env.display = D::envelope;
                env.displayIds = { n ("modAttack", i), n ("modDecay", i), n ("modSustain", i), n ("modRelease", i) };
                env.displayHeight = 54;
                env.items = { small (n ("modAttack", i), "A"), small (n ("modDecay", i), "D"),
                              small (n ("modSustain", i), "S"), small (n ("modRelease", i), "R") };
                env.tip = "An envelope that moves other controls: route it in Modulation";
                lfoRow.push_back (env);
            }

            Section matrix;
            matrix.title = "Modulation";
            matrix.custom = "modMatrix";
            matrix.weight = 1.6f;
            matrix.tip = "Route an LFO, envelope, velocity or the mod wheel to any control";
            lfoRow.push_back (matrix);
            modulation.rows.push_back (lfoRow);
            l.pages.push_back (modulation);

            // Page 4: effects and voicing
            Page effects { "Effects", {} };
            Section dist { "Distortion", "distortionOn" };
            dist.items = { big ("distortion", "Drive") };
            dist.columns = 1;
            dist.weight = 0.6f;

            Section chorus { "Chorus", "chorusOn" };
            chorus.items = { small ("chorusSpeed", "Rate"), small ("chorusDepth", "Depth"), small ("chorusWidth", "Width"), small ("chorusMix", "Mix") };

            Section delay { "Delay", "delayOn" };
            delay.items = { menu ("delay", "Time"), small ("delayFeedback", "Feedback"), small ("delayCrossfeed", "Ping-pong"), small ("delayMix", "Mix") };
            delay.columns = 3;

            Section reverb { "Reverb", "reverbOn" };
            reverb.items = { small ("reverbSize", "Size"), small ("reverbDamping", "Damping"), small ("reverbWidth", "Width"), small ("reverbMix", "Mix") };

            Section voice { "Voice" };
            voice.items = { buttons ("voiceMode", "Mode"), menu ("voices", "Polyphony"), knob ("legato", "Glide"), big ("masterLevel", "Level") };
            voice.columns = 2;
            voice.weight = 1.1f;
            voice.tip = "Mono and Legato play one note at a time; Glide slides between overlapping notes";

            effects.rows.push_back ({ dist, chorus, delay, reverb, voice });
            l.pages.push_back (effects);
            return l;
        }

        //==============================================================================
        Layout single (int width, const juce::String& description, std::vector<Section> row)
        {
            Layout l;
            l.width = width;
            l.description = description;
            l.pages.push_back ({ {}, { row } });
            return l;
        }

        Layout equaliser()
        {
            Section curve { "Response" };
            curve.display = D::eq4;
            curve.displayIds = { "Low-pass freq", "Low-pass gain", "Low-pass Q", "Mid freq 1", "Mid gain 1", "Mid Q 1",
                                 "Mid freq 2", "Mid gain 2", "Mid Q 2", "High-pass freq", "High-pass gain", "High-pass Q" };
            curve.displayHeight = 190;
            curve.tip = "Drag a band: frequency and gain. Scroll on it: width (Q). Double-click: back to 0 dB.";

            const auto band = [] (const char* title, const char* f, const char* g, const char* q)
            {
                Section s { title };
                s.items = { knob (f, "Freq"), knob (g, "Gain"), small (q, "Q") };
                s.columns = 3;
                return s;
            };

            Layout l;
            l.width = 820;
            l.description = "Four bands: a low shelf, two bells and a high shelf";
            l.settings.push_back (onOff ("phaseInvert", "Phase invert"));
            Section options { "Output" };
            options.items = { toggle ("phaseInvert", "Phase invert") };
            options.columns = 1;
            options.weight = 0.6f;
            l.pages.push_back ({ {}, { { curve },
                                       { band ("Low shelf", "Low-pass freq", "Low-pass gain", "Low-pass Q"),
                                         band ("Mid 1", "Mid freq 1", "Mid gain 1", "Mid Q 1"),
                                         band ("Mid 2", "Mid freq 2", "Mid gain 2", "Mid Q 2"),
                                         band ("High shelf", "High-pass freq", "High-pass gain", "High-pass Q"),
                                         options } } });
            return l;
        }

        Layout compressor()
        {
            Section curve { "Curve" };
            curve.display = D::compressor;
            curve.displayIds = { "threshold", "ratio", "output gain" };
            curve.displayHeight = 180;
            curve.weight = 0.9f;

            Section controls { "Compressor" };
            controls.items = { big ("threshold", "Threshold"), big ("ratio", "Ratio"), knob ("attack", "Attack"), knob ("release", "Release"),
                               knob ("output gain", "Make-up"), knob ("input gain", "Sidechain gain"), toggle ("sidechainTrigger", "Sidechain") };
            controls.columns = 3;
            controls.weight = 1.1f;

            auto l = single (720, "Evens out levels: loud parts are turned down above the threshold", { curve, controls });
            l.settings.push_back (onOff ("sidechainTrigger", "Sidechain"));
            return l;
        }

        Layout reverb()
        {
            Section tail { "Tail" };
            tail.display = D::reverb;
            tail.displayIds = { "room size", "damping", "wet level", "width", "mode" };
            tail.displayHeight = 110;
            tail.items = { big ("room size", "Size"), knob ("damping", "Damping"), knob ("width", "Width"),
                           knob ("wet level", "Wet"), knob ("dry level", "Dry"), toggle ("mode", "Freeze") };
            tail.columns = 5;
            return single (600, "A room around the sound: size, darkness and how much", { tail });
        }

        Layout delay()
        {
            Section echoes { "Echoes" };
            echoes.display = D::delay;
            echoes.displayIds = { "length", "feedback", "mix proportion" };
            echoes.displayHeight = 100;
            echoes.items = { big ("length", "Time"), knob ("feedback", "Feedback"), knob ("mix proportion", "Mix") };
            echoes.columns = 3;

            Section sync { "Sync to tempo" };
            sync.custom = "delaySync";
            sync.weight = 0.55f;
            sync.tip = "Sets the time to a note length at the song's tempo";

            auto l = single (640, "Repeats of the sound: time, how many, how loud", { echoes, sync });
            l.settings.push_back (number ("length", "Time", 1.0f, 2000.0f, 150.0f, "ms", 0, 300.0f));
            return l;
        }

        Layout chorus()
        {
            Section s { "Chorus" };
            s.display = D::wobble;
            s.displayIds = { "speedHz", "depthMs" };
            s.items = { knob ("speedHz", "Rate"), knob ("depthMs", "Depth"), knob ("width", "Width"), big ("mixProportion", "Mix") };
            auto l = single (480, "Doubles the sound with a slowly moving copy: wider and richer", { s });
            l.settings = { number ("depthMs", "Depth", 0.1f, 20.0f, 3.0f, "ms", 1, 4.0f), number ("speedHz", "Rate", 0.1f, 10.0f, 1.0f, "Hz", 2, 1.0f),
                           number ("width", "Width", 0.0f, 1.0f, 0.5f, "%", 0, 0.0f, 100.0f), number ("mixProportion", "Mix", 0.0f, 1.0f, 0.5f, "%", 0, 0.0f, 100.0f) };
            return l;
        }

        Layout phaser()
        {
            Section s { "Phaser" };
            s.display = D::wobble;
            s.displayIds = { "rate", "depth" };
            s.items = { knob ("rate", "Rate"), knob ("depth", "Depth"), big ("feedback", "Feedback") };
            s.columns = 3;
            auto l = single (420, "A sweeping, whooshing filter", { s });
            l.settings = { number ("depth", "Depth", 1.0f, 10.0f, 5.0f, "", 1), number ("rate", "Rate", 0.05f, 10.0f, 0.4f, "Hz", 2, 1.0f),
                           number ("feedback", "Feedback", 0.0f, 0.99f, 0.7f, "%", 0, 0.0f, 100.0f) };
            return l;
        }

        Layout lowPass()
        {
            Section s { "Filter" };
            s.display = D::lowpass;
            s.displayIds = { "frequency", "mode" };
            s.displayHeight = 100;
            s.items = { buttons ("mode", "Type"), big ("frequency", "Cut-off") };
            s.columns = 2;
            auto l = single (460, "Removes the top (low-pass) or the bottom (high-pass) of the sound", { s });
            auto mode = choice ("mode", "Type", { "Low-pass", "High-pass" }, 0.0f);
            mode.textValues = { "lowpass", "highpass" };
            l.settings.push_back (mode);
            return l;
        }

        Layout ours (const juce::String& type)
        {
            const auto section = [] (const char* title, D display, juce::StringArray ids, std::vector<Item> items, int columns = 4, int height = 110)
            {
                Section s { title };
                s.display = display;
                s.displayIds = std::move (ids);
                s.displayHeight = height;
                s.items = std::move (items);
                s.columns = columns;
                return s;
            };

            if (type == GatePlugin::xmlTypeName)
                return single (560, "Silences the noise between phrases", { section ("Gate", D::gate, { "threshold", "floor" },
                    { big ("threshold", "Threshold"), knob ("floor", "Floor"), small ("attack", "Attack"), small ("hold", "Hold"), small ("release", "Release") }, 5, 130) });

            if (type == DeEsserPlugin::xmlTypeName)
                return single (480, "Tames harsh \"s\" and \"t\" sounds", { section ("De-esser", D::deEsser, { "frequency", "threshold", "most" },
                    { knob ("frequency", "Frequency"), big ("threshold", "Threshold"), knob ("most", "Max cut") }, 3) });

            if (type == LimiterPlugin::xmlTypeName)
                return single (480, "Nothing goes over the ceiling: louder without clipping", { section ("Limiter", D::limiter, { "input", "ceiling" },
                    { big ("input", "Drive"), big ("ceiling", "Ceiling"), knob ("release", "Release") }, 3, 130) });

            if (type == CutFilterPlugin::xmlTypeName)
                return single (480, "Removes rumble below and/or hiss above", { section ("Low and high cut", D::cutFilter, { "lowcut", "highcut" },
                    { big ("lowcut", "Low cut"), big ("highcut", "High cut") }, 2) });

            if (type == SaturatorPlugin::xmlTypeName)
                return single (560, "Warmth and grit: tape, tube or hard clipping", { section ("Saturator", D::saturator, { "drive", "curve", "mix" },
                    { buttons ("curve", "Type"), big ("drive", "Drive"), knob ("tone", "Tone"), knob ("mix", "Mix"), knob ("output", "Output") }, 4, 120) });

            if (type == TunePlugin::xmlTypeName)
                return single (480, "Pulls notes to the key: subtle (slow) or robotic (fast)", { section ("Tune", D::keys, { "root", "scale" },
                    { menu ("root", "Key"), buttons ("scale", "Scale"), big ("amount", "Amount"), knob ("speed", "Speed") }, 2, 70) });

            if (type == TremoloPlugin::xmlTypeName)
                return single (440, "Moves the level (tremolo) or the sound left and right (pan)", { section ("Tremolo", D::wobble, { "rate", "depth", "mode" },
                    { buttons ("mode", "Mode"), big ("rate", "Rate"), big ("depth", "Depth") }, 2, 70) });

            if (type == UtilityPlugin::xmlTypeName)
                return single (480, "Gain, stereo width, mono and phase", { section ("Utility", D::stereo, { "width", "mono", "flipl", "flipr" },
                    { big ("gain", "Gain"), big ("width", "Width"), toggle ("mono", "Mono"), toggle ("flipl", "Flip L"), toggle ("flipr", "Flip R") }, 2, 120) });

            if (type == AmpPlugin::xmlTypeName)
            {
                Section amp { "Amp" };
                amp.items = { buttons ("mode", "Amp"), big ("gain", "Gain"), knob ("volume", "Volume"), knob ("blend", "Clean blend") };
                amp.columns = 3;
                amp.weight = 1.2f;

                auto tone = section ("Tone", D::ampTone, { "tight", "bass", "mid", "treble", "presence" },
                                     { small ("tight", "Tight"), small ("bass", "Bass"), small ("mid", "Mid"), small ("treble", "Treble"), small ("presence", "Presence") }, 5, 90);
                tone.weight = 1.3f;

                Section cab { "Cabinet & gate" };
                cab.items = { menu ("cab", "Cabinet"), knob ("gate", "Gate") };
                cab.columns = 1;
                cab.weight = 0.7f;
                return single (980, "A guitar or bass amp and cabinet, clean to metal", { amp, tone, cab });
            }

            return {};
        }
    }

    //==============================================================================
    std::optional<Layout> forPlugin (te::Plugin& plugin)
    {
        const auto type = plugin.getPluginType();

        if (type == te::FourOscPlugin::xmlTypeName)     return fourOsc();
        if (type == te::EqualiserPlugin::xmlTypeName)   return equaliser();
        if (type == te::CompressorPlugin::xmlTypeName)  return compressor();
        if (type == te::ReverbPlugin::xmlTypeName)      return reverb();
        if (type == te::DelayPlugin::xmlTypeName)       return delay();
        if (type == te::ChorusPlugin::xmlTypeName)      return chorus();
        if (type == te::PhaserPlugin::xmlTypeName)      return phaser();
        if (type == te::LowPassPlugin::xmlTypeName)     return lowPass();

        if (auto l = ours (type); ! l.pages.empty())
            return l;

        return std::nullopt;
    }

    juce::StringArray settingIds (const Layout& layout)
    {
        juce::StringArray ids;

        for (const auto& s : layout.settings)
            ids.add (s.id);

        return ids;
    }

    juce::String tidyValueText (const juce::String& raw)
    {
        auto text = raw.trim();

        // Pan: Tracktion writes "0R" for centre and "34L"/"12R" for the sides
        if (text.matchesWildcard ("*L", true) || text.matchesWildcard ("*R", true))
        {
            const auto amount = text.dropLastCharacters (1);
            if (amount.containsOnly ("0123456789.-") && amount.isNotEmpty())
                return amount.getFloatValue() == 0.0f ? juce::String ("C") : juce::String (juce::roundToInt (amount.getFloatValue())) + text.getLastCharacters (1);
        }

        // split into a leading number and a unit
        int i = 0;
        if (i < text.length() && (text[i] == '-' || text[i] == '+'))
            ++i;
        const int digitsStart = i;
        while (i < text.length() && (juce::CharacterFunctions::isDigit (text[i]) || text[i] == '.'))
            ++i;

        if (i == digitsStart)
            return text;

        auto value = text.substring (0, i).getDoubleValue();
        auto unit = text.substring (i).trim();
        const bool signedText = text.startsWithChar ('+');

        if (unit == "Hz" && std::abs (value) >= 1000.0)
        {
            value /= 1000.0;
            unit = "kHz";
        }
        else if (unit == "ms" && std::abs (value) >= 1000.0)
        {
            value /= 1000.0;
            unit = "s";
        }

        const auto magnitude = std::abs (value);
        const int decimals = magnitude >= 100.0 ? 0 : magnitude >= 10.0 ? 1 : 2;
        auto number = decimals == 0 ? juce::String (juce::roundToInt (value)) : juce::String (value, decimals);

        if (number.containsChar ('.'))
            number = number.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");

        if (number == "-0")
            number = "0";

        if (signedText && value > 0.0)
            number = "+" + number;

        return unit.isEmpty() ? number : number + " " + unit;
    }
}
