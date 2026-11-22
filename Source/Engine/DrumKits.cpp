/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "DrumKits.h"
#include "../Core/AppPaths.h"

namespace ez::DrumKits
{
    namespace
    {
        constexpr double sampleRate = 44100.0;
        constexpr float pi = juce::MathConstants<float>::pi, twoPi = juce::MathConstants<float>::twoPi;

        // Bump when the sounds change, so old files are not mixed with new ones
        constexpr const char* soundVersion = "v1";

        const KitInfo kits[] =
        {
            { "Boom Bap Dust",   "Hip hop",     "Punchy, dusty, 90 BPM vibe" },
            { "Trap 808",        "Hip hop",     "Long 808, crisp hats for rolls" },
            { "Rolling DnB",     "Drum & bass", "Tight kick, snappy snare" },
            { "Liquid Kit",      "Drum & bass", "Soft and rolling" },
            { "Warehouse House", "House",       "Classic four-on-the-floor kit" },
            { "Tape Kit",        "Lo-fi",       "Warm, saturated, a little hiss" },
            // Added later: new kits go on the end (songs and presets name kits by their place)
            { "Garage Punk",     "Rock & punk", "Loud, open, a snare that cracks" },
            { "Metal Kit",       "Metal",       "Clicky kick for double kick and blasts, tight snare" },
            { "Hardcore Kit",    "Hardcore",    "Heavy, driven, a fat snare for breakdowns" },
            { "Techno Kit",      "Techno",      "A long, driven kick, tight hats, a dry clap" },
            { "Classic 808",     "Hip hop",     "The old-school boom: long kick, snappy snare, cowbell-bright hats" },
            { "Afro Kit",        "Afrobeats",   "Warm short kick, bright rim and shakers for bouncing grooves" },
            { "Pop Kit",         "Pop",         "Clean, punchy, radio-ready" },
            { "Soft Jazz",       "Jazz & soul", "Soft kick, brushy snare, long cymbals" },
            { "UK Garage",       "House",       "Skippy two-step kit: tight kick, crisp snare" },
        };

        const char* const padNames[numPads] =
        {
            "Kick", "Snare", "Clap", "Rim", "Closed Hat", "Open Hat", "Shaker", "Perc",
            "Tom Low", "Tom High", "Crash", "Ride", "Vox Chop", "808 Hit", "Snap", "FX Riser"
        };

        const juce::uint32 padColours[numPads] =
        {
            0xffe58a3a, 0xffe0c341, 0xffe0c341, 0xffe0c341, 0xff3fb8c8, 0xff3fb8c8, 0xff3fb8c8, 0xff3fb8c8,
            0xffb65bd6, 0xffb65bd6, 0xff7f8cff, 0xff7f8cff, 0xff3fa36b, 0xffe58a3a, 0xffe0c341, 0xff7f8cff
        };

        // How loud each sound is next to the kick, so a fresh kit is already roughly mixed
        const float padTrim[numPads] = { 1.0f, 0.9f, 0.8f, 0.6f, 0.5f, 0.45f, 0.4f, 0.6f, 0.75f, 0.75f, 0.45f, 0.4f, 0.6f, 0.9f, 0.6f, 0.45f };

        /** What makes one kit sound different from another. */
        struct Flavour
        {
            float kickStart, kickEnd, kickPitchTime, kickDecay, kickClick, kickDrive;
            float snareTone, snareToneDecay, snareNoiseDecay, snareNoiseLowpass, snareNoiseMix;
            float hatDecay, hatHighpass, openHatDecay, cymbalDecay;
            float tomBase;
            float bassDecay, bassFreq, bassDrive;
            float lowpass;      // the whole kit, Hz (0 = off)
            float drive;        // the whole kit (1 = clean)
            float noiseFloor;   // tape hiss and crackle
            float tune;         // semitones, for the pitched sounds
        };

        const Flavour flavours[] =
        {
            { 150, 52, 0.035f, 0.35f, 0.5f, 1.6f,   190, 0.08f, 0.18f,  7000, 0.70f,   0.050f, 7000, 0.35f, 1.4f,   90,   0.9f, 49, 1.4f,   9500, 1.30f, 0.004f,  0 },
            { 180, 46, 0.030f, 0.45f, 0.7f, 1.2f,   210, 0.06f, 0.14f, 11000, 0.80f,   0.035f, 8500, 0.45f, 1.8f,  100,   2.2f, 44, 1.8f,      0, 1.00f, 0.0f,    0 },
            { 200, 58, 0.025f, 0.25f, 0.8f, 1.5f,   230, 0.07f, 0.20f, 12000, 0.75f,   0.030f, 9000, 0.30f, 1.6f,  110,   0.7f, 55, 1.6f,      0, 1.10f, 0.0f,    2 },
            { 140, 55, 0.040f, 0.30f, 0.3f, 1.1f,   200, 0.10f, 0.22f,  8000, 0.60f,   0.040f, 7500, 0.30f, 2.0f,   95,   0.8f, 52, 1.2f,  12000, 1.00f, 0.0f,    0 },
            { 160, 48, 0.050f, 0.50f, 0.45f, 1.3f,  180, 0.09f, 0.20f,  9000, 0.65f,   0.045f, 8000, 0.50f, 1.6f,   85,   1.2f, 47, 1.5f,      0, 1.15f, 0.0f,   -1 },
            { 130, 50, 0.040f, 0.32f, 0.2f, 2.0f,   175, 0.09f, 0.20f,  5500, 0.65f,   0.050f, 6000, 0.30f, 1.2f,   85,   0.8f, 48, 1.6f,   6000, 1.80f, 0.010f, -2 },
            { 130, 60, 0.030f, 0.30f, 0.6f, 1.4f,   195, 0.09f, 0.26f, 10000, 0.70f,   0.045f, 8000, 0.50f, 2.4f,  100,   0.6f, 50, 1.2f,      0, 1.20f, 0.0f,    0 },
            { 220, 62, 0.012f, 0.18f, 1.2f, 1.8f,   240, 0.07f, 0.22f, 13000, 0.75f,   0.030f, 9500, 0.40f, 2.6f,  110,   0.5f, 55, 1.4f,      0, 1.15f, 0.0f,    1 },
            { 160, 55, 0.025f, 0.28f, 0.9f, 1.9f,   210, 0.08f, 0.30f,  9000, 0.80f,   0.040f, 8000, 0.45f, 2.2f,   95,   0.7f, 50, 1.6f,      0, 1.50f, 0.0f,   -1 },
            { 170, 45, 0.040f, 0.55f, 0.6f, 1.8f,   200, 0.07f, 0.16f,  9000, 0.70f,   0.025f, 9500, 0.25f, 1.4f,   95,   1.0f, 45, 1.5f,      0, 1.40f, 0.0f,    0 },
            { 120, 45, 0.060f, 0.90f, 0.3f, 1.0f,   220, 0.12f, 0.15f,  9000, 0.60f,   0.040f, 8000, 0.50f, 1.5f,   90,   2.8f, 43, 1.0f,      0, 1.00f, 0.0f,    0 },
            { 150, 55, 0.030f, 0.25f, 0.4f, 1.2f,   260, 0.06f, 0.12f, 10000, 0.60f,   0.030f, 8500, 0.35f, 1.6f,  120,   1.2f, 48, 1.3f,      0, 1.10f, 0.0f,    2 },
            { 165, 52, 0.035f, 0.35f, 0.55f, 1.3f,  200, 0.08f, 0.20f, 11000, 0.72f,   0.035f, 8500, 0.40f, 2.0f,  100,   1.0f, 48, 1.4f,      0, 1.10f, 0.0f,    0 },
            { 110, 55, 0.030f, 0.30f, 0.15f, 1.0f,  180, 0.05f, 0.35f,  6000, 0.90f,   0.060f, 6500, 0.60f, 3.0f,  100,   0.6f, 50, 1.0f,  10000, 1.00f, 0.002f,  0 },
            { 170, 50, 0.035f, 0.30f, 0.6f, 1.3f,   220, 0.06f, 0.14f, 10000, 0.70f,   0.030f, 9000, 0.30f, 1.5f,  105,   1.0f, 50, 1.4f,      0, 1.15f, 0.0f,    1 },
        };

        static_assert (std::size (kits) == std::size (flavours));

        //==============================================================================
        /** Same seed, same noise: a kit always sounds the same. */
        struct Noise
        {
            explicit Noise (juce::uint32 seed) : state (seed * 2654435761u + 1u) {}

            float next()
            {
                state = state * 1664525u + 1013904223u;
                return (float) (state >> 8) / 8388608.0f - 1.0f;
            }

            juce::uint32 state;
        };

        struct OnePole
        {
            float z = 0.0f, a = 1.0f;

            void setCutoff (float hz)   { a = 1.0f - std::exp (-twoPi * juce::jlimit (10.0f, 20000.0f, hz) / (float) sampleRate); }
            float lowpass (float x)     { z += a * (x - z); return z; }
            float highpass (float x)    { return x - lowpass (x); }
        };

        /** Chamberlin state-variable filter: band-pass output. */
        struct BandPass
        {
            float low = 0.0f, band = 0.0f;

            float process (float x, float hz, float q)
            {
                const auto f = 2.0f * std::sin (pi * juce::jlimit (20.0f, 7000.0f, hz) / (float) sampleRate);
                low += f * band;
                const auto high = x - low - q * band;
                band += f * high;
                return band;
            }
        };

        float decayEnvelope (float t, float decay)  { return std::exp (-t * 4.6f / juce::jmax (0.001f, decay)); }   // -40 dB at "decay"
        float square (float phase)                  { return std::sin (phase) >= 0.0f ? 1.0f : -1.0f; }

        /** Six detuned squares: the classic metallic hat/cymbal tone. */
        float metallic (float t, float scale)
        {
            static const float freqs[] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
            float s = 0.0f;

            for (auto f : freqs)
                s += square (twoPi * f * scale * t);

            return s / 6.0f;
        }

        float shaped (float x, float drive)
        {
            return drive <= 1.0f ? x : std::tanh (x * drive) / std::tanh (drive);
        }

        int samplesFor (float seconds)   { return juce::jmax (64, (int) (seconds * (float) sampleRate)); }

        //==============================================================================
        std::vector<float> makeSound (int pad, const Flavour& k, juce::uint32 seed)
        {
            Noise noise (seed);
            const auto tuneRatio = std::pow (2.0f, k.tune / 12.0f);
            std::vector<float> out;
            const auto t = [] (size_t i) { return (float) i / (float) sampleRate; };

            switch (pad)
            {
                case 0:   // Kick
                {
                    out.resize ((size_t) samplesFor (k.kickDecay * 1.6f + 0.05f));
                    float phase = 0.0f;

                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const auto f = (k.kickEnd + (k.kickStart - k.kickEnd) * std::exp (-t (i) / k.kickPitchTime)) * tuneRatio;
                        phase += twoPi * f / (float) sampleRate;
                        const auto body = std::sin (phase) * decayEnvelope (t (i), k.kickDecay);
                        const auto click = k.kickClick * noise.next() * std::exp (-t (i) / 0.003f) * 0.6f;
                        out[i] = shaped (body + click, k.kickDrive);
                    }
                    break;
                }

                case 1:   // Snare
                {
                    out.resize ((size_t) samplesFor (k.snareNoiseDecay * 1.3f + 0.02f));
                    OnePole hp, lp;
                    hp.setCutoff (1000.0f);
                    lp.setCutoff (k.snareNoiseLowpass);
                    const auto tone = k.snareTone * tuneRatio;

                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const auto toneEnv = decayEnvelope (t (i), k.snareToneDecay);
                        const auto body = (0.6f * std::sin (twoPi * tone * t (i)) + 0.3f * std::sin (twoPi * tone * 1.47f * t (i))) * toneEnv;
                        const auto rattle = lp.lowpass (hp.highpass (noise.next())) * decayEnvelope (t (i), k.snareNoiseDecay) * k.snareNoiseMix * 1.6f;
                        out[i] = body * (1.0f - k.snareNoiseMix * 0.5f) + rattle;
                    }
                    break;
                }

                case 2:   // Clap: three quick bursts, then a tail
                {
                    out.resize ((size_t) samplesFor (0.35f));
                    BandPass bp;

                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        float env = 0.0f;

                        for (int burst = 0; burst < 3; ++burst)
                        {
                            const auto since = t (i) - (float) burst * 0.011f;

                            if (since >= 0.0f)
                                env = juce::jmax (env, std::exp (-since / 0.006f));
                        }

                        env = juce::jmax (env, t (i) >= 0.022f ? 0.55f * decayEnvelope (t (i) - 0.022f, 0.18f) : 0.0f);
                        out[i] = bp.process (noise.next(), 1200.0f, 0.9f) * env * 2.2f;
                    }
                    break;
                }

                case 3:   // Rim
                {
                    out.resize ((size_t) samplesFor (0.08f));

                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const auto env = decayEnvelope (t (i), 0.035f);
                        out[i] = (0.6f * std::sin (twoPi * 1700.0f * tuneRatio * t (i)) + 0.4f * square (twoPi * 800.0f * tuneRatio * t (i))) * env
                                 + noise.next() * std::exp (-t (i) / 0.002f) * 0.4f;
                    }
                    break;
                }

                case 4:   // Closed hat
                case 5:   // Open hat
                {
                    const auto decay = pad == 4 ? k.hatDecay : k.openHatDecay;
                    out.resize ((size_t) samplesFor (decay * 1.4f + 0.01f));
                    OnePole hp1, hp2;
                    hp1.setCutoff (k.hatHighpass);
                    hp2.setCutoff (k.hatHighpass);

                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const auto raw = 0.6f * metallic (t (i), 2.0f) + 0.5f * noise.next();
                        out[i] = hp2.highpass (hp1.highpass (raw)) * decayEnvelope (t (i), decay) * 1.8f;
                    }
                    break;
                }

                case 6:   // Shaker
                {
                    out.resize ((size_t) samplesFor (0.16f));
                    OnePole hp;
                    hp.setCutoff (5000.0f);

                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const auto attack = juce::jmin (1.0f, t (i) / 0.015f);
                        out[i] = hp.highpass (noise.next()) * attack * decayEnvelope (juce::jmax (0.0f, t (i) - 0.015f), 0.09f) * 1.5f;
                    }
                    break;
                }

                case 7:   // Perc (a conga-like tone)
                case 8:   // Tom low
                case 9:   // Tom high
                {
                    const auto base = (pad == 7 ? 330.0f : pad == 8 ? k.tomBase : k.tomBase * 1.6f) * tuneRatio;
                    const auto decay = pad == 7 ? 0.18f : 0.35f;
                    out.resize ((size_t) samplesFor (decay * 1.4f));
                    float phase = 0.0f;

                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const auto f = base * (1.0f + 0.6f * std::exp (-t (i) / 0.02f));
                        phase += twoPi * f / (float) sampleRate;
                        out[i] = std::sin (phase) * decayEnvelope (t (i), decay) + noise.next() * std::exp (-t (i) / 0.004f) * 0.2f;
                    }
                    break;
                }

                case 10:  // Crash
                case 11:  // Ride
                {
                    const bool ride = pad == 11;
                    const auto decay = ride ? k.cymbalDecay * 0.9f : k.cymbalDecay;
                    out.resize ((size_t) samplesFor (decay * 1.2f));
                    OnePole hp;
                    hp.setCutoff (ride ? 5000.0f : 4000.0f);

                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        auto s = (ride ? 0.7f : 0.45f) * metallic (t (i), ride ? 3.0f : 2.4f) + (ride ? 0.25f : 0.6f) * noise.next();

                        if (ride)
                            s += 0.35f * std::sin (twoPi * 3000.0f * t (i)) * decayEnvelope (t (i), 0.4f);

                        out[i] = hp.highpass (s) * decayEnvelope (t (i), decay) * 1.4f;
                    }
                    break;
                }

                case 12:  // Vox chop: a short sung "ah"
                {
                    out.resize ((size_t) samplesFor (0.3f));
                    BandPass f1, f2;
                    float phase = 0.0f;

                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const auto f = 220.0f * tuneRatio * (1.0f - 0.04f * t (i) / 0.3f);
                        phase = std::fmod (phase + f / (float) sampleRate, 1.0f);
                        const auto saw = 2.0f * phase - 1.0f;
                        const auto env = juce::jmin (1.0f, t (i) / 0.01f) * decayEnvelope (t (i), 0.28f);
                        out[i] = (f1.process (saw, 800.0f, 0.25f) + 0.7f * f2.process (saw, 1200.0f, 0.25f)) * env * 1.5f;
                    }
                    break;
                }

                case 13:  // 808 hit
                {
                    out.resize ((size_t) samplesFor (k.bassDecay * 1.3f));
                    float phase = 0.0f;

                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const auto f = k.bassFreq * tuneRatio * (1.0f + 1.5f * std::exp (-t (i) / 0.04f));
                        phase += twoPi * f / (float) sampleRate;
                        out[i] = shaped (std::sin (phase) * decayEnvelope (t (i), k.bassDecay), k.bassDrive);
                    }
                    break;
                }

                case 14:  // Snap
                {
                    out.resize ((size_t) samplesFor (0.12f));
                    BandPass bp;

                    for (size_t i = 0; i < out.size(); ++i)
                        out[i] = bp.process (noise.next(), 2500.0f, 0.7f) * decayEnvelope (t (i), 0.06f) * 2.5f;

                    break;
                }

                default:  // 15: FX riser, filtered noise sweeping up
                {
                    const auto length = 1.6f;
                    out.resize ((size_t) samplesFor (length));
                    BandPass bp;

                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const auto x = t (i) / length;
                        const auto fadeOut = juce::jmin (1.0f, (length - t (i)) / 0.06f);
                        out[i] = bp.process (noise.next(), 300.0f * std::pow (25.0f, x), 0.5f) * x * x * fadeOut * 2.0f;
                    }
                    break;
                }
            }

            // The whole kit's colour: tone, saturation, tape noise
            OnePole tone1, tone2;
            tone1.setCutoff (k.lowpass);
            tone2.setCutoff (k.lowpass);
            Noise hiss (seed ^ 0x5eedu);

            for (auto& s : out)
            {
                if (k.lowpass > 0.0f)
                    s = tone2.lowpass (tone1.lowpass (s));

                s = shaped (s, k.drive);

                if (k.noiseFloor > 0.0f)
                {
                    const auto n = hiss.next();
                    s += n * k.noiseFloor * 0.3f + (std::abs (n) > 0.9995f ? n * k.noiseFloor * 20.0f : 0.0f);   // hiss + rare crackle
                }
            }

            // Same peak for every sound, then the per-pad trim; short fade at the end: no clicks
            float peak = 0.0f;

            for (auto s : out)
                peak = juce::jmax (peak, std::abs (s));

            const auto gain = peak > 0.0f ? 0.8f * padTrim[pad] / peak : 0.0f;
            const auto fade = juce::jmin ((int) out.size(), (int) (0.002 * sampleRate));

            for (size_t i = 0; i < out.size(); ++i)
            {
                out[i] *= gain;
                const auto fromEnd = (int) (out.size() - 1 - i);

                if (fromEnd < fade)
                    out[i] *= (float) fromEnd / (float) fade;
            }

            return out;
        }

        bool writeWav (const juce::File& file, const std::vector<float>& samples)
        {
            // Written next to the target, then moved into place: a half-written file never has the real name
            const auto temp = file.getSiblingFile (file.getFileNameWithoutExtension() + ".partial");
            temp.deleteFile();

            {
                std::unique_ptr<juce::OutputStream> stream = temp.createOutputStream();

                if (stream == nullptr)
                    return false;

                juce::WavAudioFormat wav;
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                               .withSampleRate (sampleRate)
                                                               .withNumChannels (1)
                                                               .withBitsPerSample (24));

                if (writer == nullptr)
                    return false;

                const float* channels[] = { samples.data() };

                if (! writer->writeFromFloatArrays (channels, 1, (int) samples.size()))
                    return false;
            }

            return temp.moveFileTo (file);
        }
    }

    //==============================================================================
    int getNumKits()                        { return (int) std::size (kits); }

    const KitInfo& getKit (int kitIndex)
    {
        jassert (juce::isPositiveAndBelow (kitIndex, getNumKits()));
        return kits[juce::jlimit (0, getNumKits() - 1, kitIndex)];
    }

    int findKit (const juce::String& name)
    {
        for (int i = 0; i < getNumKits(); ++i)
            if (name == kits[i].name)
                return i;

        return -1;
    }

    juce::StringArray getGenres()
    {
        juce::StringArray genres;

        for (auto& k : kits)
            genres.addIfNotAlreadyThere (k.genre);

        return genres;
    }

    juce::String getPadName (int pad)       { return juce::isPositiveAndBelow (pad, numPads) ? padNames[pad] : "Pad"; }
    juce::Colour getPadColour (int pad)     { return juce::Colour (juce::isPositiveAndBelow (pad, numPads) ? padColours[pad] : 0xff8a93a6); }
    int getDefaultChokeGroup (int pad)      { return pad == 4 || pad == 5 ? 1 : 0; }

    juce::File getFolder()
    {
        return AppPaths::getRootFolder().getChildFile ("Drum Kits");
    }

    juce::File getSampleFile (int kitIndex, int pad, juce::String& error)
    {
        if (! juce::isPositiveAndBelow (kitIndex, getNumKits()) || ! juce::isPositiveAndBelow (pad, numPads))
        {
            error = "There is no such kit sound";
            return {};
        }

        const auto folder = getFolder().getChildFile (juce::String ("Built-in ") + soundVersion).getChildFile (kits[kitIndex].name);
        const auto file = folder.getChildFile (juce::String (pad + 1).paddedLeft ('0', 2) + " " + padNames[pad] + ".wav");

        if (file.existsAsFile() && file.getSize() > 0)
            return file;

        if (! folder.createDirectory().wasOk())
        {
            error = "Could not make the folder " + folder.getFullPathName();
            return {};
        }

        if (! writeWav (file, makeSound (pad, flavours[kitIndex], (juce::uint32) (kitIndex * 100 + pad + 1))))
        {
            error = "Could not write " + file.getFullPathName();
            return {};
        }

        return file;
    }
}
