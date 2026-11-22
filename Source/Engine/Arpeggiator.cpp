/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "Arpeggiator.h"

namespace ez
{
    void Arpeggiator::queueInput (int sampleOffset, int note, int velocity) noexcept
    {
        if (numQueued < maxQueued && juce::isPositiveAndBelow (note, 128))
            queue[numQueued++] = { sampleOffset, note, velocity };
    }

    void Arpeggiator::reset() noexcept
    {
        numHeld = 0;
        numQueued = 0;
        sounding = -1;
        stepIndex = 0;
        expectedBeat = -1.0;
    }

    void Arpeggiator::applyInputs (int upToSample, const Settings& s) noexcept
    {
        int used = 0;

        while (used < numQueued && queue[used].sampleOffset <= upToSample)
        {
            const auto& in = queue[used++];

            if (in.velocity > 0)
                press (in.note, in.velocity, s.latch);
            else
                release (in.note, s.latch);
        }

        // Shift what's left to the front (a handful at most, no allocation)
        for (int i = used; i < numQueued; ++i)
            queue[i - used] = queue[i];

        numQueued -= used;
    }

    void Arpeggiator::applyLatch (const Settings& s) noexcept
    {
        if (s.latch)
            return;

        // Latch switched off: let go of the notes no key is holding
        int kept = 0;

        for (int i = 0; i < numHeld; ++i)
            if (held[i].down)
                held[kept++] = held[i];

        numHeld = kept;
    }

    void Arpeggiator::press (int note, int velocity, bool latch) noexcept
    {
        // Latch: the first key of a new chord (nothing held down) replaces the latched one
        if (latch)
        {
            bool anyDown = false;

            for (int i = 0; i < numHeld; ++i)
                anyDown = anyDown || held[i].down;

            if (! anyDown)
                numHeld = 0;
        }

        for (int i = 0; i < numHeld; ++i)
        {
            if (held[i].note == note)
            {
                held[i].velocity = velocity;
                held[i].down = true;
                return;
            }
        }

        if (numHeld < maxHeld)
            held[numHeld++] = { note, velocity, true };
    }

    void Arpeggiator::release (int note, bool latch) noexcept
    {
        for (int i = 0; i < numHeld; ++i)
        {
            if (held[i].note != note)
                continue;

            if (latch)
            {
                held[i].down = false;   // keeps playing until the next chord
                return;
            }

            for (int j = i + 1; j < numHeld; ++j)   // keep the played order
                held[j - 1] = held[j];

            --numHeld;
            return;
        }
    }

    int Arpeggiator::noteForStep (int index, const Settings& s) noexcept
    {
        // The notes in play order (as played) or low to high (every other mode)
        Held order[maxHeld];

        for (int i = 0; i < numHeld; ++i)
            order[i] = held[i];

        if (s.mode != asPlayed)
            for (int i = 1; i < numHeld; ++i)   // insertion sort: a few notes, no allocation
                for (int j = i; j > 0 && order[j - 1].note > order[j].note; --j)
                    std::swap (order[j - 1], order[j]);

        const auto octaves = juce::jlimit (1, 4, s.octaves);
        const auto length = numHeld * octaves;   // every note in every octave, low to high
        int position = 0;

        switch (s.mode)
        {
            case down:
                position = length - 1 - index % length;
                break;

            case upDown:
            {
                // Up then down, without playing the top and bottom twice
                const auto period = juce::jmax (1, 2 * length - 2);
                const auto j = index % period;
                position = j < length ? j : period - j;
                break;
            }

            case random:
                seed = seed * 1664525u + 1013904223u;
                position = (int) ((seed >> 8) % (juce::uint32) length);
                break;

            case up:
            case asPlayed:
            default:
                position = index % length;
                break;
        }

        const auto& chosen = order[position % numHeld];
        pickedVelocity = chosen.velocity;
        return juce::jlimit (0, 127, chosen.note + 12 * (position / numHeld));
    }
}
