/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "MidiPatterns.h"

namespace ez::MidiPatterns
{
    namespace
    {
        constexpr int drumFirstNote = 36;   // DrumMachinePlugin::firstNote (Engine never needs the plugin for this)
        constexpr int middleC = 60;

        // Chords as notes from the root (octave 4), voiced to move smoothly from one to the next
        using Chord = std::vector<int>;

        // Major key
        const Chord I { 0, 4, 7 }, Imaj7 { 0, 4, 7, 11 }, ii { 2, 5, 9 }, ii7 { 2, 5, 9, 12 }, iii { -1, 4, 7 }, iii7 { -1, 2, 4, 7 },
                    IV { 0, 5, 9 }, IVmaj7 { 0, 4, 5, 9 }, V { -1, 2, 7 }, V7 { -1, 2, 5, 7 }, vi { 0, 4, 9 }, vi7 { 0, 4, 7, 9 };
        // Minor key
        const Chord i { 0, 3, 7 }, i7 { 0, 3, 7, 10 }, iv { 0, 5, 8 }, iv7 { 0, 3, 5, 8 }, v { -2, 2, 7 }, Vmaj { -1, 2, 7 },
                    VI { 0, 3, 8 }, VImaj7 { -1, 3, 8 }, III { -2, 3, 7 }, VII { -2, 2, 5 };

        /** One chord per bar (or per beatsEach), played in a rhythm: "whole" holds, "stabs" hits the off-beats,
            "pulse" plays eighths, "split" two half-notes. */
        Pattern chords (const char* name, const char* description, bool minor, std::vector<Chord> progression,
                        const char* rhythm = "whole", double beatsEach = 4.0)
        {
            Pattern p { name, description, Kind::chords, minor, beatsEach * (double) progression.size(), {} };
            const juce::String r (rhythm);

            for (size_t c = 0; c < progression.size(); ++c)
            {
                const auto start = (double) c * beatsEach;
                std::vector<std::pair<double, double>> hits;   // start, length (beats, within the chord)

                if (r == "stabs")       for (double b = 0.5; b < beatsEach; b += 1.0) hits.push_back ({ b, 0.25 });
                else if (r == "pulse")  for (double b = 0.0; b < beatsEach; b += 0.5) hits.push_back ({ b, 0.4 });
                else if (r == "split")  { hits.push_back ({ 0.0, beatsEach * 0.5 - 0.05 }); hits.push_back ({ beatsEach * 0.5, beatsEach * 0.5 - 0.05 }); }
                else                    hits.push_back ({ 0.0, beatsEach - 0.05 });

                for (auto [b, len] : hits)
                    for (auto n : progression[c])
                        p.notes.push_back ({ n, start + b, len, r == "stabs" ? 96 : 88 });
            }

            return p;
        }

        /** A line of single notes: { pitch, start, length } (velocity from the beat: stronger on the beat). */
        Pattern line (const char* name, const char* description, Kind kind, bool minor, double lengthBeats,
                      std::vector<std::array<double, 3>> notes)
        {
            Pattern p { name, description, kind, minor, lengthBeats, {} };

            for (const auto& n : notes)
            {
                const bool onBeat = std::abs (n[1] - std::round (n[1])) < 1.0e-6;
                p.notes.push_back ({ (int) n[0], n[1], n[2], onBeat ? 104 : 86 });
            }

            return p;
        }

        /** The same notes again, a set number of beats later (a one-bar idea to a four-bar clip). */
        std::vector<std::array<double, 3>> repeat (std::vector<std::array<double, 3>> notes, double everyBeats, int times)
        {
            std::vector<std::array<double, 3>> all;

            for (int t = 0; t < times; ++t)
                for (auto n : notes)
                    all.push_back ({ n[0], n[1] + t * everyBeats, n[2] });

            return all;
        }

        /** Drums: one lane per pad, one bar each ("x" hit, "X" accent, "o" soft, "." rest). 16 characters are
            sixteenths, 32 are thirty-seconds (hat rolls), 12 are eighth-note triplets, 24 sixteenth triplets.
            The bar repeats for `bars` bars; `fill` (16 steps, pad and steps) replaces that lane in the last bar. */
        struct Lane { int pad; const char* steps; };

        Pattern drums (const char* name, const char* description, std::vector<Lane> lanes, int bars = 2,
                       std::optional<Lane> fill = std::nullopt)
        {
            Pattern p { name, description, Kind::drums, false, 4.0 * bars, {} };

            for (int bar = 0; bar < bars; ++bar)
            {
                for (const auto& lane : lanes)
                {
                    const auto& use = (fill.has_value() && bar == bars - 1 && fill->pad == lane.pad) ? *fill : lane;
                    const juce::String steps (use.steps);
                    const auto step = 4.0 / juce::jmax (1, steps.length());

                    for (int s = 0; s < steps.length(); ++s)
                    {
                        const auto c = steps[s];

                        if (c == 'x' || c == 'X' || c == 'o')
                            p.notes.push_back ({ use.pad, bar * 4.0 + s * step, juce::jmin (0.25, step), c == 'X' ? 127 : c == 'x' ? 108 : 68 });
                    }
                }
            }

            return p;
        }

        // Drum Machine pads
        enum Pad { kick, snare, clap, rim, hat, openHat, shaker, perc, tomLow, tomHigh, crash, ride, vox, hit808, snap, riser };

        std::vector<Pattern> build()
        {
            std::vector<Pattern> all;

            //==========================================================================
            // Chord progressions (four bars)
            all.push_back (chords ("Pop: I-V-vi-IV",        "The four chords behind countless hits (major)", false, { I, V, vi, IV }));
            all.push_back (chords ("Pop: vi-IV-I-V",        "The same four, starting sadder (major)",        false, { vi, IV, I, V }));
            all.push_back (chords ("Doo-wop: I-vi-IV-V",    "50s ballad turnaround (major)",                 false, { I, vi, IV, V }));
            all.push_back (chords ("Gospel: IV-V-iii-vi",   "Lifting, churchy movement (major)",             false, { IVmaj7, V7, iii7, vi7 }));
            all.push_back (chords ("Lo-fi: ii-V-I (sevenths)", "Jazzy and mellow (major)",                   false, { ii7, V7, Imaj7, Imaj7 }));
            all.push_back (chords ("Jazz: ii-V-I-vi",       "The jazz turnaround, two chords a bar (major)", false, { ii7, V7, Imaj7, vi7, ii7, V7, Imaj7, Imaj7 }, "whole", 2.0));
            all.push_back (chords ("Neo-soul: IVmaj7-iii7-vi7", "Smooth R&B colours (major)",                false, { IVmaj7, iii7, vi7, vi7 }));
            all.push_back (chords ("Sad: i-VI-III-VII",     "Emotional minor progression (minor)",           true,  { i, VI, III, VII }));
            all.push_back (chords ("Epic: i-VII-VI-VII",    "Big cinematic build (minor)",                   true,  { i, VII, VI, VII }));
            all.push_back (chords ("Trap: i-iv-i-VI",       "Dark and simple, for 808s (minor)",             true,  { i, iv, i, VI }));
            all.push_back (chords ("Drill: i-VI-iv-v",      "Moody, sliding-bass friendly (minor)",          true,  { i, VI, iv, v }));
            all.push_back (chords ("Andalusian: i-VII-VI-V", "Flamenco descent (minor)",                     true,  { i, VII, VI, Vmaj }));
            all.push_back (chords ("House: minor 7 stabs",  "Off-beat organ/piano stabs (minor)",            true,  { i7, i7, iv7, iv7 }, "stabs"));
            all.push_back (chords ("Dance: pulsing eighths", "Driving eighth-note chords (minor)",           true,  { i, VI, III, VII }, "pulse"));
            all.push_back (chords ("Ballad: halves",        "Two chords a bar, gentle (major)",              false, { I, V, vi, IV }, "split"));
            all.push_back (chords ("R&B: i7-iv7",           "Two-chord groove (minor)",                       true,  { i7, i7, iv7, iv7 }));
            all.push_back (chords ("Dreamy: VImaj7-i7",     "Floating, ambient (minor)",                     true,  { VImaj7, i7, VImaj7, i7 }));

            //==========================================================================
            // Basslines (root at MIDI 36: pitch -24)
            all.push_back (line ("Bass: pop roots",         "Eighth-note roots for I-V-vi-IV (major)", Kind::bass, false, 16.0,
                                 [] { std::vector<std::array<double, 3>> n;
                                      const int roots[] = { -24, -17, -15, -20 };
                                      for (int bar = 0; bar < 4; ++bar) for (int e = 0; e < 8; ++e) n.push_back ({ (double) roots[bar], bar * 4.0 + e * 0.5, 0.45 });
                                      return n; }()));
            all.push_back (line ("Bass: 808 trap",          "Long 808s with a jump up (minor)", Kind::bass, true, 16.0,
                                 { { -24, 0, 1.5 }, { -24, 1.75, 0.75 }, { -12, 2.75, 0.5 }, { -24, 3.5, 0.5 },
                                   { -19, 4, 2.0 }, { -24, 6.5, 1.5 },
                                   { -24, 8, 1.5 }, { -24, 9.75, 0.75 }, { -12, 10.75, 0.5 }, { -24, 11.5, 0.5 },
                                   { -28, 12, 2.5 }, { -26, 14.5, 1.5 } }));
            all.push_back (line ("Bass: drill slides",      "Overlapping notes: a sliding 808 glides (minor)", Kind::bass, true, 16.0,
                                 { { -24, 0, 1.25 }, { -21, 1.0, 0.75 }, { -24, 1.75, 1.5 }, { -17, 4, 1.25 }, { -19, 5.0, 1.0 }, { -24, 6, 2.0 },
                                   { -24, 8, 1.25 }, { -21, 9.0, 0.75 }, { -24, 9.75, 1.5 }, { -16, 12, 1.0 }, { -19, 13, 1.25 }, { -24, 14, 2.0 } }));
            all.push_back (line ("Bass: house offbeat",     "Bouncing off-beat bass (minor)", Kind::bass, true, 16.0,
                                 repeat ({ { -24, 0.5, 0.4 }, { -24, 1.5, 0.4 }, { -12, 2.5, 0.4 }, { -24, 3.5, 0.4 } }, 4.0, 4)));
            all.push_back (line ("Bass: disco octaves",     "Root and octave on eighths (major)", Kind::bass, false, 16.0,
                                 [] { std::vector<std::array<double, 3>> n;
                                      const int roots[] = { -24, -24, -19, -17 };
                                      for (int bar = 0; bar < 4; ++bar) for (int e = 0; e < 8; ++e) n.push_back ({ (double) roots[bar] + (e % 2 ? 12 : 0), bar * 4.0 + e * 0.5, 0.4 });
                                      return n; }()));
            all.push_back (line ("Bass: DnB rolling",       "Long sub notes for 174 BPM (minor)", Kind::bass, true, 16.0,
                                 { { -24, 0, 2.5 }, { -24, 2.75, 1.0 }, { -20, 4, 3.5 }, { -24, 8, 2.5 }, { -24, 10.75, 1.0 }, { -22, 12, 3.5 } }));
            all.push_back (line ("Bass: reggaeton bounce",  "Dembow-friendly bass (minor)", Kind::bass, true, 16.0,
                                 repeat ({ { -24, 0, 0.7 }, { -24, 0.75, 0.25 }, { -24, 1.5, 0.4 }, { -17, 2, 0.7 }, { -17, 2.75, 0.25 }, { -19, 3.5, 0.4 } }, 4.0, 4)));
            all.push_back (line ("Bass: funk sixteenths",   "Syncopated, with ghost notes (minor)", Kind::bass, true, 16.0,
                                 repeat ({ { -24, 0, 0.2 }, { -24, 0.75, 0.2 }, { -12, 1.0, 0.2 }, { -14, 1.5, 0.2 }, { -24, 2.25, 0.2 },
                                           { -21, 2.75, 0.2 }, { -19, 3.0, 0.4 }, { -17, 3.5, 0.2 } }, 4.0, 4)));
            all.push_back (line ("Bass: walking jazz",      "Quarter notes through ii-V-I (major)", Kind::bass, false, 16.0,
                                 { { -22, 0, 0.9 }, { -19, 1, 0.9 }, { -17, 2, 0.9 }, { -15, 3, 0.9 },
                                   { -17, 4, 0.9 }, { -13, 5, 0.9 }, { -12, 6, 0.9 }, { -13, 7, 0.9 },
                                   { -24, 8, 0.9 }, { -20, 9, 0.9 }, { -17, 10, 0.9 }, { -20, 11, 0.9 },
                                   { -24, 12, 0.9 }, { -15, 13, 0.9 }, { -17, 14, 0.9 }, { -18, 15, 0.9 } }));
            all.push_back (line ("Bass: rock eighths",      "Straight driving eighths (major)", Kind::bass, false, 16.0,
                                 [] { std::vector<std::array<double, 3>> n;
                                      const int roots[] = { -24, -24, -19, -17 };
                                      for (int bar = 0; bar < 4; ++bar) for (int e = 0; e < 8; ++e) n.push_back ({ (double) roots[bar], bar * 4.0 + e * 0.5, 0.45 });
                                      return n; }()));

            //==========================================================================
            // Melodies and arps
            all.push_back (line ("Arp: minor up 16ths",     "Up the chord over two octaves (minor)", Kind::melody, true, 16.0,
                                 [] { std::vector<std::array<double, 3>> n;
                                      const int up[] = { 0, 3, 7, 12, 15, 19, 24, 19 };
                                      for (int s = 0; s < 64; ++s) n.push_back ({ (double) up[s % 8] - 12, s * 0.25, 0.22 });
                                      return n; }()));
            all.push_back (line ("Arp: trance gate",        "Pulsing chord tones with an octave jump (minor)", Kind::melody, true, 16.0,
                                 [] { std::vector<std::array<double, 3>> n;
                                      const int tones[] = { 0, 12, 7, 12, 3, 12, 7, 12 };
                                      const int shift[] = { 0, -4, 3, -2 };
                                      for (int s = 0; s < 64; ++s) n.push_back ({ (double) tones[s % 8] + shift[s / 16], s * 0.25, 0.18 });
                                      return n; }()));
            all.push_back (line ("Arp: major broken chords", "Gentle eighth-note broken chords (major)", Kind::melody, false, 16.0,
                                 [] { std::vector<std::array<double, 3>> n;
                                      const int chordsUp[4][4] = { { 0, 4, 7, 12 }, { -5, -1, 2, 7 }, { -3, 0, 4, 9 }, { -7, -3, 0, 5 } };
                                      for (int bar = 0; bar < 4; ++bar) for (int e = 0; e < 8; ++e) n.push_back ({ (double) chordsUp[bar][e < 4 ? e : 7 - e], bar * 4.0 + e * 0.5, 0.45 });
                                      return n; }()));
            all.push_back (line ("Melody: pentatonic hook", "A catchy four-bar hook (minor pentatonic)", Kind::melody, true, 16.0,
                                 { { 12, 0, 0.5 }, { 10, 0.5, 0.5 }, { 7, 1, 1 }, { 10, 2.5, 0.5 }, { 12, 3, 1 },
                                   { 15, 4, 0.75 }, { 12, 4.75, 0.75 }, { 10, 5.5, 0.5 }, { 7, 6, 2 },
                                   { 12, 8, 0.5 }, { 10, 8.5, 0.5 }, { 7, 9, 1 }, { 5, 10.5, 0.5 }, { 3, 11, 1 },
                                   { 5, 12, 0.75 }, { 3, 12.75, 0.75 }, { 0, 13.5, 2.5 } }));
            all.push_back (line ("Melody: lo-fi motif",     "Lazy, behind-the-beat phrase (major)", Kind::melody, false, 16.0,
                                 { { 4, 0.25, 0.75 }, { 7, 1.25, 0.5 }, { 11, 2, 1.5 }, { 9, 4.25, 0.75 }, { 7, 5.25, 0.5 }, { 4, 6, 1.75 },
                                   { 2, 8.25, 0.75 }, { 4, 9.25, 0.5 }, { 7, 10, 1.5 }, { 4, 12.25, 0.75 }, { 2, 13.25, 0.5 }, { 0, 14, 2 } }));
            all.push_back (line ("Melody: bell motif",      "Sparse, ringing notes (major)", Kind::melody, false, 16.0,
                                 { { 12, 0, 1.5 }, { 7, 1.5, 1 }, { 16, 3, 1 }, { 14, 4, 2 }, { 11, 6, 2 },
                                   { 12, 8, 1.5 }, { 7, 9.5, 1 }, { 19, 11, 1 }, { 16, 12, 4 } }));
            all.push_back (line ("Melody: pluck riff",      "Syncopated sixteenth riff (minor)", Kind::melody, true, 16.0,
                                 repeat ({ { 0, 0, 0.25 }, { 12, 0.75, 0.25 }, { 10, 1.5, 0.25 }, { 7, 2, 0.25 }, { 3, 2.75, 0.25 }, { 7, 3.5, 0.25 } }, 4.0, 4)));
            all.push_back (line ("Melody: call & answer",   "A two-bar question, a two-bar answer (major)", Kind::melody, false, 16.0,
                                 { { 7, 0, 0.5 }, { 9, 0.5, 0.5 }, { 12, 1, 1 }, { 11, 2, 0.5 }, { 9, 2.5, 0.5 }, { 7, 3, 3 },
                                   { 4, 8, 0.5 }, { 5, 8.5, 0.5 }, { 7, 9, 1 }, { 5, 10, 0.5 }, { 4, 10.5, 0.5 }, { 0, 11, 3 } }));

            //==========================================================================
            // Drum beats (two bars; the last bar has a fill where given)
            all.push_back (drums ("Boom bap",       "Hip hop, 85-95 BPM",
                                  { { kick, "x......x..x....." }, { snare, "....x.......x..." }, { hat, "x.o.x.o.x.o.x.o." } }, 2,
                                  Lane { snare, "....x.......x.xx" }));
            all.push_back (drums ("Trap",           "130-150 BPM, with hat rolls",
                                  { { kick, "x.....x...x....." }, { clap, "........x......." },
                                    { hat, "x.x.x.x.x.x.x.x.xxxx.x.x.x.xxxxx" }, { openHat, "..............x." } }));
            all.push_back (drums ("Drill",          "UK/NY drill, 140 BPM: snare on 4, sliding hats",
                                  { { kick, "x.........x..x.." }, { snare, "........x......." }, { hat, "x..x..x.x..x..x." }, { perc, "......x.......x." } }));
            all.push_back (drums ("House",          "Four on the floor, 120-128 BPM",
                                  { { kick, "x...x...x...x..." }, { clap, "....x.......x..." }, { hat, "..x...x...x...x." }, { shaker, "xxxxxxxxxxxxxxxx" } }));
            all.push_back (drums ("Techno",         "Driving, 128-135 BPM",
                                  { { kick, "X...X...X...X..." }, { clap, "....x.......x..." }, { hat, "..x...x...x...x." }, { ride, "x.x.x.x.x.x.x.x." }, { perc, "...x......x....." } }));
            all.push_back (drums ("Drum & bass",    "Two-step, 170-176 BPM",
                                  { { kick, "x.........x....." }, { snare, "....x.......x..." }, { hat, "x.x.x.x.x.x.x.x." }, { shaker, "..x...x...x...xx" } }, 2,
                                  Lane { snare, "....x..x.x..x.xx" }));
            all.push_back (drums ("Breakbeat",      "Classic break feel, 90-110 BPM",
                                  { { kick, "x.........x.x..." }, { snare, "....x..o.o..x..o" }, { hat, "x.x.x.x.x.x.x.x." } }));
            all.push_back (drums ("Afrobeats",      "Bouncing groove, 100-115 BPM",
                                  { { kick, "x..x..x...x..x.." }, { rim, "...x..x....x..x." }, { shaker, "x.xxx.xxx.xxx.xx" }, { perc, "......x.......x." } }));
            all.push_back (drums ("Reggaeton",      "The dembow, 90-100 BPM",
                                  { { kick, "x...x...x...x..." }, { snare, "...x..x....x..x." }, { hat, "x.x.x.x.x.x.x.x." } }));
            all.push_back (drums ("UK garage",      "Skippy two-step, 130-135 BPM",
                                  { { kick, "x.........x....." }, { snare, "....x.......x..." }, { hat, "..x..x.x..x..x.x" }, { shaker, "x.xx.xx.x.xx.xx." } }));
            all.push_back (drums ("Lo-fi",          "Lazy hip hop, 70-85 BPM",
                                  { { kick, "x........x......" }, { snare, "....x.......x..." }, { hat, "x.o.x.o.x.o.x.oo" }, { rim, "..........o....." } }));
            all.push_back (drums ("Pop",            "Punchy, 100-120 BPM",
                                  { { kick, "x.....x.x......." }, { snare, "....x.......x..." }, { hat, "x.x.x.x.x.x.x.x." }, { clap, "....x.......x..." } }, 2,
                                  Lane { snare, "....x.......xxxx" }));
            all.push_back (drums ("R&B",            "Slow and swung, 65-80 BPM",
                                  { { kick, "x......x.x......" }, { snap, "....x.......x..." }, { hat, "xoxoxoxoxoxoxoxo" }, { openHat, "..............x." } }));
            all.push_back (drums ("Rock",           "Straight eighths, 100-140 BPM",
                                  { { kick, "x.......x.x....." }, { snare, "....x.......x..." }, { hat, "x.x.x.x.x.x.x.x." } }, 2,
                                  Lane { snare, "....x.......xxxx" }));
            all.push_back (drums ("Punk",           "Fast and loud, 170-200 BPM",
                                  { { kick, "x...x...x...x..." }, { snare, "..x...x...x...x." }, { crash, "x..............." }, { hat, "x.x.x.x.x.x.x.x." } }));
            all.push_back (drums ("Metal: double kick", "Sixteenth kicks under a backbeat, 120-180 BPM",
                                  { { kick, "xxxxxxxxxxxxxxxx" }, { snare, "....x.......x..." }, { ride, "x.x.x.x.x.x.x.x." } }));
            all.push_back (drums ("Blast beat",     "Extreme metal, 180+ BPM",
                                  { { kick, "x.x.x.x.x.x.x.x." }, { snare, ".x.x.x.x.x.x.x.x" }, { crash, "x.x.x.x.x.x.x.x." } }));
            all.push_back (drums ("Funk",           "Ghost notes and syncopation, 95-115 BPM",
                                  { { kick, "x..x......x..x.." }, { snare, "....x..o.o..x..o" }, { hat, "xxxxxxxxxxxxxxxx" }, { openHat, "..............x." } }));
            all.push_back (drums ("Jazz swing",     "Ride pattern and soft feathered kick",
                                  { { ride, "x...x..xx...x..x" }, { hat, "....x.......x..." }, { kick, "o...o...o...o..." }, { rim, "......o.......o." } }));
            all.push_back (drums ("Half-time",      "Heavy, the snare on 3",
                                  { { kick, "x.....x...x....." }, { snare, "........x......." }, { hat, "x.x.x.x.x.x.x.x." } }));
            all.push_back (drums ("Shuffle (triplets)", "Swung triplet feel, 80-110 BPM",
                                  { { kick, "x.....x.....x..." }, { snare, "...x.....x.." }, { hat, "x.xx.xx.xx.x" } }));
            all.push_back (drums ("Build-up roll",  "Snare roll that speeds up, into a crash",
                                  { { snare, "x...x...x.x.x.x.xxxxxxxxxxxxxxxx" }, { kick, "x...x...x...x..." } }, 2));

            return all;
        }
    }

    juce::String Pattern::categoryName() const
    {
        return kindName (kind);
    }

    juce::String kindName (Kind k)
    {
        switch (k)
        {
            case Kind::chords:  return "Chord progressions";
            case Kind::bass:    return "Basslines";
            case Kind::melody:  return "Melodies & arps";
            case Kind::drums:   return "Drum beats";
        }

        return {};
    }

    const std::vector<Pattern>& getAll()
    {
        static const std::vector<Pattern> all = build();
        return all;
    }

    const Pattern* find (const juce::String& name)
    {
        for (const auto& p : getAll())
            if (p.name == name)
                return &p;

        return nullptr;
    }

    int transposeFor (int keyRoot)
    {
        if (! juce::isPositiveAndBelow (keyRoot, 12))
            return 0;

        return keyRoot > 6 ? keyRoot - 12 : keyRoot;   // G to B go down, so nothing climbs more than a tritone
    }

    int midiNoteFor (const Pattern& p, const Note& n, int keyRoot)
    {
        if (p.kind == Kind::drums)
            return drumFirstNote + n.pitch;

        return juce::jlimit (0, 127, middleC + n.pitch + transposeFor (keyRoot));
    }

    juce::String suggestedSound (const Pattern& p)
    {
        switch (p.kind)
        {
            case Kind::chords:  return p.name.containsIgnoreCase ("stabs") ? "Organ" : p.name.containsIgnoreCase ("Dreamy") ? "Warm pad" : "Electric piano";
            case Kind::bass:    return p.name.containsIgnoreCase ("808") || p.name.containsIgnoreCase ("drill") ? "808 slide"
                                     : p.name.containsIgnoreCase ("house") || p.name.containsIgnoreCase ("disco") || p.name.containsIgnoreCase ("funk") ? "Deep house bass"
                                     : "Sub bass";
            case Kind::melody:  return p.name.containsIgnoreCase ("Arp") ? "Pluck" : p.name.containsIgnoreCase ("bell") ? "Bell" : "Sine lead";
            case Kind::drums:   return {};
        }

        return {};
    }
}
