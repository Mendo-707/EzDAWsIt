/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "PluginLayouts.h"
#include "../Engine/ArpeggiatorPlugin.h"
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
        Item hero (const juce::String& id, const juce::String& label)                             { return { id, label, Size::hero, Style::knob }; }
        Item stepper (const juce::String& id, const juce::String& label)                          { return { id, label, Size::normal, Style::stepper }; }
        Item fader (const juce::String& id, const juce::String& label)                            { return { id, label, Size::normal, Style::fader }; }

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

            using Signal = Section::Signal;

            // Page 1: the four oscillators
            Page oscillators { "Oscillators", {} };
            std::vector<Section> oscRow;

            for (int i = 1; i <= 4; ++i)
            {
                Section s ("Osc " + juce::String (i), n ("waveShape", i));
                s.rememberPower = true;   // off and on again brings the same wave back
                s.display = D::oscWave;
                s.displayIds = { n ("waveShape", i), n ("pulseWidth", i), n ("voices", i), n ("detune", i) };
                s.displayHeight = 110;

                auto level = fader (n ("level", i), "Level");
                level.span = 2;
                auto unison = stepper (n ("voices", i), "Unison");
                unison.span = 2;
                auto detune = small (n ("detune", i), "Detune");
                auto spread = small (n ("spread", i), "Spread");

                for (auto* item : { &detune, &spread })
                {
                    item->enabledBy = n ("voices", i);
                    item->enabledFrom = 2.0f;
                    item->disabledTip = "Set Unison to 2 or more voices first";
                }

                auto width = small (n ("pulseWidth", i), "Width");
                width.enabledBy = n ("waveShape", i);
                width.enabledFrom = 2.0f;   // the square wave only
                width.enabledUpTo = 2.0f;
                width.disabledTip = "Pulse width shapes the square wave";

                s.items = { level, unison, small (n ("tune", i), "Tune"), small (n ("fineTune", i), "Fine"),
                            small (n ("pan", i), "Pan"), width, detune, spread };
                s.columns = 4;
                s.tip = "Oscillator " + juce::String (i) + ": pick a wave with the arrows on the screen (or click it). Unison stacks detuned copies for a thicker sound.";
                oscRow.push_back (s);
            }

            oscillators.rows.push_back (oscRow);
            l.pages.push_back (oscillators);

            // Page 2: filter and envelopes
            Page shape { "Filter & Amp", {} };
            Section filter ("Filter");
            filter.display = D::synthFilter;
            filter.displayIds = { "filterType", "filterFreq", "filterResonance", "filterSlope", "filterAmount" };
            filter.displayHeight = 150;
            auto type = buttons ("filterType", "Type");
            type.icons = { "off", "lp", "hp", "bp", "notch" };
            type.span = 3;
            auto slope = buttons ("filterSlope", "Slope");
            slope.span = 2;
            filter.items = { type, slope, hero ("filterFreq", "Cut-off"), knob ("filterResonance", "Resonance"), small ("filterAmount", "Env amount"),
                             small ("filterKey", "Key track"), small ("filterVelocity", "Velocity") };
            filter.columns = 5;
            filter.weight = 1.55f;
            filter.tip = "Drag the curve: left and right for the cut-off, up and down for resonance (Shift: fine)";

            Section filterEnv ("Filter envelope");
            filterEnv.signal = Signal::envelope;
            filterEnv.display = D::envelope;
            filterEnv.displayIds = { "filterAttack", "filterDecay", "filterSustain", "filterRelease" };
            filterEnv.displayHeight = 100;
            filterEnv.items = { small ("filterAttack", "Attack"), small ("filterDecay", "Decay"), small ("filterSustain", "Sustain"), small ("filterRelease", "Release") };
            filterEnv.tip = "How the cut-off moves during a note (set Env amount on the filter). Drag the points.";

            Section amp ("Amp envelope");
            amp.signal = Signal::envelope;
            amp.titleToggle = "ampAnalog";
            amp.display = D::envelope;
            amp.displayIds = { "ampAttack", "ampDecay", "ampSustain", "ampRelease" };
            amp.displayHeight = 100;
            amp.items = { small ("ampAttack", "Attack"), small ("ampDecay", "Decay"), small ("ampSustain", "Sustain"), small ("ampRelease", "Release"),
                          small ("ampVelocity", "Velocity") };
            amp.columns = 5;
            amp.weight = 1.15f;
            amp.tip = "The volume of each note over time. Drag the points. Analog: a little drift, like hardware";

            shape.rows.push_back ({ filter, filterEnv, amp });
            l.pages.push_back (shape);

            // Page 3: modulation: the sources on top, where they go underneath
            Page modulation { "Modulation", {} };
            std::vector<Section> sourceRow;

            for (int i = 1; i <= 2; ++i)
            {
                Section lfo ("LFO " + juce::String (i));
                lfo.signal = Signal::modulation;
                lfo.titleToggle = n ("lfoSync", i);
                lfo.display = D::lfo;
                lfo.displayIds = { n ("lfoWaveShape", i), n ("lfoRate", i), n ("lfoDepth", i) };
                lfo.displayHeight = 70;
                auto rate = small (n ("lfoRate", i), "Rate");
                rate.enabledBy = n ("lfoSync", i);
                rate.enabledBelow = true;
                rate.disabledTip = "Synced to the tempo: set the length instead";
                auto length = stepper (n ("lfoBeat", i), "Length");
                length.enabledBy = n ("lfoSync", i);
                length.disabledTip = "Switch Sync on (in the title) to lock it to the tempo";
                length.span = 3;
                lfo.items = { length, rate, small (n ("lfoDepth", i), "Depth") };
                lfo.columns = 3;
                lfo.tip = "A slow wave that moves other controls: route it below. Sync locks it to the song's tempo";
                sourceRow.push_back (lfo);
            }

            for (int i = 1; i <= 2; ++i)
            {
                Section env ("Mod env " + juce::String (i));
                env.signal = Signal::modulation;
                env.display = D::modEnvelope;
                env.displayIds = { n ("modAttack", i), n ("modDecay", i), n ("modSustain", i), n ("modRelease", i) };
                env.displayHeight = 70;
                env.items = { small (n ("modAttack", i), "A"), small (n ("modDecay", i), "D"),
                              small (n ("modSustain", i), "S"), small (n ("modRelease", i), "R") };
                env.tip = "An envelope that moves other controls: route it below";
                sourceRow.push_back (env);
            }

            Section matrix ("Routings");
            matrix.signal = Signal::modulation;
            matrix.custom = "modMatrix";
            matrix.tip = "Route an LFO, an envelope, velocity or the mod wheel to any control";
            modulation.rows.push_back (sourceRow);
            modulation.rows.push_back ({ matrix });
            l.pages.push_back (modulation);

            // Page 4: effects in signal order, then the voice strip
            Page effects { "Effects", {} };
            Section dist ("Distortion", "distortionOn");
            dist.display = D::softClip;
            dist.displayIds = { "distortion" };
            dist.displayHeight = 64;
            dist.items = { knob ("distortion", "Drive") };
            dist.columns = 1;
            dist.chainNext = true;

            Section chorus ("Chorus", "chorusOn");
            chorus.display = D::wobble;
            chorus.displayIds = { "chorusSpeed", "chorusDepth" };
            chorus.displayHeight = 64;
            chorus.items = { small ("chorusSpeed", "Rate"), small ("chorusDepth", "Depth"), small ("chorusWidth", "Width"), small ("chorusMix", "Mix") };
            chorus.chainNext = true;

            Section delay ("Delay", "delayOn");
            delay.display = D::synthDelay;
            delay.displayIds = { "delay", "delayFeedback", "delayMix", "delayCrossfeed" };
            delay.displayHeight = 64;
            auto pingPong = toggle ("delayCrossfeed", "Ping-pong");
            pingPong.span = 2;
            auto time = menu ("delay", "Time");
            time.span = 2;
            delay.items = { time, pingPong, small ("delayFeedback", "Feedback"), small ("delayMix", "Mix") };
            delay.columns = 4;
            delay.chainNext = true;

            Section reverb ("Reverb", "reverbOn");
            reverb.display = D::reverb;
            reverb.displayIds = { "reverbSize", "reverbDamping", "reverbMix", "reverbWidth" };
            reverb.displayHeight = 64;
            reverb.items = { small ("reverbSize", "Size"), small ("reverbDamping", "Damping"), small ("reverbWidth", "Width"), small ("reverbMix", "Mix") };

            Section voice ("Voice");
            auto mode = buttons ("voiceMode", "Mode");
            mode.span = 3;
            auto polyphony = stepper ("voices", "Polyphony");
            polyphony.span = 2;
            polyphony.enabledBy = "voiceMode";
            polyphony.enabledFrom = 2.0f;
            polyphony.disabledTip = "Mono and Legato play one note at a time";
            auto level = fader ("masterLevel", "Level");
            level.span = 4;
            level.unit = "dB";
            mode.span = 3;
            auto glide = fader ("legato", "Glide");
            glide.span = 3;
            glide.enabledBy = "voiceMode";
            glide.enabledFrom = 2.0f;
            glide.enabledBelow = true;
            glide.disabledTip = "Glide works in Mono and Legato";
            voice.items = { mode, polyphony, glide, level };
            voice.columns = 12;
            voice.tip = "Mono and Legato play one note at a time; Glide slides between overlapping notes";

            effects.rows.push_back ({ dist, chorus, delay, reverb });
            effects.rows.push_back ({ voice });
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
            // One big screen (it is the card), the bands as a quiet strip underneath
            Section curve ("Response");
            curve.bare = true;
            curve.display = D::eq4;
            curve.displayIds = { "Low-pass freq", "Low-pass gain", "Low-pass Q", "Mid freq 1", "Mid gain 1", "Mid Q 1",
                                 "Mid freq 2", "Mid gain 2", "Mid Q 2", "High-pass freq", "High-pass gain", "High-pass Q" };
            curve.displayHeight = 210;
            curve.tip = "Drag a band: frequency and gain (Shift: fine). Scroll on it or Alt-drag: width (Q). Double-click: back to 0 dB.";

            int index = 0;
            const auto band = [&index] (const char* title, const char* f, const char* g, const char* q)
            {
                Section s (title);
                s.band = index++;
                s.items = { small (f, "Freq"), small (g, "Gain"), small (q, "Q") };
                s.columns = 3;
                return s;
            };

            Layout l;
            l.width = 820;
            l.description = "Four bands: a low shelf, two bells and a high shelf";
            l.settings.push_back (onOff ("phaseInvert", "Phase invert"));
            l.headerToggles = { "phaseInvert" };
            l.planned = { "Analyzer: the sound's spectrum behind the curve", "A power switch per band" };
            l.pages.push_back ({ {}, { { curve },
                                       { band ("Low shelf", "Low-pass freq", "Low-pass gain", "Low-pass Q"),
                                         band ("Mid 1", "Mid freq 1", "Mid gain 1", "Mid Q 1"),
                                         band ("Mid 2", "Mid freq 2", "Mid gain 2", "Mid Q 2"),
                                         band ("High shelf", "High-pass freq", "High-pass gain", "High-pass Q") } } });
            return l;
        }

        Layout compressor()
        {
            Section curve ("Curve");
            curve.signal = Section::Signal::dynamics;
            curve.display = D::compressor;
            curve.displayIds = { "threshold", "ratio", "output gain" };
            curve.displayHeight = 230;
            curve.weight = 1.0f;
            curve.tip = "Drag the amber point: threshold. Alt-drag: ratio";

            Section controls ("Compressor");
            controls.signal = Section::Signal::dynamics;
            // a compact sidechain strip along the bottom: the switch and its gain on one row
            auto sidechain = toggle ("sidechainTrigger", "Sidechain");
            sidechain.span = 1;
            auto sidechainGain = fader ("input gain", "Sidechain gain");
            sidechainGain.span = 2;
            sidechainGain.enabledBy = "sidechainTrigger";
            sidechainGain.disabledTip = "Switch Sidechain on: another track's sound then sets when it compresses (set it up from the track's menu)";
            controls.items = { big ("threshold", "Threshold"), big ("ratio", "Ratio"), knob ("attack", "Attack"), knob ("release", "Release"),
                               knob ("output gain", "Make-up"), sidechain, sidechainGain };
            controls.columns = 3;
            controls.knobsFirst = true;
            controls.weight = 1.1f;
            curve.weight = 0.9f;

            auto l = single (760, "Evens out levels: loud parts are turned down above the threshold", { curve, controls });
            l.settings.push_back (onOff ("sidechainTrigger", "Sidechain"));
            l.planned = { "Gain reduction meter and a live level ball (needs a level tap in the engine's compressor)",
                          "Auto make-up", "Pick the sidechain source here" };
            return l;
        }

        Layout reverb()
        {
            Section tail ("Tail");
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
            Section echoes ("Echoes");
            echoes.display = D::delay;
            echoes.displayIds = { "length", "feedback", "mix proportion" };
            echoes.displayHeight = 130;
            echoes.items = { big ("length", "Time"), knob ("feedback", "Feedback"), knob ("mix proportion", "Mix") };
            echoes.columns = 3;
            echoes.weight = 1.5f;
            echoes.tip = "Drag the first echo: time (snaps to notes; Alt: free) and feedback (Shift: fine)";

            Section sync ("Note length");
            sync.custom = "delaySync";
            sync.tip = "Sets the time to a note length at the song's tempo";

            auto l = single (700, "Repeats of the sound: time, how many, how loud", { echoes, sync });
            l.settings.push_back (number ("length", "Time", 1.0f, 2000.0f, 150.0f, "ms", 0, 300.0f));
            l.planned = { "Ping-pong and filters on the repeats (the engine's delay has neither)", "Follow tempo changes" };
            return l;
        }

        Layout chorus()
        {
            Section s ("Chorus");
            s.display = D::wobble;
            s.displayIds = { "speedHz", "depthMs" };
            s.items = { knob ("speedHz", "Rate"), knob ("depthMs", "Depth"), knob ("width", "Width"), big ("mixProportion", "Mix") };
            auto l = single (520, "Doubles the sound with a slowly moving copy: wider and richer", { s });
            l.settings = { number ("depthMs", "Depth", 0.1f, 20.0f, 3.0f, "ms", 1, 4.0f), number ("speedHz", "Rate", 0.1f, 10.0f, 1.0f, "Hz", 2, 1.0f),
                           number ("width", "Width", 0.0f, 1.0f, 0.5f, "%", 0, 0.0f, 100.0f), number ("mixProportion", "Mix", 0.0f, 1.0f, 0.5f, "%", 0, 0.0f, 100.0f) };
            return l;
        }

        Layout phaser()
        {
            Section s ("Phaser");
            s.display = D::wobble;
            s.displayIds = { "rate", "depth" };
            s.items = { knob ("rate", "Rate"), knob ("depth", "Depth"), big ("feedback", "Feedback") };
            s.columns = 3;
            auto l = single (480, "A sweeping, whooshing filter", { s });
            l.settings = { number ("depth", "Depth", 1.0f, 10.0f, 5.0f, "", 1), number ("rate", "Rate", 0.05f, 10.0f, 0.4f, "Hz", 2, 1.0f),
                           number ("feedback", "Feedback", 0.0f, 0.99f, 0.7f, "%", 0, 0.0f, 100.0f) };
            return l;
        }

        Layout lowPass()
        {
            Section s ("Filter");
            s.display = D::lowpass;
            s.displayIds = { "frequency", "mode" };
            s.displayHeight = 100;
            s.items = { buttons ("mode", "Type"), big ("frequency", "Cut-off") };
            s.columns = 2;
            auto l = single (500, "Removes the top (low-pass) or the bottom (high-pass) of the sound", { s });
            auto mode = choice ("mode", "Type", { "Low-pass", "High-pass" }, 0.0f);
            mode.textValues = { "lowpass", "highpass" };
            l.settings.push_back (mode);
            return l;
        }

        Layout ours (const juce::String& type)
        {
            const auto section = [] (const char* title, D display, juce::StringArray ids, std::vector<Item> items, int columns = 4, int height = 110)
            {
                Section s (title);
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
                return single (520, "Tames harsh \"s\" and \"t\" sounds", { section ("De-esser", D::deEsser, { "frequency", "threshold", "most" },
                    { knob ("frequency", "Frequency"), big ("threshold", "Threshold"), knob ("most", "Max cut") }, 3) });

            if (type == LimiterPlugin::xmlTypeName)
                return single (540, "Nothing goes over the ceiling: louder without clipping", { section ("Limiter", D::limiter, { "input", "ceiling" },
                    { big ("input", "Drive"), big ("ceiling", "Ceiling"), knob ("release", "Release") }, 3, 130) });

            if (type == CutFilterPlugin::xmlTypeName)
                return single (520, "Removes rumble below and/or hiss above", { section ("Low and high cut", D::cutFilter, { "lowcut", "highcut" },
                    { big ("lowcut", "Low cut"), big ("highcut", "High cut") }, 2) });

            if (type == SaturatorPlugin::xmlTypeName)
                return single (560, "Warmth and grit: tape, tube or hard clipping", { section ("Saturator", D::saturator, { "drive", "curve", "mix" },
                    { buttons ("curve", "Type"), big ("drive", "Drive"), knob ("tone", "Tone"), knob ("mix", "Mix"), knob ("output", "Output") }, 4, 120) });

            if (type == TunePlugin::xmlTypeName)
                return single (520, "Pulls notes to the key: subtle (slow) or robotic (fast)", { section ("Tune", D::keys, { "root", "scale" },
                    { menu ("root", "Key"), buttons ("scale", "Scale"), big ("amount", "Amount"), knob ("speed", "Speed") }, 2, 70) });

            if (type == TremoloPlugin::xmlTypeName)
                return single (480, "Moves the level (tremolo) or the sound left and right (pan)", { section ("Tremolo", D::wobble, { "rate", "depth", "mode" },
                    { buttons ("mode", "Mode"), big ("rate", "Rate"), big ("depth", "Depth") }, 2, 70) });

            if (type == ArpeggiatorPlugin::xmlTypeName)
            {
                auto pattern = section ("Pattern", D::arpPattern, { "rate", "mode", "octaves", "gate", "velocity" },
                                        { buttons ("mode", "Mode"), menu ("rate", "Rate"), big ("octaves", "Octaves"), big ("gate", "Gate"),
                                          big ("velocity", "Velocity"), toggle ("latch", "Latch") }, 3, 110);
                auto l = single (560, "Plays the notes you hold one at a time, in time with the song", { pattern });
                l.planned = { "Swing", "Step velocities", "Repeat each note" };
                return l;
            }

            if (type == UtilityPlugin::xmlTypeName)
                return single (500, "Gain, stereo width, mono and phase", { section ("Utility", D::stereo, { "width", "mono", "flipl", "flipr" },
                    { big ("gain", "Gain"), big ("width", "Width"), toggle ("mono", "Mono"), toggle ("flipl", "Flip L"), toggle ("flipr", "Flip R") }, 2, 120) });

            if (type == AmpPlugin::xmlTypeName)
            {
                Section amp ("Amp");
                amp.custom = "ampTypes";
                amp.items = { hero ("gain", "Gain"), knob ("volume", "Volume"), knob ("blend", "Clean blend") };
                amp.columns = 3;
                amp.weight = 1.25f;

                auto tone = section ("Tone", D::ampTone, { "tight", "bass", "mid", "treble", "presence" },
                                     { small ("tight", "Tight"), small ("bass", "Bass"), small ("mid", "Mid"), small ("treble", "Treble"), small ("presence", "Presence") }, 5, 130);
                tone.weight = 1.6f;

                Section cab ("Cabinet & gate");
                cab.custom = "cabinet";
                cab.items = { knob ("gate", "Gate") };
                cab.columns = 1;
                cab.weight = 0.9f;
                auto l = single (1000, "A guitar or bass amp and cabinet, clean to metal", { amp, tone, cab });
                l.planned = { "Mic position on the cabinet" };
                return l;
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
