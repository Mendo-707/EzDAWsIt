/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Small DSP building blocks for the built-in effects and the Vocal Chain.
    Rules for everything here:
      - prepare() may allocate (message thread or initialise); process() never allocates, locks or logs.
      - Up to 2 channels. Parameters are plain values set before each block (from AutomatableParameters).
      - Stable at any sample rate from 22.05 to 192 kHz, and never produce NaN/inf for finite input.
*/

#pragma once

#include "../Core/Common.h"
#include <array>
#include <cmath>

namespace ez::dsp
{
    constexpr float pi = juce::MathConstants<float>::pi;

    inline float dbToGain (float db) noexcept    { return db <= -120.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) noexcept     { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }

    /** Coefficient for a one-pole smoother reaching ~63% in `ms`. */
    inline float onePoleCoeff (float ms, double sampleRate) noexcept
    {
        return ms <= 0.0f ? 0.0f : std::exp (-1.0f / (ms * 0.001f * (float) sampleRate));
    }

    /** Flushes tiny values (denormals) to zero. */
    inline float flush (float x) noexcept        { return std::abs (x) < 1.0e-15f ? 0.0f : x; }

    //==============================================================================
    /** RBJ cookbook biquad, transposed direct form II, per channel state. */
    struct Biquad
    {
        enum class Type { lowPass, highPass, bandPass, peak, lowShelf, highShelf };

        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<float, 2> z1 {}, z2 {};

        void reset() noexcept       { z1 = {}; z2 = {}; }

        void set (Type type, double sampleRate, float freq, float q, float gainDb = 0.0f) noexcept
        {
            freq = juce::jlimit (10.0f, (float) (sampleRate * 0.49), freq);
            q = juce::jmax (0.05f, q);
            const auto A = std::pow (10.0f, gainDb / 40.0f);
            const auto w0 = 2.0f * pi * freq / (float) sampleRate;
            const auto cw = std::cos (w0), sw = std::sin (w0);
            const auto alpha = sw / (2.0f * q);
            float nb0 = 1, nb1 = 0, nb2 = 0, na0 = 1, na1 = 0, na2 = 0;

            switch (type)
            {
                case Type::lowPass:   nb0 = (1 - cw) / 2; nb1 = 1 - cw; nb2 = (1 - cw) / 2; na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha; break;
                case Type::highPass:  nb0 = (1 + cw) / 2; nb1 = -(1 + cw); nb2 = (1 + cw) / 2; na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha; break;
                case Type::bandPass:  nb0 = alpha; nb1 = 0; nb2 = -alpha; na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha; break;
                case Type::peak:      nb0 = 1 + alpha * A; nb1 = -2 * cw; nb2 = 1 - alpha * A; na0 = 1 + alpha / A; na1 = -2 * cw; na2 = 1 - alpha / A; break;
                case Type::lowShelf:
                {
                    const auto s = 2 * std::sqrt (A) * alpha;
                    nb0 = A * ((A + 1) - (A - 1) * cw + s); nb1 = 2 * A * ((A - 1) - (A + 1) * cw); nb2 = A * ((A + 1) - (A - 1) * cw - s);
                    na0 = (A + 1) + (A - 1) * cw + s; na1 = -2 * ((A - 1) + (A + 1) * cw); na2 = (A + 1) + (A - 1) * cw - s;
                    break;
                }
                case Type::highShelf:
                {
                    const auto s = 2 * std::sqrt (A) * alpha;
                    nb0 = A * ((A + 1) + (A - 1) * cw + s); nb1 = -2 * A * ((A - 1) + (A + 1) * cw); nb2 = A * ((A + 1) + (A - 1) * cw - s);
                    na0 = (A + 1) - (A - 1) * cw + s; na1 = 2 * ((A - 1) - (A + 1) * cw); na2 = (A + 1) - (A - 1) * cw - s;
                    break;
                }
            }

            b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
        }

        float process (float x, int ch) noexcept
        {
            const auto y = b0 * x + z1[(size_t) ch];
            z1[(size_t) ch] = flush (b1 * x - a1 * y + z2[(size_t) ch]);
            z2[(size_t) ch] = flush (b2 * x - a2 * y);
            return y;
        }
    };

    //==============================================================================
    /** Peak envelope with separate attack and release (linear gain in, linear level out). */
    struct Envelope
    {
        float attack = 0, release = 0, level = 0;

        void setTimes (float attackMs, float releaseMs, double sampleRate) noexcept
        {
            attack = onePoleCoeff (attackMs, sampleRate);
            release = onePoleCoeff (releaseMs, sampleRate);
        }

        float process (float input) noexcept
        {
            const auto x = std::abs (input);
            const auto c = x > level ? attack : release;
            level = flush (c * level + (1.0f - c) * x);
            return level;
        }
    };

    //==============================================================================
    /** Noise gate: below the threshold the sound drops by `rangeDb` (a full gate at -80). */
    struct Gate
    {
        float thresholdDb = -50, rangeDb = -80, attackMs = 1, holdMs = 50, releaseMs = 120;
        bool smoothInDb = false;   // glide in decibels: shuts all the way within the release (before a big drive)

        void prepare (double sr) noexcept      { sampleRate = sr; env.level = 0; gain = 1; gainDb = 0; holdLeft = 0; }

        void process (float* const* ch, int numChannels, int numSamples) noexcept
        {
            env.setTimes (0.5f, 30.0f, sampleRate);
            const auto threshold = dbToGain (thresholdDb), floor = dbToGain (rangeDb);
            const auto open = onePoleCoeff (attackMs, sampleRate), close = onePoleCoeff (releaseMs, sampleRate);
            const auto hold = (int) (holdMs * 0.001f * (float) sampleRate);

            for (int i = 0; i < numSamples; ++i)
            {
                float peak = 0;

                for (int c = 0; c < numChannels; ++c)
                    peak = juce::jmax (peak, std::abs (ch[c][i]));

                const auto level = env.process (peak);

                const bool above = level >= threshold;

                if (above)                              holdLeft = hold;
                else if (holdLeft > 0)                  --holdLeft;

                const auto isOpen = above || holdLeft > 0;   // (hold 0: open exactly while above)

                if (smoothInDb)
                {
                    const auto targetDb = isOpen ? 0.0f : rangeDb;
                    const auto c = targetDb > gainDb ? open : close;
                    gainDb = c * gainDb + (1.0f - c) * targetDb;
                    gain = gainDb <= rangeDb + 0.5f ? floor : dbToGain (gainDb);
                }
                else
                {
                    const auto target = isOpen ? 1.0f : floor;
                    const auto c = target > gain ? open : close;
                    gain = flush (c * gain + (1.0f - c) * target);
                }

                for (int k = 0; k < numChannels; ++k)
                    ch[k][i] *= gain;
            }
        }

        float getGain() const noexcept         { return gain; }

    private:
        double sampleRate = 44100;
        Envelope env;
        float gain = 1, gainDb = 0;
        int holdLeft = 0;
    };

    //==============================================================================
    /** Feed-forward compressor, stereo linked, soft knee, with makeup gain. */
    struct Compressor
    {
        float thresholdDb = -18, ratio = 3, attackMs = 10, releaseMs = 120, kneeDb = 6, makeupDb = 0;

        void prepare (double sr) noexcept      { sampleRate = sr; env.level = 0; reductionDb = 0; }

        float computeGainDb (float levelDb) const noexcept
        {
            const auto over = levelDb - thresholdDb;
            const auto slope = 1.0f - 1.0f / juce::jmax (1.0f, ratio);

            if (kneeDb > 0 && std::abs (over) < kneeDb * 0.5f)
                return -slope * (over + kneeDb * 0.5f) * (over + kneeDb * 0.5f) / (2.0f * kneeDb);

            return over > 0 ? -slope * over : 0.0f;
        }

        void process (float* const* ch, int numChannels, int numSamples) noexcept
        {
            env.setTimes (attackMs, releaseMs, sampleRate);
            const auto makeup = dbToGain (makeupDb);
            float maxReduction = 0;

            for (int i = 0; i < numSamples; ++i)
            {
                float peak = 0;

                for (int c = 0; c < numChannels; ++c)
                    peak = juce::jmax (peak, std::abs (ch[c][i]));

                const auto gainDb = computeGainDb (gainToDb (env.process (peak)));
                maxReduction = juce::jmin (maxReduction, gainDb);
                const auto g = dbToGain (gainDb) * makeup;

                for (int c = 0; c < numChannels; ++c)
                    ch[c][i] *= g;
            }

            reductionDb = maxReduction;
        }

        float getReductionDb() const noexcept  { return reductionDb; }   // <= 0, the last block's most

    private:
        double sampleRate = 44100;
        Envelope env;
        float reductionDb = 0;
    };

    //==============================================================================
    /** De-esser: turns down only the highs above `freq` when they get louder than the threshold. */
    struct DeEsser
    {
        float freq = 6000, thresholdDb = -30, maxReductionDb = -12;

        void prepare (double sr) noexcept      { sampleRate = sr; split.reset(); detect.reset(); env.level = 0; lastFreq = -1; }

        void process (float* const* ch, int numChannels, int numSamples) noexcept
        {
            if (freq != lastFreq)
            {
                split.set (Biquad::Type::lowPass, sampleRate, freq * 0.8f, 0.707f);   // highs = input - lows: an exact split
                detect.set (Biquad::Type::bandPass, sampleRate, freq * 1.2f, 1.2f);
                lastFreq = freq;
            }

            env.setTimes (0.5f, 60.0f, sampleRate);
            const auto threshold = dbToGain (thresholdDb), floor = dbToGain (maxReductionDb);
            float most = 1;

            for (int i = 0; i < numSamples; ++i)
            {
                float side = 0;

                for (int c = 0; c < numChannels; ++c)
                    side = juce::jmax (side, std::abs (detect.process (ch[c][i], c)));

                const auto level = env.process (side);
                const auto g = level > threshold ? juce::jmax (floor, threshold / level) : 1.0f;
                most = juce::jmin (most, g);

                for (int c = 0; c < numChannels; ++c)
                {
                    const auto high = ch[c][i] - split.process (ch[c][i], c);
                    ch[c][i] -= high * (1.0f - g);   // the lows and mids pass untouched
                }
            }

            reductionDb = gainToDb (most);
        }

        float getReductionDb() const noexcept  { return reductionDb; }

    private:
        double sampleRate = 44100;
        Biquad split, detect;
        Envelope env;
        float lastFreq = -1, reductionDb = 0;
    };

    //==============================================================================
    /** Saturation: drive into a soft curve, a tone control, a dry/wet mix, output trim. */
    struct Saturator
    {
        enum class Curve { tape, tube, hard };
        float driveDb = 6, toneHz = 12000, mix = 1, outputDb = 0;
        Curve curve = Curve::tape;

        void prepare (double sr) noexcept      { sampleRate = sr; tone.reset(); dc.reset(); lastTone = -1; }

        static float shape (float x, Curve c) noexcept
        {
            switch (c)
            {
                case Curve::tube:  return x >= 0 ? 1.0f - std::exp (-x) : -(1.0f - std::exp (x)) * 0.8f;   // a little uneven: warmer
                case Curve::hard:  return juce::jlimit (-1.0f, 1.0f, x * (1.5f - 0.5f * x * x * (std::abs (x) < 1.0f ? 1.0f : 0.0f)));
                case Curve::tape:  break;
            }

            return std::tanh (x);
        }

        void process (float* const* ch, int numChannels, int numSamples) noexcept
        {
            if (toneHz != lastTone)
            {
                tone.set (Biquad::Type::lowPass, sampleRate, toneHz, 0.707f);
                dc.set (Biquad::Type::highPass, sampleRate, 15.0f, 0.707f);
                lastTone = toneHz;
            }

            const auto drive = dbToGain (driveDb);
            const auto out = dbToGain (outputDb) / juce::jmax (1.0f, std::sqrt (drive));   // keeps louder drive from simply being louder
            const auto wet = juce::jlimit (0.0f, 1.0f, mix);

            for (int c = 0; c < numChannels; ++c)
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto x = ch[c][i];
                    const auto y = dc.process (tone.process (shape (x * drive, curve), c), c) * out;
                    ch[c][i] = x + (y - x) * wet;
                }
        }

    private:
        double sampleRate = 44100;
        Biquad tone, dc;
        float lastTone = -1;
    };

    //==============================================================================
    /** Brick-wall limiter with a short look-ahead: the gain is down before a peak arrives.
        The loudest peak in the look-ahead window is a true sliding maximum (a monotonic queue). */
    struct Limiter
    {
        float ceilingDb = -1, releaseMs = 80, inputDb = 0;

        void prepare (double sr, int)
        {
            sampleRate = sr;
            lookahead = juce::jmax (1, (int) std::ceil (0.0015 * sr));
            delay.setSize (2, lookahead + 1, false, true, false);
            delay.clear();
            queueValue.assign ((size_t) lookahead + 2, 0.0f);
            queueIndex.assign ((size_t) lookahead + 2, 0);
            head = tail = 0;
            counter = 0;
            writePos = 0;
            gain = 1;
        }

        int getLatencySamples() const noexcept  { return lookahead; }

        void process (float* const* ch, int numChannels, int numSamples) noexcept
        {
            if (queueValue.empty())
                return;

            const auto ceiling = dbToGain (ceilingDb), input = dbToGain (inputDb);
            const auto release = onePoleCoeff (releaseMs, sampleRate);
            const auto attackStep = 1.0f / (float) lookahead;
            const auto size = delay.getNumSamples();
            const auto capacity = (int) queueValue.size();
            float most = 1;

            for (int i = 0; i < numSamples; ++i)
            {
                float peak = 0;

                for (int c = 0; c < numChannels; ++c)
                    peak = juce::jmax (peak, std::abs (ch[c][i] * input));

                // Sliding maximum over the samples now in the delay (this one and the `lookahead` before it)
                while (tail != head && queueValue[(size_t) ((tail - 1 + capacity) % capacity)] <= peak)
                    tail = (tail - 1 + capacity) % capacity;

                queueValue[(size_t) tail] = peak;
                queueIndex[(size_t) tail] = counter;
                tail = (tail + 1) % capacity;

                while (queueIndex[(size_t) head] <= counter - (juce::int64) lookahead - 1)
                    head = (head + 1) % capacity;

                ++counter;
                const auto windowPeak = queueValue[(size_t) head];
                const auto target = windowPeak > ceiling ? ceiling / windowPeak : 1.0f;

                // Down in a straight line over at most the look-ahead (a peak takes that long to come out)
                if (target < gain)  gain = juce::jmax (target, gain - attackStep);
                else                gain = flush (release * gain + (1.0f - release) * target);

                most = juce::jmin (most, gain);
                const auto readPos = (writePos + 1) % size;

                for (int c = 0; c < numChannels && c < 2; ++c)
                {
                    delay.setSample (c, writePos, ch[c][i] * input);
                    ch[c][i] = juce::jlimit (-ceiling, ceiling, delay.getSample (c, readPos) * gain);   // the last safety net
                }

                writePos = readPos;
            }

            reductionDb = gainToDb (most);
        }

        float getReductionDb() const noexcept  { return reductionDb; }

    private:
        double sampleRate = 44100;
        juce::AudioBuffer<float> delay;
        std::vector<float> queueValue;
        std::vector<juce::int64> queueIndex;
        int lookahead = 64, writePos = 0, head = 0, tail = 0;
        juce::int64 counter = 0;
        float gain = 1, reductionDb = 0;
    };

    //==============================================================================
    /** Three-band Linkwitz-Riley (4th order) crossover: low / mid / high add back up to the input with
        a flat level (an all-pass: the low band also goes through the upper split's all-pass, so the
        three stay in phase where they overlap). */
    struct Crossover3
    {
        void set (double sampleRate, float lowHz, float highHz) noexcept
        {
            constexpr auto q = 0.70710678f;   // two Butterworths in a row = Linkwitz-Riley

            for (auto* f : { &lowLp1, &lowLp2 })        f->set (Biquad::Type::lowPass, sampleRate, lowHz, q);
            for (auto* f : { &lowHp1, &lowHp2 })        f->set (Biquad::Type::highPass, sampleRate, lowHz, q);
            for (auto* f : { &highLp1, &highLp2, &apLp1, &apLp2 })   f->set (Biquad::Type::lowPass, sampleRate, highHz, q);
            for (auto* f : { &highHp1, &highHp2, &apHp1, &apHp2 })   f->set (Biquad::Type::highPass, sampleRate, highHz, q);
        }

        void reset() noexcept
        {
            for (auto* f : { &lowLp1, &lowLp2, &lowHp1, &lowHp2, &highLp1, &highLp2, &highHp1, &highHp2, &apLp1, &apLp2, &apHp1, &apHp2 })
                f->reset();
        }

        void split (float x, int ch, float& low, float& mid, float& high) noexcept
        {
            const auto lowRaw = lowLp2.process (lowLp1.process (x, ch), ch);
            const auto rest = lowHp2.process (lowHp1.process (x, ch), ch);
            mid = highLp2.process (highLp1.process (rest, ch), ch);
            high = highHp2.process (highHp1.process (rest, ch), ch);
            low = apLp2.process (apLp1.process (lowRaw, ch), ch) + apHp2.process (apHp1.process (lowRaw, ch), ch);   // the upper split's all-pass
        }

    private:
        Biquad lowLp1, lowLp2, lowHp1, lowHp2, highLp1, highLp2, highHp1, highHp2, apLp1, apLp2, apHp1, apHp2;
    };

    //==============================================================================
    /** Gain, stereo width (0 mono .. 2 extra wide), mono switch, phase flip. */
    struct Utility
    {
        float gainDb = 0, width = 1;
        bool mono = false, flipLeft = false, flipRight = false;

        void process (float* const* ch, int numChannels, int numSamples) noexcept
        {
            const auto g = dbToGain (gainDb);
            const auto w = mono ? 0.0f : juce::jlimit (0.0f, 2.0f, width);

            for (int i = 0; i < numSamples; ++i)
            {
                auto l = ch[0][i] * (flipLeft ? -g : g);

                if (numChannels < 2)
                {
                    ch[0][i] = l;
                    continue;
                }

                auto r = ch[1][i] * (flipRight ? -g : g);
                const auto mid = (l + r) * 0.5f, side = (l - r) * 0.5f * w;
                ch[0][i] = mid + side;
                ch[1][i] = mid - side;
            }
        }
    };

    //==============================================================================
    /** Tremolo / auto-pan: a slow LFO on the level (or between left and right). */
    struct Tremolo
    {
        float rateHz = 4, depth = 0.5f;
        bool autoPan = false;

        void prepare (double sr) noexcept      { sampleRate = sr; phase = 0; }

        void process (float* const* ch, int numChannels, int numSamples) noexcept
        {
            const auto step = juce::jlimit (0.01f, 40.0f, rateHz) / (float) sampleRate;
            const auto d = juce::jlimit (0.0f, 1.0f, depth);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto lfo = 0.5f + 0.5f * std::sin (2.0f * pi * phase);   // 0..1
                phase += step;

                if (phase >= 1.0f)
                    phase -= 1.0f;

                if (autoPan && numChannels > 1)
                {
                    ch[0][i] *= 1.0f - d * lfo;
                    ch[1][i] *= 1.0f - d * (1.0f - lfo);
                }
                else
                {
                    const auto g = 1.0f - d * lfo;

                    for (int c = 0; c < numChannels; ++c)
                        ch[c][i] *= g;
                }
            }
        }

    private:
        double sampleRate = 44100;
        float phase = 0;
    };

    //==============================================================================
    /** Pitch correction ("Tune"): finds the sung note (YIN, on a copy decimated to about 22 kHz) and pulls
        it to the nearest note of a scale with a two-tap delay-line pitch shifter.
          amount   0..1: how far to pull (1 = all the way); 0 = the sound passes (still delayed, see below)
          speedMs  how fast it gets there (fast = the robotic hip hop effect)
          root, scale: the notes it pulls to (scale: chromatic, major, minor)
        Always delays the sound by getLatencySamples() (the shifter's average delay), corrected or not,
        so switching between them never flams, and the host can compensate.
        The YIN search is spread over the hop (a few lags per analysis sample): no CPU spikes. */
    struct PitchCorrector
    {
        enum class Scale { chromatic, major, minor };
        float amount = 1, speedMs = 40;
        int root = 0;
        Scale scale = Scale::chromatic;

        void prepare (double sr)
        {
            sampleRate = sr;
            decimation = juce::jmax (1, juce::roundToInt (sr / 22050.0));
            analysisRate = sr / decimation;
            analysis.assign ((size_t) analysisSize, 0.0f);
            snapshot.assign ((size_t) (window + maxLag + 2), 0.0f);
            diff.assign ((size_t) maxLag + 2, 0.0f);
            grain = pendingGrain = (float) (sr * 0.03);   // 30 ms grains to start
            maxGrain = (float) (sr * 0.04);
            latency = (int) std::ceil (maxGrain * 0.5f) + 2;   // the taps' average delay, whatever the grain
            const auto lineLength = (int) std::ceil (sr * 0.1) + 4;
            line.setSize (2, lineLength, false, true, false);
            line.clear();
            writePos = analysisPos = hopCount = decimCount = 0;
            decimAcc = 0;
            ratio = targetRatio = 1;
            tapPhase = 0.5f;   // tap 0 at the centre delay: the same as the dry path
            detectedHz = 0;
            wet = 0;
            wetSmooth = onePoleCoeff (15.0f, sr);
            searching = false;
        }

        int getLatencySamples() const noexcept     { return latency; }
        float getDetectedHz() const noexcept       { return detectedHz; }

        void process (float* const* ch, int numChannels, int numSamples) noexcept
        {
            if (analysis.empty())
                return;

            const auto lineLength = line.getNumSamples();
            const auto smooth = onePoleCoeff (juce::jmax (1.0f, speedMs), sampleRate);
            const auto pull = juce::jlimit (0.0f, 1.0f, amount);
            const auto lagsPerStep = maxLag / hop + 2;

            for (int i = 0; i < numSamples; ++i)
            {
                // 1. Analysis on a mono, decimated copy; the search runs a few lags at a time
                const auto mono = numChannels > 1 ? (ch[0][i] + ch[1][i]) * 0.5f : ch[0][i];
                decimAcc += mono;

                if (++decimCount >= decimation)
                {
                    analysis[(size_t) analysisPos] = decimAcc / (float) decimation;
                    decimAcc = 0;
                    decimCount = 0;
                    analysisPos = (analysisPos + 1) % analysisSize;

                    if (++hopCount >= hop)
                    {
                        hopCount = 0;
                        startSearch();
                    }

                    if (searching)
                        searchSome (lagsPerStep, pull);
                }

                ratio = smooth * ratio + (1.0f - smooth) * (pull > 0 ? targetRatio : 1.0f);

                // In tune (or unvoiced): the dry sound, delayed like the shifted one
                const auto wantWet = std::abs (ratio - 1.0f) > 0.0015f ? 1.0f : 0.0f;
                wet = wetSmooth * wet + (1.0f - wetSmooth) * wantWet;

                for (int c = 0; c < numChannels && c < 2; ++c)
                    line.setSample (c, writePos, ch[c][i]);

                // 2. Shifting: two taps sweeping through the grain, half a grain apart, crossfaded, both
                //    centred on the fixed latency. A new grain size is taken only when a tap is at its
                //    jump (silent there): the other tap is then exactly at the centre, which no grain moves
                const auto before = tapPhase;
                tapPhase += (1.0f - ratio) / grain;
                tapPhase -= std::floor (tapPhase);

                if (pendingGrain != grain && (int) std::floor (before * 2.0f) != (int) std::floor (tapPhase * 2.0f))
                    grain = pendingGrain;

                for (int c = 0; c < numChannels && c < 2; ++c)
                {
                    const auto dry = readLine (c, (float) writePos - (float) latency, lineLength);
                    float out = dry;

                    if (wet > 1.0e-4f)
                    {
                        float shifted = 0;

                        for (int tap = 0; tap < 2; ++tap)
                        {
                            auto p = tapPhase + 0.5f * (float) tap;
                            p -= std::floor (p);
                            const auto fade = std::sin (pi * p);   // zero at the jumps
                            shifted += readLine (c, (float) writePos - ((float) latency - grain * 0.5f + p * grain), lineLength) * fade * fade;
                        }

                        out = dry + (shifted - dry) * wet;
                    }

                    ch[c][i] = out;
                }

                writePos = (writePos + 1) % lineLength;
            }
        }

    private:
        static constexpr int analysisSize = 1024, hop = 128, window = 512;
        static constexpr int maxLag = 400;   // ~55 Hz at the ~22 kHz analysis rate
        double sampleRate = 44100, analysisRate = 44100;
        int decimation = 2, decimCount = 0, latency = 0;
        std::vector<float> analysis, snapshot, diff;
        juce::AudioBuffer<float> line;
        int writePos = 0, analysisPos = 0, hopCount = 0;
        float decimAcc = 0, ratio = 1, targetRatio = 1, tapPhase = 0.5f, grain = 1323, pendingGrain = 1323, maxGrain = 1764,
              detectedHz = 0, wet = 0, wetSmooth = 0;

        // The search in progress
        bool searching = false;
        int searchLag = 1, best = -1, minLag = 2;
        float running = 0;

        float readLine (int c, float pos, int length) const noexcept
        {
            while (pos < 0)
                pos += (float) length;

            const auto i0 = (int) pos % length;
            const auto i1 = (i0 + 1) % length;
            const auto frac = pos - std::floor (pos);
            return line.getSample (c, i0) + (line.getSample (c, i1) - line.getSample (c, i0)) * frac;
        }

        bool inScale (int note) const noexcept
        {
            static constexpr bool majorSteps[12] { true, false, true, false, true, true, false, true, false, true, false, true };
            static constexpr bool minorSteps[12] { true, false, true, true, false, true, false, true, true, false, true, false };
            const auto degree = ((note - root) % 12 + 12) % 12;

            switch (scale)
            {
                case Scale::major:     return majorSteps[degree];
                case Scale::minor:     return minorSteps[degree];
                case Scale::chromatic: break;
            }

            return true;
        }

        void startSearch() noexcept
        {
            // The newest window + maxLag samples, oldest first, so the search sees what was just sung
            const auto length = window + maxLag;
            const auto start = (analysisPos - length + analysisSize * 2) % analysisSize;
            float energy = 0;

            for (int j = 0; j < length; ++j)
            {
                const auto x = analysis[(size_t) ((start + j) % analysisSize)];
                snapshot[(size_t) j] = x;

                if (j < window)
                    energy += x * x;
            }

            if (energy < 1.0e-6f * (float) window)
            {
                targetRatio = 1;   // too quiet to tell (-60 dB): leave it
                detectedHz = 0;
                searching = false;
                return;
            }

            searching = true;
            searchLag = 1;
            best = -1;
            running = 0;
            minLag = juce::jmax (2, (int) (analysisRate / 1000.0));   // up to 1 kHz
        }

        void searchSome (int lags, float pull) noexcept
        {
            for (int n = 0; n < lags && searching; ++n, ++searchLag)
            {
                if (searchLag > maxLag)
                {
                    finish (pull);
                    return;
                }

                float d = 0;

                for (int j = 0; j < window; ++j)
                {
                    const auto a = snapshot[(size_t) j], b = snapshot[(size_t) (j + searchLag)];
                    d += (a - b) * (a - b);
                }

                // YIN: cumulative mean normalised difference; the first dip under 0.15, walked to its bottom
                running += d;
                diff[(size_t) searchLag] = running > 0 ? d * (float) searchLag / running : 1.0f;

                if (best < 0)
                {
                    if (searchLag > minLag && diff[(size_t) searchLag] < 0.15f)
                        best = searchLag;
                }
                else if (diff[(size_t) searchLag] < diff[(size_t) best])
                {
                    best = searchLag;
                }
                else
                {
                    finish (pull);
                    return;
                }
            }
        }

        void finish (float pull) noexcept
        {
            searching = false;

            if (best <= 0 || best >= maxLag)
            {
                targetRatio = 1;   // unvoiced (breath, s) or too low to tell: leave it
                detectedHz = 0;
                return;
            }

            auto lag = (float) best;

            if (best > 1)
            {
                const auto a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
                const auto denom = a - 2 * b + c;

                if (std::abs (denom) > 1.0e-9f && searchLag >= best + 1)
                    lag += 0.5f * (a - c) / denom;
            }

            detectedHz = (float) analysisRate / lag;

            // Pitch-synchronous grains: the taps half a grain apart are then a whole number of periods
            // apart, in phase, so their crossfade never cancels (at 100 Hz a fixed 30 ms grain would)
            const auto period = (float) sampleRate / detectedHz;
            const auto periods = juce::jmax (1, juce::roundToInt ((float) sampleRate * 0.015f / period));
            const auto wanted = juce::jlimit ((float) sampleRate * 0.01f, maxGrain, 2.0f * (float) periods * period);

            if (std::abs (wanted - pendingGrain) > 1.0f)
                pendingGrain = wanted;   // taken at the next tap jump (process)
            const auto midi = 69.0f + 12.0f * std::log2 (detectedHz / 440.0f);
            auto nearest = (int) std::round (midi);

            for (int step = 0; step < 6 && ! inScale (nearest); ++step)
                nearest += (midi > (float) nearest ? 1 : -1) * (step % 2 == 0 ? 1 : -1) * (step + 1);

            if (! inScale (nearest))
                nearest = (int) std::round (midi);

            const auto correction = ((float) nearest - midi) * pull;   // semitones
            targetRatio = std::pow (2.0f, juce::jlimit (-2.0f, 2.0f, correction) / 12.0f);
        }
    };

    //==============================================================================
    /** A guitar or bass amp: gate, tight (a high-pass before the drive), one or two drive stages run at
        4x the sample rate (so high gain doesn't fizz with aliasing), a tone stack, presence and a
        speaker cabinet. Blend mixes the plain input back in (a bass DI under the amp). */
    struct Amp
    {
        enum class Mode { clean, crunch, punk, lead, metal, bass };
        enum class Cab { off, closed4x12, open2x12, combo1x12, bass8x10 };

        Mode mode = Mode::crunch;
        Cab cab = Cab::closed4x12;
        float gain = 5;          // 0..10
        float gateDb = -96;      // -96 and below: no gate
        float tightHz = 60;
        float bassDb = 0, midDb = 0, trebleDb = 0, presenceDb = 0;
        float volumeDb = 0;
        float blend = 0;         // 0..1: the plain input mixed back in

        static constexpr int factor = 4;
        static constexpr float gateOff = -95.0f;

        struct Voice { float minDb, maxDb; int stages; float midHz, midDb, tightScale, bias, fizzHz, trim; };

        static const Voice& voiceFor (Mode m) noexcept
        {
            static const Voice voices[] =
            {
                {  0, 20, 1,   0, 0, 0.7f, 0.10f, 9000, 0.22f },   // clean
                {  6, 36, 1, 800, 3, 1.0f, 0.30f, 7500, 0.16f },   // crunch
                { 12, 42, 2, 900, 4, 1.2f, 0.25f, 7000, 0.13f },   // punk
                { 18, 50, 2, 750, 6, 1.3f, 0.20f, 6500, 0.12f },   // lead
                { 24, 58, 2, 720, 8, 1.8f, 0.10f, 6500, 0.12f },   // metal
                {  0, 30, 1,   0, 0, 0.4f, 0.15f, 6000, 0.22f },   // bass
            };

            return voices[juce::jlimit (0, 5, (int) m)];
        }

        void prepare (double sr) noexcept
        {
            sampleRate = sr;
            gateDsp.prepare (sr);
            gateDsp.attackMs = 0.5f;
            gateDsp.holdMs = 30.0f;
            gateDsp.releaseMs = 60.0f;
            gateDsp.rangeDb = -120.0f;   // fully shut: the drive after it adds up to 60 dB
            gateDsp.smoothInDb = true;

            // 8th-order Butterworth either side of the drive, at 4x the rate, below the original Nyquist
            static constexpr float q[4] = { 0.5098f, 0.6013f, 0.9000f, 2.5629f };
            const auto cutoff = juce::jmin (20000.0f, (float) (sr * 0.45));

            for (int i = 0; i < 4; ++i)
            {
                up[(size_t) i].set (Biquad::Type::lowPass, sr * factor, cutoff, q[i]);
                down[(size_t) i].set (Biquad::Type::lowPass, sr * factor, cutoff, q[i]);
                up[(size_t) i].reset();
                down[(size_t) i].reset();
            }

            for (auto* f : { &tight, &preMid, &fizz, &dcBlock, &bassShelf, &midPeak, &trebleShelf, &presence })
                f->reset();

            for (auto& f : cabFilters)
                f.reset();

            lastKey = {};
            driveNow1 = driveNow2 = -1;
        }

        void process (float* const* ch, int numChannels, int numSamples) noexcept
        {
            if (numSamples <= 0 || numChannels <= 0)
                return;

            updateFilters();

            if (gateDb > gateOff)
            {
                gateDsp.thresholdDb = gateDb;
                gateDsp.process (ch, numChannels, numSamples);
            }

            const auto& v = voiceFor (mode);
            const auto driveDb = v.minDb + (v.maxDb - v.minDb) * juce::jlimit (0.0f, 10.0f, gain) / 10.0f;
            const auto target1 = dbToGain (v.stages > 1 ? driveDb * 0.55f : driveDb);
            const auto target2 = dbToGain (driveDb * 0.45f + 6.0f);

            if (driveNow1 < 0)
            {
                driveNow1 = target1;
                driveNow2 = target2;
            }

            const auto out = v.trim * dbToGain (volumeDb);
            const auto mix = juce::jlimit (0.0f, 1.0f, blend);
            const auto bias = v.bias, biasOffset = std::tanh (v.bias);
            const bool twoStages = v.stages > 1;
            const auto step1 = (target1 - driveNow1) / (float) numSamples, step2 = (target2 - driveNow2) / (float) numSamples;

            for (int c = 0; c < numChannels && c < 2; ++c)
            {
                auto g1 = driveNow1, g2 = driveNow2;   // the same ramp for each channel

                for (int i = 0; i < numSamples; ++i)
                {
                    g1 += step1;
                    g2 += step2;

                    const auto dry = ch[c][i];
                    auto x = tight.process (dry, c);

                    if (hasPreMid)
                        x = preMid.process (x, c);

                    float y = 0;

                    for (int k = 0; k < factor; ++k)
                    {
                        auto u = k == 0 ? x * (float) factor : 0.0f;

                        for (auto& f : up)
                            u = f.process (u, c);

                        u = std::tanh (u * g1 + bias) - biasOffset;

                        if (twoStages)
                            u = std::tanh (u * g2 + bias * 0.5f) - std::tanh (bias * 0.5f);

                        for (auto& f : down)
                            u = f.process (u, c);

                        y = u;   // the last of the four: one in four kept
                    }

                    y = fizz.process (y, c);
                    y = dcBlock.process (y, c);
                    y = bassShelf.process (y, c);
                    y = midPeak.process (y, c);
                    y = trebleShelf.process (y, c);
                    y = presence.process (y, c);

                    for (int f = 0; f < numCabFilters; ++f)
                        y = cabFilters[(size_t) f].process (y, c);

                    y *= out;
                    ch[c][i] = y + (dry * dbToGain (volumeDb) - y) * mix;
                }
            }

            driveNow1 = target1;
            driveNow2 = target2;
        }

        float getGateGain() const noexcept   { return gateDb > gateOff ? gateDsp.getGain() : 1.0f; }

    private:
        double sampleRate = 44100;
        Gate gateDsp;
        std::array<Biquad, 4> up, down;
        Biquad tight, preMid, fizz, dcBlock, bassShelf, midPeak, trebleShelf, presence;
        std::array<Biquad, 6> cabFilters;
        int numCabFilters = 0;
        bool hasPreMid = false;
        float driveNow1 = -1, driveNow2 = -1;
        std::array<float, 8> lastKey {};

        void updateFilters() noexcept
        {
            const std::array<float, 8> key { (float) mode, (float) cab, tightHz, bassDb, midDb, trebleDb, presenceDb, 1.0f };

            if (key == lastKey)
                return;

            lastKey = key;
            const auto& v = voiceFor (mode);
            const bool isBass = mode == Mode::bass;

            tight.set (Biquad::Type::highPass, sampleRate, juce::jlimit (15.0f, 600.0f, tightHz * v.tightScale), 0.707f);
            hasPreMid = v.midDb > 0;

            if (hasPreMid)
                preMid.set (Biquad::Type::peak, sampleRate, v.midHz, 0.9f, v.midDb);

            fizz.set (Biquad::Type::lowPass, sampleRate, v.fizzHz, 0.707f);
            dcBlock.set (Biquad::Type::highPass, sampleRate, 20.0f, 0.707f);
            bassShelf.set (Biquad::Type::lowShelf, sampleRate, isBass ? 80.0f : 110.0f, 0.707f, juce::jlimit (-15.0f, 15.0f, bassDb));
            midPeak.set (Biquad::Type::peak, sampleRate, isBass ? 500.0f : 700.0f, 0.8f, juce::jlimit (-15.0f, 15.0f, midDb));
            trebleShelf.set (Biquad::Type::highShelf, sampleRate, isBass ? 2000.0f : 2800.0f, 0.707f, juce::jlimit (-15.0f, 15.0f, trebleDb));
            presence.set (Biquad::Type::highShelf, sampleRate, isBass ? 3500.0f : 4500.0f, 0.707f, juce::jlimit (-15.0f, 15.0f, presenceDb));

            struct Stage { Biquad::Type type; float hz, q, db; };
            std::array<Stage, 6> stages {};
            int n = 0;
            const auto add = [&] (Biquad::Type t, float hz, float q, float db = 0.0f) { stages[(size_t) n++] = { t, hz, q, db }; };

            switch (cab)
            {
                case Cab::closed4x12:   // big, thumpy, a bite at 2.5 kHz, nothing fizzy above 6 kHz
                    add (Biquad::Type::highPass, 75, 0.7f);  add (Biquad::Type::peak, 110, 1.2f, 4.0f);  add (Biquad::Type::peak, 400, 1.0f, -3.0f);
                    add (Biquad::Type::peak, 2500, 1.5f, 4.0f);  add (Biquad::Type::lowPass, 5200, 0.7f);  add (Biquad::Type::lowPass, 6500, 0.6f);
                    break;
                case Cab::open2x12:
                    add (Biquad::Type::highPass, 70, 0.7f);  add (Biquad::Type::peak, 120, 1.0f, 2.0f);  add (Biquad::Type::peak, 1800, 1.2f, 3.0f);
                    add (Biquad::Type::lowPass, 6000, 0.7f);  add (Biquad::Type::lowPass, 7500, 0.6f);
                    break;
                case Cab::combo1x12:
                    add (Biquad::Type::highPass, 90, 0.7f);  add (Biquad::Type::peak, 150, 0.9f, 1.5f);  add (Biquad::Type::peak, 3000, 1.4f, 3.0f);
                    add (Biquad::Type::lowPass, 7000, 0.7f);  add (Biquad::Type::lowPass, 8500, 0.6f);
                    break;
                case Cab::bass8x10:
                    add (Biquad::Type::highPass, 40, 0.7f);  add (Biquad::Type::peak, 80, 1.0f, 3.0f);  add (Biquad::Type::peak, 800, 1.0f, -2.0f);
                    add (Biquad::Type::peak, 2000, 1.5f, 2.0f);  add (Biquad::Type::lowPass, 4000, 0.7f);  add (Biquad::Type::lowPass, 5000, 0.6f);
                    break;
                case Cab::off:
                    break;
            }

            for (int i = 0; i < n; ++i)
                cabFilters[(size_t) i].set (stages[(size_t) i].type, sampleRate, stages[(size_t) i].hz, stages[(size_t) i].q, stages[(size_t) i].db);

            if (n != numCabFilters)
                for (auto& f : cabFilters)
                    f.reset();

            numCabFilters = n;
        }
    };

    //==============================================================================
    /** The pitch of a sound, by YIN (de Cheveigne & Kawahara). 0 when there isn't a clear one.
        Allocation free if `scratch` holds maxLag + 2 floats. */
    inline float detectPitch (const float* data, int numSamples, double sampleRate, float minHz, float maxHz,
                              float* scratch, int scratchSize, float threshold = 0.15f) noexcept
    {
        if (data == nullptr || scratch == nullptr || sampleRate <= 0 || minHz <= 0 || maxHz <= minHz)
            return 0.0f;

        const auto minLag = juce::jmax (2, (int) std::floor (sampleRate / maxHz));
        const auto maxLag = juce::jmin (scratchSize - 2, (int) std::ceil (sampleRate / minHz));
        const auto window = numSamples - maxLag - 1;

        if (maxLag <= minLag + 2 || window < maxLag)
            return 0.0f;

        // Difference, then cumulative mean normalised
        scratch[0] = 1.0f;
        float running = 0;

        for (int lag = 1; lag <= maxLag; ++lag)
        {
            float sum = 0;

            for (int i = 0; i < window; ++i)
            {
                const auto d = data[i] - data[i + lag];
                sum += d * d;
            }

            running += sum;
            scratch[lag] = running > 0 ? sum * (float) lag / running : 1.0f;
        }

        // The first dip under the threshold, followed down to its bottom
        int best = -1;

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            if (scratch[lag] < threshold)
            {
                while (lag + 1 <= maxLag && scratch[lag + 1] < scratch[lag])
                    ++lag;

                best = lag;
                break;
            }
        }

        if (best < 0)
            return 0.0f;

        // Between samples: the bottom of a parabola through the three points
        auto exact = (float) best;

        if (best > 1 && best < maxLag)
        {
            const auto a = scratch[best - 1], b = scratch[best], c = scratch[best + 1];
            const auto denom = a - 2.0f * b + c;

            if (std::abs (denom) > 1.0e-9f)
                exact += juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / denom);
        }

        return (float) (sampleRate / exact);
    }
}
