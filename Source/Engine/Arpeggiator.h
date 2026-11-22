/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The arpeggiator's note logic, apart from Tracktion so it can be tested on its own.

    Real-time safe: fixed-size arrays, no allocation, no locks. Held notes and the incoming
    note queue are capped (extra notes are ignored, never a crash). Time is in beats: the
    plugin hands each block's start and end beat (the song's while playing, a free-running
    clock at the song's tempo when stopped), and steps land exactly on the grid.
*/

#pragma once

#include "../Core/Common.h"

namespace ez
{
    class Arpeggiator
    {
    public:
        enum Mode { up, down, upDown, asPlayed, random, numModes };

        struct Settings
        {
            double stepBeats = 0.25;    // 1/16
            int mode = up;
            int octaves = 1;            // 1..4
            float gate = 0.6f;          // of a step, 0.05..1
            bool latch = false;         // notes keep playing after the keys are let go, until a new chord
            int fixedVelocity = 0;      // 0 = as played
        };

        struct Event
        {
            int sampleOffset;
            int note;
            int velocity;   // 0 = note off
        };

        /** Queues a key going down (velocity > 0) or up (0) at a sample in the coming block. */
        void queueInput (int sampleOffset, int note, int velocity) noexcept;

        /** Everything off at once (all-notes-off, a seek, the plugin bypassed). */
        void reset() noexcept;

        /** Runs one block covering [startBeat, endBeat) over numSamples, calling emit (const Event&)
            in time order. A jump in beats (a seek or loop) ends the sounding note and restarts. */
        template <typename Emit>
        void process (double startBeat, double endBeat, int numSamples, const Settings& s, Emit&& emit)
        {
            if (numSamples <= 0 || endBeat <= startBeat)
            {
                applyInputs (std::numeric_limits<int>::max(), s);
                return;
            }

            const auto step = juce::jlimit (1.0 / 64.0, 4.0, s.stepBeats);
            const auto toSample = [&] (double beat)
            {
                return juce::jlimit (0, numSamples - 1, (int) ((beat - startBeat) / (endBeat - startBeat) * numSamples));
            };

            if (std::abs (startBeat - expectedBeat) > 1.0e-6 && sounding >= 0)   // a jump: end what's playing
                endSounding (0, emit);

            if (std::abs (startBeat - expectedBeat) > 1.0e-6)
                stepIndex = 0;

            expectedBeat = endBeat;
            applyLatch (s);

            auto nextStep = std::ceil (startBeat / step - 1.0e-9) * step;

            for (;;)
            {
                const auto offDue = sounding >= 0 && offBeat < endBeat;
                const auto stepDue = nextStep < endBeat;

                if (! offDue && ! stepDue)
                    break;

                if (offDue && (! stepDue || offBeat <= nextStep))
                {
                    applyInputs (toSample (offBeat), s);
                    endSounding (toSample (offBeat), emit);
                    continue;
                }

                const auto at = toSample (nextStep);
                applyInputs (at, s);

                if (sounding >= 0)
                    endSounding (at, emit);

                if (numHeld > 0)
                {
                    sounding = noteForStep (stepIndex++, s);
                    soundingVelocity = s.fixedVelocity > 0 ? s.fixedVelocity : pickedVelocity;
                    offBeat = nextStep + step * juce::jlimit (0.05, 1.0, (double) s.gate) - 1.0e-9;
                    emit (Event { at, sounding, soundingVelocity });
                }
                else
                {
                    stepIndex = 0;   // the next chord starts from its first note
                }

                nextStep += step;
            }

            applyInputs (std::numeric_limits<int>::max(), s);

            if (numHeld == 0 && sounding >= 0 && ! s.latch)   // keys let go: stop at once, not at the gate
                endSounding (numSamples - 1, emit);
        }

        int getNumHeld() const noexcept         { return numHeld; }
        int getSoundingNote() const noexcept    { return sounding; }

    private:
        static constexpr int maxHeld = 32, maxQueued = 128;

        struct Held { int note, velocity; bool down; };
        struct Input { int sampleOffset, note, velocity; };

        Held held[maxHeld] {};
        int numHeld = 0;
        Input queue[maxQueued] {};
        int numQueued = 0;

        int sounding = -1, soundingVelocity = 0, pickedVelocity = 100, stepIndex = 0;
        double offBeat = 0.0, expectedBeat = -1.0;
        juce::uint32 seed = 0x2545F491u;

        void applyInputs (int upToSample, const Settings&) noexcept;
        void applyLatch (const Settings&) noexcept;
        void press (int note, int velocity, bool latch) noexcept;
        void release (int note, bool latch) noexcept;
        int noteForStep (int index, const Settings&) noexcept;

        template <typename Emit>
        void endSounding (int at, Emit& emit)
        {
            emit (Event { at, sounding, 0 });
            sounding = -1;
        }

    };
}
