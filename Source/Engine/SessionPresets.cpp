/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Session: effect chains, track presets and song templates (see Session.h).

    Built-ins are written here as recipes (which effects, which settings) and built with the same code
    a user preset goes through, so both behave alike. Saved files are plain XML:

      .ezchain     <EZCHAIN name description>  PLUGIN...  </EZCHAIN>
      .eztrack     <EZTRACK name description colour volume pan> <INSTRUMENT>PLUGIN</INSTRUMENT> <EFFECTS>PLUGIN...</EFFECTS>
      .eztemplate  a song file without clips (and without the reference song), with a description

    Plugin states are saved without their item ids: each use makes new ones, so the same chain can go
    on many tracks.
*/

#include "Session.h"
#include "Effects.h"
#include "VocalChain.h"
#include "Mastering.h"
#include "DrumKits.h"
#include "DrumMachinePlugin.h"
#include "AudioEngine.h"
#include "../Core/AppPaths.h"

namespace ez
{
    namespace
    {
        //==============================================================================
        // Built-in recipes

        struct EffectRecipe
        {
            const char* type;
            std::vector<std::pair<int, float>> values;   // control index, value (our effects)
            int vocalPreset = -1;                        // Vocal Chain: which preset
        };

        struct ChainRecipe
        {
            const char* name;
            const char* description;
            std::vector<EffectRecipe> effects;
        };

        struct TrackRecipe
        {
            const char* name;
            const char* description;
            const char* instrument = nullptr;            // plugin type, or nullptr for an audio track
            int kit = -1;                                // a Drum Machine's kit
            std::vector<EffectRecipe> effects;
            float volumeDb = 0.0f;
            int colour = -1;                             // palette index (-1: the next one)
            std::vector<std::pair<const char*, float>> patch = {};   // the instrument's settings (property, value)
            float pan = 0.0f;
        };

        struct TemplateRecipe
        {
            const char* name;
            const char* description;
            double bpm;
            std::vector<const char*> tracks;             // track preset names, in order
            bool reverbReturn = false;
            int masterPreset = -1;                       // MasteringChainPlugin preset (switches the chain on)
            int target = 0;                              // loudness target index
            std::vector<float> pans = {};                // per track, in order (missing: centre)
            bool inputsInOrder = false;                  // track n records interface input n (a band, miked up)
        };

        // Shortcuts for our effects
        EffectRecipe cut (float lowHz, float highHz)     { return { CutFilterPlugin::xmlTypeName, { { CutFilterPlugin::lowCut, lowHz }, { CutFilterPlugin::highCut, highHz } } }; }
        EffectRecipe saturate (float driveDb, float curve, float mix)
                                                        { return { SaturatorPlugin::xmlTypeName, { { SaturatorPlugin::drive, driveDb }, { SaturatorPlugin::curve, curve }, { SaturatorPlugin::mix, mix } } }; }
        EffectRecipe limit (float inputDb, float ceilingDb) { return { LimiterPlugin::xmlTypeName, { { LimiterPlugin::input, inputDb }, { LimiterPlugin::ceiling, ceilingDb } } }; }
        EffectRecipe vocal (int preset)                 { return { VocalChainPlugin::xmlTypeName, {}, preset }; }
        EffectRecipe plain (const char* type)           { return { type, {} }; }

        struct AmpSettings { int mode; float gain; int cab = 1; float gate = -96, tight = 60, bass = 0, mid = 0, treble = 0, presence = 0, blend = 0; };

        EffectRecipe amp (AmpSettings a)
        {
            return { AmpPlugin::xmlTypeName, { { AmpPlugin::mode, (float) a.mode }, { AmpPlugin::gain, a.gain }, { AmpPlugin::cab, (float) a.cab },
                                               { AmpPlugin::gate, a.gate }, { AmpPlugin::tight, a.tight }, { AmpPlugin::bass, a.bass },
                                               { AmpPlugin::mid, a.mid }, { AmpPlugin::treble, a.treble }, { AmpPlugin::presence, a.presence },
                                               { AmpPlugin::blend, a.blend } } };
        }

        EffectRecipe gate (float thresholdDb, float releaseMs)
        {
            return { GatePlugin::xmlTypeName, { { GatePlugin::threshold, thresholdDb }, { GatePlugin::release, releaseMs } } };
        }

        enum AmpMode { clean, crunch, punk, lead, metal, bassAmp };
        enum Cabinet { noCab, cab4x12, cab2x12, cab1x12, cab8x10 };

        // Four Osc settings: a sliding 808 (mono, glides between overlapping notes) and a Reese (detuned saws, filtered)
        const std::vector<std::pair<const char*, float>> slide808
        {
            { "waveShape1", 1.0f }, { "level1", 0.0f }, { "waveShape2", 0.0f },
            { "ampAttack", 0.001f }, { "ampDecay", 2.5f }, { "ampSustain", 35.0f }, { "ampRelease", 0.3f },
            { "voiceMode", 1.0f }, { "legato", 90.0f }, { "distortionOn", 1.0f }, { "distortion", 0.25f },
            { "filterType", 0.0f }, { "masterLevel", -4.0f }
        };

        const std::vector<std::pair<const char*, float>> reese
        {
            { "waveShape1", 3.0f }, { "voices1", 4.0f }, { "detune1", 0.22f }, { "spread1", 60.0f }, { "level1", -3.0f },
            { "waveShape2", 3.0f }, { "tune2", -12.0f }, { "voices2", 2.0f }, { "detune2", 0.12f }, { "level2", -6.0f },
            { "ampAttack", 0.005f }, { "ampSustain", 100.0f }, { "ampRelease", 0.15f },
            { "filterType", 1.0f }, { "filterFreq", 78.0f }, { "filterResonance", 25.0f },
            { "voiceMode", 1.0f }, { "legato", 40.0f }, { "masterLevel", -8.0f }
        };

        const std::vector<ChainRecipe>& chainRecipes()
        {
            static const std::vector<ChainRecipe> recipes
            {
                { "Vocal: clean & clear",   "Rumble off, esses tamed, even, a little room",
                  { cut (90.0f, 18000.0f), plain (DeEsserPlugin::xmlTypeName), plain (te::CompressorPlugin::xmlTypeName), plain (te::ReverbPlugin::xmlTypeName) } },
                { "Vocal: radio / phone",   "Thin and gritty, like a phone speaker",
                  { cut (400.0f, 3500.0f), saturate (14.0f, 1.0f, 70.0f) } },
                { "Drums: punch & glue",    "Squeezed together, a little warmth, no overs",
                  { plain (te::CompressorPlugin::xmlTypeName), saturate (6.0f, 0.0f, 35.0f), limit (3.0f, -1.0f) } },
                { "Bass: tight & mono",     "Sub cleaned up, centred, a bit of growl",
                  { cut (30.0f, 20000.0f), { UtilityPlugin::xmlTypeName, { { UtilityPlugin::mono, 1.0f } } }, saturate (9.0f, 1.0f, 40.0f), limit (2.0f, -1.0f) } },
                { "Lo-fi",                  "Dull, warm, wobbly: tape left in the sun",
                  { cut (120.0f, 6000.0f), saturate (12.0f, 0.0f, 70.0f), { TremoloPlugin::xmlTypeName, { { TremoloPlugin::rate, 0.6f }, { TremoloPlugin::depth, 12.0f } } } } },
                { "Space: big reverb",      "A big room, lows kept out of it",
                  { cut (200.0f, 12000.0f), plain (te::ReverbPlugin::xmlTypeName) } },
                { "Echo throw",             "Delay in time with the song, then a room",
                  { plain (te::DelayPlugin::xmlTypeName), cut (150.0f, 9000.0f), plain (te::ReverbPlugin::xmlTypeName) } },
                // Added later: new ones go on the end
                { "Guitar: metal rhythm",   "Gated, tight, high gain through a 4x12",
                  { amp ({ metal, 7.0f, cab4x12, -55.0f, 120.0f, 2.0f, -3.0f, 2.0f, 3.0f }), cut (80.0f, 11000.0f) } },
                { "Guitar: punk crunch",    "Loud, open crunch with a bit of bite",
                  { amp ({ punk, 6.0f, cab4x12, -65.0f, 80.0f, 1.0f, 2.0f, 1.0f, 2.0f }), cut (70.0f, 12000.0f) } },
                { "Guitar: clean & shimmer", "A clean combo, a little chorus, a room",
                  { amp ({ clean, 4.0f, cab1x12, -96.0f, 60.0f, 0.0f, 0.0f, 2.0f, 1.0f }), plain (te::ChorusPlugin::xmlTypeName), plain (te::ReverbPlugin::xmlTypeName) } },
                { "Guitar: lead",           "Singing lead with an echo",
                  { amp ({ lead, 7.0f, cab4x12, -70.0f, 90.0f, 0.0f, 3.0f, 1.0f, 2.0f }), plain (te::DelayPlugin::xmlTypeName), plain (te::ReverbPlugin::xmlTypeName) } },
                { "Bass: DI + amp",         "The plain bass under a driven amp, kept even",
                  { amp ({ bassAmp, 4.0f, cab8x10, -96.0f, 40.0f, 2.0f, 0.0f, 0.0f, 0.0f, 40.0f }), plain (te::CompressorPlugin::xmlTypeName) } },
                { "Vocal: scream",          "Screamed and shouted vocals: controlled, gritty, cutting",
                  { cut (120.0f, 14000.0f), plain (te::CompressorPlugin::xmlTypeName), saturate (10.0f, 1.0f, 40.0f), limit (3.0f, -1.0f) } },
                { "Drums: room smash",      "Crushed room sound: big and trashy",
                  { plain (te::CompressorPlugin::xmlTypeName), saturate (12.0f, 1.0f, 55.0f), limit (4.0f, -1.0f) } },
            };

            return recipes;
        }

        const std::vector<TrackRecipe>& trackRecipes()
        {
            static const std::vector<TrackRecipe> recipes
            {
                { "Rap vocal",          "Bright, upfront lead (Vocal Chain)",          nullptr, -1, { vocal (0) }, 0.0f, 3 },
                { "R&B lead vocal",     "Warm and roomy lead (Vocal Chain)",           nullptr, -1, { vocal (1) }, 0.0f, 3 },
                { "Backing vocals",     "Wide group vocals (Vocal Chain)",             nullptr, -1, { vocal (5) }, -4.0f, 4 },
                { "Adlibs",             "Phone-filtered adlibs (Vocal Chain)",         nullptr, -1, { vocal (4) }, -6.0f, 4 },
                { "Podcast voice",      "Speech: clear, even, dry (Vocal Chain)",      nullptr, -1, { vocal (7) }, 0.0f, 3 },
                { "Guitar / acoustic",  "A miked instrument: cleaned up, even, a room", nullptr, -1,
                  { cut (80.0f, 18000.0f), plain (te::CompressorPlugin::xmlTypeName), plain (te::ReverbPlugin::xmlTypeName) }, -2.0f, 2 },
                { "Synth keys",         "Four-oscillator synth with a room",           te::FourOscPlugin::xmlTypeName, -1, { plain (te::ReverbPlugin::xmlTypeName) }, -6.0f, 2 },
                { "808 / sub bass",     "Synth bass: mono, with some growl",           te::FourOscPlugin::xmlTypeName, -1,
                  { { UtilityPlugin::xmlTypeName, { { UtilityPlugin::mono, 1.0f } } }, saturate (9.0f, 1.0f, 45.0f) }, -4.0f, 1 },
                { "Sampler",            "Play any sound across the keys",              te::SamplerPlugin::xmlTypeName, -1, {}, -3.0f, 6 },
                { "Drums: boom bap",    "Drum Machine, Boom Bap Dust kit, glued",      DrumMachinePlugin::xmlTypeName, 0,
                  { plain (te::CompressorPlugin::xmlTypeName), limit (2.0f, -1.0f) }, 0.0f, 0 },
                { "Drums: trap",        "Drum Machine, Trap 808 kit",                  DrumMachinePlugin::xmlTypeName, 1, { limit (2.0f, -1.0f) }, 0.0f, 0 },
                { "Drums: drum & bass", "Drum Machine, Rolling DnB kit",               DrumMachinePlugin::xmlTypeName, 2, { limit (2.0f, -1.0f) }, 0.0f, 0 },
                { "Drums: house",       "Drum Machine, Warehouse House kit",           DrumMachinePlugin::xmlTypeName, 4, { limit (2.0f, -1.0f) }, 0.0f, 0 },
                { "Drums: lo-fi",       "Drum Machine, Tape Kit, dusty",               DrumMachinePlugin::xmlTypeName, 5, { saturate (8.0f, 0.0f, 50.0f), cut (40.0f, 9000.0f) }, 0.0f, 0 },
                // Added later: new ones go on the end
                { "808 slide (trap)",   "Long 808 that slides into overlapping notes", te::FourOscPlugin::xmlTypeName, -1,
                  { { UtilityPlugin::xmlTypeName, { { UtilityPlugin::mono, 1.0f } } }, saturate (8.0f, 1.0f, 40.0f), limit (2.0f, -1.0f) }, -3.0f, 1, slide808 },
                { "Reese bass (DnB)",   "Detuned, growling drum & bass bass",          te::FourOscPlugin::xmlTypeName, -1,
                  { cut (30.0f, 20000.0f), saturate (6.0f, 1.0f, 30.0f) }, -4.0f, 1, reese },
                { "Guitar: clean",      "Clean amp, combo speaker, a room",            nullptr, -1,
                  { amp ({ clean, 4.0f, cab1x12, -96.0f, 60.0f, 0.0f, 0.0f, 2.0f, 1.0f }), plain (te::ReverbPlugin::xmlTypeName) }, -3.0f, 2 },
                { "Guitar: crunch",     "Punk and rock crunch through a 4x12",         nullptr, -1,
                  { amp ({ punk, 6.0f, cab4x12, -65.0f, 80.0f, 1.0f, 2.0f, 1.0f, 2.0f }), cut (70.0f, 12000.0f) }, -3.0f, 2 },
                { "Guitar: metal rhythm", "High gain, gated tight for fast riffs and chugs", nullptr, -1,
                  { amp ({ metal, 7.0f, cab4x12, -55.0f, 120.0f, 2.0f, -3.0f, 2.0f, 3.0f }), cut (80.0f, 11000.0f) }, -4.0f, 2 },
                { "Guitar: lead",       "Singing lead with an echo",                   nullptr, -1,
                  { amp ({ lead, 7.0f, cab4x12, -70.0f, 90.0f, 0.0f, 3.0f, 1.0f, 2.0f }), plain (te::DelayPlugin::xmlTypeName), plain (te::ReverbPlugin::xmlTypeName) }, -4.0f, 5 },
                { "Bass guitar",        "DI under a bass amp, evened out",             nullptr, -1,
                  { amp ({ bassAmp, 4.0f, cab8x10, -96.0f, 40.0f, 2.0f, 0.0f, 0.0f, 0.0f, 40.0f }), plain (te::CompressorPlugin::xmlTypeName) }, -2.0f, 1 },
                { "Bass: hardcore grit", "Driven bass that cuts through loud guitars", nullptr, -1,
                  { amp ({ bassAmp, 7.0f, cab8x10, -70.0f, 40.0f, 1.0f, 3.0f, 2.0f, 2.0f, 30.0f }), limit (3.0f, -1.0f) }, -2.0f, 1 },
                { "Rock vocal",         "Upfront lead that sits over guitars (Vocal Chain)", nullptr, -1,
                  { vocal (0), saturate (5.0f, 0.0f, 25.0f) }, 0.0f, 3 },
                { "Scream vocal",       "Screamed and shouted vocals: controlled, gritty", nullptr, -1,
                  { cut (120.0f, 14000.0f), plain (te::CompressorPlugin::xmlTypeName), saturate (10.0f, 1.0f, 40.0f), limit (3.0f, -1.0f) }, 0.0f, 3 },
                { "Gang vocals",        "Group shouts: wide and roomy (Vocal Chain)",  nullptr, -1, { vocal (5), saturate (6.0f, 1.0f, 30.0f) }, -3.0f, 4 },
                { "Drums: punk",        "Drum Machine, Garage Punk kit",               DrumMachinePlugin::xmlTypeName, 6,
                  { plain (te::CompressorPlugin::xmlTypeName), limit (2.0f, -1.0f) }, 0.0f, 0 },
                { "Drums: metal",       "Drum Machine, Metal Kit",                     DrumMachinePlugin::xmlTypeName, 7,
                  { plain (te::CompressorPlugin::xmlTypeName), limit (2.0f, -1.0f) }, 0.0f, 0 },
                { "Drums: hardcore",    "Drum Machine, Hardcore Kit, crushed",         DrumMachinePlugin::xmlTypeName, 8,
                  { plain (te::CompressorPlugin::xmlTypeName), saturate (6.0f, 1.0f, 30.0f), limit (2.0f, -1.0f) }, 0.0f, 0 },
                { "Drum mic: kick",     "A miked kick: gated, punchy, no boom",        nullptr, -1,
                  { cut (35.0f, 12000.0f), gate (-35.0f, 80.0f), plain (te::CompressorPlugin::xmlTypeName) }, 0.0f, 0 },
                { "Drum mic: snare",    "A miked snare: gated, cracking",              nullptr, -1,
                  { cut (90.0f, 16000.0f), gate (-32.0f, 120.0f), plain (te::CompressorPlugin::xmlTypeName) }, 0.0f, 0 },
                { "Drum mic: toms",     "Miked toms: gated so they ring only when hit", nullptr, -1,
                  { cut (60.0f, 12000.0f), gate (-30.0f, 250.0f) }, -2.0f, 0 },
                { "Drum mic: overheads", "Cymbals and the whole kit from above",       nullptr, -1,
                  { cut (200.0f, 20000.0f), plain (te::CompressorPlugin::xmlTypeName) }, -3.0f, 0 },
                { "Drum mic: room",     "The room around the kit, smashed",            nullptr, -1,
                  { plain (te::CompressorPlugin::xmlTypeName), saturate (12.0f, 1.0f, 55.0f), limit (4.0f, -1.0f) }, -8.0f, 0 },
            };

            return recipes;
        }

        const std::vector<TemplateRecipe>& templateRecipes()
        {
            static const std::vector<TemplateRecipe> recipes
            {
                { "Hip hop beat",       "Drums, 808, keys, rap vocal, backing, adlibs; reverb return",   90.0,
                  { "Drums: boom bap", "808 / sub bass", "Synth keys", "Rap vocal", "Backing vocals", "Adlibs" }, true, 1, 0 },
                { "Trap",               "Trap drums, 808, keys, vocal and adlibs",                     140.0,
                  { "Drums: trap", "808 / sub bass", "Synth keys", "Rap vocal", "Adlibs" }, true, 1, 0 },
                { "Drum & bass",        "Rolling drums, sub bass, keys, vocal",                        174.0,
                  { "Drums: drum & bass", "808 / sub bass", "Synth keys", "R&B lead vocal" }, true, 4, 3 },
                { "Singer-songwriter",  "Vocal, guitar, keys, backing vocals; a room for all",          96.0,
                  { "R&B lead vocal", "Guitar / acoustic", "Synth keys", "Backing vocals" }, true, 0, 0 },
                { "Lo-fi beat",         "Dusty drums, keys, bass, a sampler",                           80.0,
                  { "Drums: lo-fi", "Synth keys", "808 / sub bass", "Sampler" }, true, 2, 0 },
                { "Podcast",            "Two voices, levelled for podcasts",                           120.0,
                  { "Podcast voice", "Podcast voice" }, false, 5, 1 },
                // Added later: new ones go on the end
                { "Punk",               "Fast drums, bass, two crunch guitars left and right, vocals", 180.0,
                  { "Drums: punk", "Bass guitar", "Guitar: crunch", "Guitar: crunch", "Rock vocal", "Gang vocals" }, true, 1, 0,
                  { 0.0f, 0.0f, -0.8f, 0.8f, 0.0f, 0.0f } },
                { "Hardcore",           "Heavy drums, gritty bass, doubled metal guitars, screams and gang vocals", 160.0,
                  { "Drums: hardcore", "Bass: hardcore grit", "Guitar: metal rhythm", "Guitar: metal rhythm", "Scream vocal", "Gang vocals" }, true, 1, 0,
                  { 0.0f, 0.0f, -0.9f, 0.9f, 0.0f, 0.0f } },
                { "Metal",              "Metal drums, bass, rhythm guitars doubled left and right, a lead, two vocals", 120.0,
                  { "Drums: metal", "Bass guitar", "Guitar: metal rhythm", "Guitar: metal rhythm", "Guitar: lead", "Scream vocal", "Rock vocal" }, true, 1, 0,
                  { 0.0f, 0.0f, -1.0f, 1.0f, 0.2f, 0.0f, 0.0f } },
                { "Rock band",          "Drums, bass, crunch and clean guitars, vocal and backing", 120.0,
                  { "Drums: punk", "Bass guitar", "Guitar: crunch", "Guitar: clean", "Rock vocal", "Backing vocals" }, true, 0, 0,
                  { 0.0f, 0.0f, -0.6f, 0.6f, 0.0f, 0.0f } },
                { "Live band tracking", "A track per microphone and instrument, to record a band playing together", 120.0,
                  { "Drum mic: kick", "Drum mic: snare", "Drum mic: toms", "Drum mic: overheads", "Drum mic: room",
                    "Bass guitar", "Guitar: crunch", "Guitar: clean", "Rock vocal" }, true, -1, 0,
                  { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.6f, 0.6f, 0.0f }, true },
                { "Trap (sliding 808)", "Trap drums, a sliding 808, keys, vocals", 140.0,
                  { "Drums: trap", "808 slide (trap)", "Synth keys", "Rap vocal", "Adlibs" }, true, 1, 0 },
                { "Drum & bass (Reese)", "Rolling drums, a Reese bass, keys and a vocal", 174.0,
                  { "Drums: drum & bass", "Reese bass (DnB)", "Synth keys", "R&B lead vocal" }, true, 4, 3 },
            };

            return recipes;
        }

        //==============================================================================
        void stripItemIDs (juce::ValueTree v)
        {
            v.removeProperty (te::IDs::id, nullptr);

            for (auto child : v)
                stripItemIDs (child);
        }

        /** What belongs to the old track, not the sound: automation, modulation links, a sidechain source. */
        void stripTrackLinks (juce::ValueTree v)
        {
            v.removeProperty (te::IDs::sidechainSourceID, nullptr);

            for (int i = v.getNumChildren(); --i >= 0;)
            {
                const auto type = v.getChild (i).getType();

                if (type == te::IDs::AUTOMATIONCURVE || type == te::IDs::MODIFIERASSIGNMENTS)
                    v.removeChild (i, nullptr);
                else
                    stripTrackLinks (v.getChild (i));
            }
        }

        juce::ValueTree capture (te::Plugin& plugin)
        {
            plugin.flushPluginStateToValueTree();
            auto state = plugin.state.createCopy();
            stripItemIDs (state);
            stripTrackLinks (state);
            return state;
        }

        /** Descriptions are read from the files once (templates can be big), again only if the file changes. */
        juce::String readDescription (const juce::File& file)
        {
            static std::map<juce::String, std::pair<juce::Time, juce::String>> cache;
            const auto when = file.getLastModificationTime();

            if (auto found = cache.find (file.getFullPathName()); found != cache.end() && found->second.first == when)
                return found->second.second;

            juce::String description;

            if (auto xml = juce::parseXML (file))
                description = xml->getStringAttribute ("description");

            cache[file.getFullPathName()] = { when, description };
            return description;
        }

        bool writeTree (const juce::ValueTree& tree, const juce::File& file)
        {
            if (auto xml = tree.createXml())
                return file.getParentDirectory().createDirectory().wasOk() && xml->writeTo (file);

            return false;
        }

        juce::ValueTree readTree (const juce::File& file)
        {
            if (auto xml = juce::parseXML (file))
                return juce::ValueTree::fromXml (*xml);

            return {};
        }

        /** Recordings and notes: what a template leaves out. Not MARKERCLIP (sections), CHORDCLIP or ARRANGERCLIP. */
        bool isClip (const juce::ValueTree& v)
        {
            static const juce::StringArray recorded { "AUDIOCLIP", "WAVECLIP", "MIDICLIP", "STEPCLIP", "EDITCLIP", "CONTAINERCLIP" };
            return recorded.contains (v.getType().toString());
        }

        /** A template starts at the top, with nothing armed and no loop of the old song. */
        void resetForTemplate (juce::ValueTree state)
        {
            if (auto transport = state.getChildWithName (te::IDs::TRANSPORT); transport.isValid())
            {
                transport.setProperty (te::IDs::position, 0.0, nullptr);
                transport.removeProperty (te::IDs::loopPoint1, nullptr);
                transport.removeProperty (te::IDs::loopPoint2, nullptr);
            }

            std::function<void (juce::ValueTree)> disarm = [&disarm] (juce::ValueTree v)
            {
                v.removeProperty (te::IDs::armed, nullptr);

                for (auto child : v)
                    disarm (child);
            };

            disarm (state.getChildWithName (te::IDs::INPUTDEVICES));
        }

        void removeClips (juce::ValueTree v)
        {
            for (int i = v.getNumChildren(); --i >= 0;)
            {
                if (isClip (v.getChild (i)))
                    v.removeChild (i, nullptr);
                else
                    removeClips (v.getChild (i));
            }
        }
    }

    //==============================================================================
    juce::File Session::getPresetFolder (PresetKind kind)
    {
        const char* names[] = { "Effect chains", "Track presets", "Song templates" };
        return AppPaths::getRootFolder().getChildFile ("Presets").getChildFile (names[(int) kind]);
    }

    juce::String Session::getPresetExtension (PresetKind kind)
    {
        const char* extensions[] = { ".ezchain", ".eztrack", ".eztemplate" };
        return extensions[(int) kind];
    }

    juce::Array<Session::PresetInfo> Session::getPresets (PresetKind kind) const
    {
        juce::Array<PresetInfo> list;

        const auto addBuiltIns = [&list, kind] (const auto& recipes)
        {
            for (int i = 0; i < (int) recipes.size(); ++i)
                list.add ({ kind, recipes[(size_t) i].name, recipes[(size_t) i].description, {}, i });
        };

        switch (kind)
        {
            case PresetKind::effectChain:   addBuiltIns (chainRecipes()); break;
            case PresetKind::trackPreset:   addBuiltIns (trackRecipes()); break;
            case PresetKind::songTemplate:  addBuiltIns (templateRecipes()); break;
        }

        auto files = getPresetFolder (kind).findChildFiles (juce::File::findFiles, false, "*" + getPresetExtension (kind));
        files.sort();

        for (const auto& f : files)
            list.add ({ kind, f.getFileNameWithoutExtension(), readDescription (f), f, -1 });

        return list;
    }

    bool Session::deletePreset (const PresetInfo& preset)
    {
        if (preset.isBuiltIn())
            return fail ("Built-in presets can't be deleted");

        if (! preset.file.existsAsFile() || ! (preset.file.moveToTrash() || preset.file.deleteFile()))
            return fail ("Could not delete " + preset.file.getFullPathName());

        presetBroadcaster.sendChangeMessage();
        return true;
    }

    bool Session::presetExists (PresetKind kind, const juce::String& name) const
    {
        return getPresetFolder (kind).getChildFile (juce::File::createLegalFileName (name.trim()) + getPresetExtension (kind)).existsAsFile();
    }

    //==============================================================================
    int Session::effectInsertIndex (te::AudioTrack& track) const
    {
        for (int i = 0; i < track.pluginList.size(); ++i)
        {
            auto* p = track.pluginList[i];

            if (dynamic_cast<te::AuxSendPlugin*> (p) != nullptr || p == track.getVolumePlugin())
                return i;
        }

        return -1;
    }

    te::Plugin::Ptr Session::insertPluginState (te::AudioTrack& track, const juce::ValueTree& state, int index,
                                                juce::StringArray& missing, juce::StringArray& unknown, bool& full)
    {
        if (track.pluginList.size() >= edit->engine.getEngineBehaviour().getEditLimits().maxPluginsOnTrack)
        {
            full = true;
            return {};
        }

        auto copy = state.createCopy();
        stripItemIDs (copy);   // new ids every time (the same chain can go on many tracks)
        auto plugin = edit->getPluginCache().createNewPlugin (copy);

        if (plugin == nullptr)
        {
            unknown.add (state.getProperty (te::IDs::name, state.getProperty (te::IDs::type)).toString());
            return {};
        }

        const auto before = track.pluginList.size();
        track.pluginList.insertPlugin (plugin, index, nullptr);

        if (track.pluginList.size() != before + 1)
        {
            full = true;
            return {};
        }

        if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get()); external != nullptr && external->getAudioPluginInstance() == nullptr)
            missing.add (external->getName());   // kept (its settings come back once it's installed)

        return plugin;
    }

    namespace
    {
        /** A built-in effect recipe as a plugin state (made, set, captured). */
        juce::ValueTree buildEffect (te::Edit& edit, const EffectRecipe& recipe, double bpm)
        {
            auto plugin = edit.getPluginCache().createNewPlugin (recipe.type, {});

            if (plugin == nullptr)
                return {};

            if (auto* delay = dynamic_cast<te::DelayPlugin*> (plugin.get()))   // in time with the song: a dotted 1/8
                delay->lengthMs = juce::jlimit (60, 2000, juce::roundToInt (60000.0 / juce::jmax (20.0, bpm) * 0.75));

            if (auto* effect = dynamic_cast<EffectPlugin*> (plugin.get()))
                for (auto [control, value] : recipe.values)
                    effect->set (control, value);

            if (auto* chain = dynamic_cast<VocalChainPlugin*> (plugin.get()); chain != nullptr
                 && juce::isPositiveAndBelow (recipe.vocalPreset, (int) VocalChainPlugin::getPresets().size()))
            {
                const auto& preset = VocalChainPlugin::getPresets()[(size_t) recipe.vocalPreset];

                for (int m = 0; m < VocalChainPlugin::numMacros; ++m)
                    chain->set (VocalChainPlugin::firstMacro + m, preset.macros[m]);

                for (int m = 0; m < VocalChainPlugin::numModules; ++m)
                    chain->set (VocalChainPlugin::firstModule + m, (preset.modulesOff & (1 << m)) != 0 ? 0.0f : 1.0f);

                chain->set (VocalChainPlugin::phone, preset.phone ? 1.0f : 0.0f);
                chain->setPresetName (preset.name);
            }

            return capture (*plugin);
        }
    }

    //==============================================================================
    bool Session::saveEffectChain (int trackIndex, const juce::String& name)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        const auto effects = getEffects (trackIndex);
        const auto legal = juce::File::createLegalFileName (name.trim());

        if (legal.isEmpty())
            return fail ("Give the chain a name");

        if (effects.isEmpty())
            return fail ("This track has no effects to save");

        juce::ValueTree tree ("EZCHAIN");
        tree.setProperty ("description", juce::String (effects.size()) + " effects from \"" + getTrackName (trackIndex) + "\"", nullptr);

        for (auto* e : effects)
            tree.appendChild (capture (*e), nullptr);

        if (! writeTree (tree, getPresetFolder (PresetKind::effectChain).getChildFile (legal + getPresetExtension (PresetKind::effectChain))))
            return fail ("Could not write the chain to " + getPresetFolder (PresetKind::effectChain).getFullPathName());

        presetBroadcaster.sendChangeMessage();
        return true;
    }

    bool Session::applyEffectChain (int trackIndex, const PresetInfo& preset)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return fail ("Click a track first");

        if (refuseWhileRecording ("adding effects"))
            return false;

        if (preset.isBuiltIn() ? ! juce::isPositiveAndBelow (preset.builtIn, (int) chainRecipes().size())
                               : ! readTree (preset.file).hasType ("EZCHAIN"))
            return fail (preset.isBuiltIn() ? juce::String ("No such chain")
                                             : "This isn't an effect chain, or it's damaged: " + preset.file.getFileName());

        // One step from here: even making a built-in's effects (to read their settings) touches the undo history
        beginUndoStep();
        std::vector<juce::ValueTree> states;
        juce::StringArray missing, unknown;

        if (preset.isBuiltIn())
        {
            for (const auto& r : chainRecipes()[(size_t) preset.builtIn].effects)
            {
                if (auto s = buildEffect (*edit, r, getBpm()); s.isValid())
                    states.push_back (s);
                else
                    unknown.add (r.type);
            }
        }
        else
        {
            for (auto child : readTree (preset.file))
                states.push_back (child);
        }

        if (states.empty())
        {
            edit->getUndoManager().undoCurrentTransactionOnly();
            return fail ("The chain is empty");
        }

        int added = 0;
        bool full = false;

        for (const auto& s : states)
        {
            if (insertPluginState (*track, s, effectInsertIndex (*track), missing, unknown, full) != nullptr)
                ++added;

            if (full)
                break;
        }

        notifyStateChanged();

        if (full)
            return fail ("Only " + juce::String (added) + " of the " + juce::String ((int) states.size()) + " effects fitted: the track is full");

        if (! missing.isEmpty() || ! unknown.isEmpty())
            showHint ((missing.isEmpty() ? juce::String() : "Not installed here (added, silent until it is): " + missing.joinIntoString (", ") + ". ")
                      + (unknown.isEmpty() ? juce::String() : "Left out (unknown here): " + unknown.joinIntoString (", ")));

        return true;
    }

    //==============================================================================
    bool Session::saveTrackPreset (int trackIndex, const juce::String& name)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* track = getTrack (trackIndex);
        const auto legal = juce::File::createLegalFileName (name.trim());

        if (track == nullptr)
            return fail ("Click a track first");

        if (legal.isEmpty())
            return fail ("Give the preset a name");

        juce::ValueTree tree ("EZTRACK");
        tree.setProperty ("trackName", track->getName(), nullptr);
        tree.setProperty ("colour", track->getColour().toString(), nullptr);
        tree.setProperty ("volume", getTrackVolumeDb (trackIndex), nullptr);
        tree.setProperty ("pan", getTrackPan (trackIndex), nullptr);

        juce::String instrumentName;

        for (auto* p : track->pluginList)
            if (p->isSynth())
            {
                juce::ValueTree instrument ("INSTRUMENT");
                instrument.appendChild (capture (*p), nullptr);
                tree.appendChild (instrument, nullptr);
                instrumentName = p->getName();
                break;
            }

        juce::ValueTree effectsTree ("EFFECTS");

        for (auto* e : getEffects (trackIndex))
            effectsTree.appendChild (capture (*e), nullptr);

        tree.appendChild (effectsTree, nullptr);
        tree.setProperty ("description", (instrumentName.isNotEmpty() ? instrumentName + " + " : juce::String())
                                         + juce::String (effectsTree.getNumChildren()) + " effects", nullptr);

        if (! writeTree (tree, getPresetFolder (PresetKind::trackPreset).getChildFile (legal + getPresetExtension (PresetKind::trackPreset))))
            return fail ("Could not write the preset to " + getPresetFolder (PresetKind::trackPreset).getFullPathName());

        presetBroadcaster.sendChangeMessage();
        return true;
    }

    bool Session::addTrackFromPreset (const PresetInfo& preset)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr)
            return fail ("No song is open");

        if (refuseWhileRecording ("adding a track"))
            return false;

        if (preset.isBuiltIn() ? ! juce::isPositiveAndBelow (preset.builtIn, (int) trackRecipes().size())
                               : ! readTree (preset.file).hasType ("EZTRACK"))
            return fail (preset.isBuiltIn() ? juce::String ("No such preset")
                                             : "This isn't a track preset, or it's damaged: " + preset.file.getFileName());

        // One step from here: even making a built-in's devices (to read their settings) touches the undo history
        beginUndoStep();
        juce::StringArray missing, unknown;
        juce::String trackName;
        juce::ValueTree instrument;
        std::vector<juce::ValueTree> effects;
        float volumeDb = 0.0f, pan = 0.0f;
        juce::Colour colour;
        int kit = -1;

        if (preset.isBuiltIn())
        {
            const auto& r = trackRecipes()[(size_t) preset.builtIn];
            trackName = r.name;
            volumeDb = r.volumeDb;
            pan = r.pan;
            kit = r.kit;

            if (juce::isPositiveAndBelow (r.colour, (int) getTrackPalette().size()))
                colour = getTrackPalette()[(size_t) r.colour].colour;

            if (r.instrument != nullptr)
            {
                if (auto plugin = edit->getPluginCache().createNewPlugin (r.instrument, {}))
                {
                    instrument = capture (*plugin);

                    for (const auto& [property, value] : r.patch)   // the copy, not the plugin: nothing to undo
                        instrument.setProperty (property, value, nullptr);
                }
                else
                {
                    unknown.add (r.instrument);
                }
            }

            for (const auto& e : r.effects)
            {
                if (auto s = buildEffect (*edit, e, getBpm()); s.isValid())
                    effects.push_back (s);
                else
                    unknown.add (e.type);
            }
        }
        else
        {
            const auto tree = readTree (preset.file);

            trackName = tree.getProperty ("trackName", preset.name).toString();
            colour = juce::Colour::fromString (tree.getProperty ("colour").toString());
            volumeDb = (float) tree.getProperty ("volume", 0.0f);
            pan = (float) tree.getProperty ("pan", 0.0f);

            if (auto i = tree.getChildWithName ("INSTRUMENT"); i.getNumChildren() > 0)
                instrument = i.getChild (0);

            for (auto e : tree.getChildWithName ("EFFECTS"))
                effects.push_back (e);
        }

        const auto before = getNumAudioTracks();
        const auto uniqueName = makeUniqueTrackName (trackName);   // two "Podcast voice" tracks: "Podcast voice 2"
        edit->ensureNumberOfAudioTracks (before + 1);
        auto* track = te::getAudioTracks (*edit).getLast();

        if (track == nullptr || getNumAudioTracks() != before + 1)
        {
            edit->getUndoManager().undoCurrentTransactionOnly();
            return fail ("Could not add a track");
        }

        track->setName (uniqueName);

        if (! colour.isTransparent())
            track->setColour (colour);
        else
            colourNewTracks (before);

        if (auto* volume = track->getVolumePlugin())
        {
            volume->setVolumeDb (juce::jlimit (minVolumeDb, maxVolumeDb, volumeDb));
            volume->setPan (juce::jlimit (-1.0f, 1.0f, pan));
        }

        bool full = false;

        if (instrument.isValid())
        {
            if (auto plugin = insertPluginState (*track, instrument, 0, missing, unknown, full))
            {
                routeMidiInputsTo (*track);

                if (auto* drums = dynamic_cast<DrumMachinePlugin*> (plugin.get()); drums != nullptr && kit >= 0)
                    applyDrumKit (*drums, kit);
            }
        }

        for (const auto& e : effects)
            if (! full)
                insertPluginState (*track, e, effectInsertIndex (*track), missing, unknown, full);

        if (! missing.isEmpty() || ! unknown.isEmpty() || full)
            showHint ((missing.isEmpty() ? juce::String() : "Not installed here (added, silent until it is): " + missing.joinIntoString (", ") + ". ")
                      + (unknown.isEmpty() ? juce::String() : "Left out (unknown here): " + unknown.joinIntoString (", ") + ". ")
                      + (full ? juce::String ("Some effects didn't fit on the track.") : juce::String()));

        log ("Added track from preset: " + preset.name);
        notifyStateChanged();
        return true;
    }

    //==============================================================================
    bool Session::saveSongTemplate (const juce::String& name)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        const auto legal = juce::File::createLegalFileName (name.trim());

        if (edit == nullptr)
            return fail ("No song is open");

        if (legal.isEmpty())
            return fail ("Give the template a name");

        edit->flushState();   // plugins write their settings into the song first
        auto state = edit->state.createCopy();
        removeClips (state);
        resetForTemplate (state);

        // Not part of a template: the reference song (it's yours to pick each time)
        for (auto masters = state.getChildWithName (te::IDs::MASTERPLUGINS); auto p : masters)
            p.removeProperty ("ezRefFile", nullptr);

        int tracks = 0;

        for (auto* t : te::getAudioTracks (*edit))
            if (t != nullptr)
                ++tracks;

        state.setProperty ("ezTemplateDescription", juce::String (tracks) + " tracks, " + juce::String (getBpm(), 0) + " BPM", nullptr);

        // The description is read from the top element by getPresets
        auto file = getPresetFolder (PresetKind::songTemplate).getChildFile (legal + getPresetExtension (PresetKind::songTemplate));
        auto tree = state;
        tree.setProperty ("description", state.getProperty ("ezTemplateDescription"), nullptr);

        if (! writeTree (tree, file))
            return fail ("Could not write the template to " + file.getParentDirectory().getFullPathName());

        presetBroadcaster.sendChangeMessage();
        return true;
    }

    bool Session::createSongFromTemplate (const juce::String& songName, const PresetInfo& preset)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (preset.isBuiltIn())
        {
            if (! juce::isPositiveAndBelow (preset.builtIn, (int) templateRecipes().size()))
                return fail ("No such template");

            const auto& recipe = templateRecipes()[(size_t) preset.builtIn];

            if (! createSong (songName))
                return false;

            // Built on the new (empty-ish) song, then made the song's starting point (no undo back to empty)
            for (auto* t : te::getAudioTracks (*edit))
                edit->deleteTrack (t);

            setBpm (recipe.bpm);
            const auto tracks = getPresets (PresetKind::trackPreset);

            juce::StringArray failed;

            for (size_t i = 0; i < recipe.tracks.size(); ++i)
            {
                const auto* name = recipe.tracks[i];

                for (const auto& p : tracks)
                {
                    if (! p.isBuiltIn() || p.name != name)
                        continue;

                    if (! addTrackFromPreset (p))
                        failed.add (juce::String (name) + ": " + lastError);
                    else if (i < recipe.pans.size() && recipe.pans[i] != 0.0f)
                        setTrackPan (getNumAudioTracks() - 1, recipe.pans[i]);   // doubled guitars, left and right

                    break;
                }
            }

            if (recipe.inputsInOrder)
            {
                // Kick mic on input 1, snare on 2 ... (needs one input per channel: a setting for every song,
                // so it's never changed from here)
                if (audioEngine.areInputsMono())
                {
                    const auto inputs = audioEngine.getAudioInputs();

                    for (int t = 0; t < getNumAudioTracks() && t < inputs.size(); ++t)
                        setTrackInput (t, inputs[t]->getDeviceID());

                    showHint ("Each track records its own input (1, 2, 3 ...). Change them in Setup > Routing; check levels in Setup > Recording.");
                }
                else
                {
                    showHint ("To record each mic on its own track: Setup > Recording > \"One input per channel\", then pick inputs in Routing.");
                }
            }

            if (recipe.reverbReturn)
                addReturnTrack (ReturnEffect::reverb, "Reverb");

            if (recipe.masterPreset >= 0)
                applyMasterPreset (recipe.masterPreset);

            setMasterTarget (recipe.target);
            edit->getUndoManager().clearUndoHistory();

            if (! failed.isEmpty())
            {
                log ("Template " + preset.name + ": " + failed.joinIntoString ("; "));
                showHint ("Some tracks couldn't be made: " + failed.joinIntoString ("; "));
            }
            notifyStateChanged();
            writeSongNow (true);   // on disk at once, like any new song (a failed save says so itself)
            return true;
        }

        auto state = readTree (preset.file);

        if (! state.hasType (te::IDs::EDIT))
            return fail ("This isn't a song template, or it's damaged: " + preset.file.getFileName());

        const auto file = AppPaths::createSongFile (songName);

        if (file == juce::File())
            return fail ("Could not create a folder for the song in " + AppPaths::getProjectsFolder().getFullPathName());

        // A new song of its own: a fresh id, nothing of the template's recordings
        removeClips (state);
        resetForTemplate (state);
        const auto id = te::ProjectItemID::createNewID (te::ProjectID {});
        state.setProperty (te::IDs::projectID, id.toString(), nullptr);
        state.removeProperty ("description", nullptr);
        state.removeProperty ("ezTemplateDescription", nullptr);

        auto newEdit = te::Edit::createEdit (te::Edit::Options { audioEngine.getEngine(), state, id, te::Edit::forEditing, nullptr,
                                                                 te::Edit::getDefaultNumUndoLevels(), [file] { return file; }, {} });
        return adoptEdit (std::move (newEdit), file, false);   // its own tracks, as saved (no default ones added)
    }
}
