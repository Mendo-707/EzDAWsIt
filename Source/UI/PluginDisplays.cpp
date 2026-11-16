/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "PluginDisplays.h"
#include "Theme.h"
#include "Widgets.h"
#include <complex>

namespace ez::PluginDisplays
{
    //==============================================================================
    namespace Response
    {
        namespace
        {
            constexpr double fs = 48000.0;

            struct Coefficients { double b0, b1, b2, a0, a1, a2; };

            float magnitudeDb (const Coefficients& c, float hz)
            {
                const auto w = juce::MathConstants<double>::twoPi * juce::jlimit (1.0, fs * 0.49, (double) hz) / fs;
                const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
                const auto h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (c.a0 + c.a1 * z1 + c.a2 * z2);
                return (float) (20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (h))));
            }

            struct Shape
            {
                double w0, cosW, alpha, A;

                Shape (float f0, float q, float gainDb = 0.0f)
                {
                    w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, fs * 0.45, (double) f0) / fs;
                    cosW = std::cos (w0);
                    alpha = std::sin (w0) / (2.0 * juce::jmax (0.05, (double) q));
                    A = std::pow (10.0, gainDb / 40.0);
                }
            };
        }

        float peak (float hz, float f0, float gainDb, float q)
        {
            const Shape s (f0, q, gainDb);
            return magnitudeDb ({ 1 + s.alpha * s.A, -2 * s.cosW, 1 - s.alpha * s.A, 1 + s.alpha / s.A, -2 * s.cosW, 1 - s.alpha / s.A }, hz);
        }

        float lowShelf (float hz, float f0, float gainDb, float q)
        {
            const Shape s (f0, q, gainDb);
            const auto A = s.A, c = s.cosW, k = 2 * std::sqrt (A) * s.alpha;
            return magnitudeDb ({ A * ((A + 1) - (A - 1) * c + k), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - k),
                                  (A + 1) + (A - 1) * c + k, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - k }, hz);
        }

        float highShelf (float hz, float f0, float gainDb, float q)
        {
            const Shape s (f0, q, gainDb);
            const auto A = s.A, c = s.cosW, k = 2 * std::sqrt (A) * s.alpha;
            return magnitudeDb ({ A * ((A + 1) + (A - 1) * c + k), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - k),
                                  (A + 1) - (A - 1) * c + k, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - k }, hz);
        }

        float lowPass (float hz, float f0, float q)
        {
            const Shape s (f0, q);
            return magnitudeDb ({ (1 - s.cosW) / 2, 1 - s.cosW, (1 - s.cosW) / 2, 1 + s.alpha, -2 * s.cosW, 1 - s.alpha }, hz);
        }

        float highPass (float hz, float f0, float q)
        {
            const Shape s (f0, q);
            return magnitudeDb ({ (1 + s.cosW) / 2, -(1 + s.cosW), (1 + s.cosW) / 2, 1 + s.alpha, -2 * s.cosW, 1 - s.alpha }, hz);
        }

        float bandPass (float hz, float f0, float q)
        {
            const Shape s (f0, q);
            return magnitudeDb ({ s.alpha, 0, -s.alpha, 1 + s.alpha, -2 * s.cosW, 1 - s.alpha }, hz);
        }

        float notch (float hz, float f0, float q)
        {
            const Shape s (f0, q);
            return magnitudeDb ({ 1, -2 * s.cosW, 1, 1 + s.alpha, -2 * s.cosW, 1 - s.alpha }, hz);
        }
    }

    //==============================================================================
    Display::Display (Values& valuesToUse, juce::StringArray idsToShow)
        : values (valuesToUse), ids (std::move (idsToShow))
    {
        setOpaque (false);
    }

    void Display::refresh()
    {
        std::vector<float> now;
        now.reserve ((size_t) ids.size());

        for (const auto& id : ids)
            now.push_back (values.get (id));

        if (now != last)
        {
            last = std::move (now);
            repaint();
        }
    }

    juce::Rectangle<float> Display::plotArea() const
    {
        return getLocalBounds().toFloat().reduced (6.0f, 6.0f);
    }

    void Display::drawBackground (juce::Graphics& g)
    {
        drawWell (g, getLocalBounds().toFloat(), (float) Theme::cornerRadius);
    }

    float Display::xForHz (juce::Rectangle<float> area, float hz)
    {
        return area.getX() + area.getWidth() * std::log (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / std::log (1000.0f);
    }

    float Display::hzForX (juce::Rectangle<float> area, float x)
    {
        return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth()));
    }

    void Display::drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> area, float dbRange)
    {
        g.setFont (Theme::font (Theme::captionFontSize));

        for (auto hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const auto x = xForHz (area, hz);
            const bool labelled = hz == 100.0f || hz == 1000.0f || hz == 10000.0f;
            g.setColour (Theme::hairline.withAlpha (labelled ? 0.9f : 0.45f));
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

            if (labelled && area.getHeight() > 50.0f)
            {
                g.setColour (Theme::textFaint);
                g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (x + 3.0f, area.getBottom() - 12.0f, 30.0f, 11.0f), juce::Justification::centredLeft, false);
            }
        }

        if (dbRange > 0.0f)
            for (auto db : { -dbRange * 0.5f, 0.0f, dbRange * 0.5f })
            {
                const auto y = area.getCentreY() - db / dbRange * area.getHeight() * 0.5f;
                g.setColour (db == 0.0f ? Theme::border : Theme::hairline.withAlpha (0.45f));
                g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
            }
    }

    void Display::drawDbGrid (juce::Graphics& g, juce::Rectangle<float> area, float minDb, float maxDb)
    {
        g.setFont (Theme::font (Theme::captionFontSize));

        for (float db = std::ceil (minDb / 12.0f) * 12.0f; db <= maxDb; db += 12.0f)
        {
            const auto y = juce::jmap (db, minDb, maxDb, area.getBottom(), area.getY());
            const auto x = juce::jmap (db, minDb, maxDb, area.getX(), area.getRight());
            g.setColour (Theme::hairline.withAlpha (0.5f));
            g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        }
    }

    namespace
    {
        constexpr float pi = juce::MathConstants<float>::pi;

        void strokeCurve (juce::Graphics& g, const juce::Path& curve, juce::Colour colour, float baselineY, bool fill)
        {
            if (fill)
            {
                const auto bounds = curve.getBounds();
                juce::Path area (curve);
                area.lineTo (bounds.getRight(), baselineY);
                area.lineTo (bounds.getX(), baselineY);
                area.closeSubPath();
                g.setColour (colour.withAlpha (0.16f));
                g.fillPath (area);
            }

            g.setColour (colour);
            g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void drawHandle (juce::Graphics& g, juce::Point<float> p, juce::Colour colour, bool hot)
        {
            const auto r = hot ? 6.0f : 4.5f;
            g.setColour (Theme::display);
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 2.0f, r * 2.0f + 2.0f).withCentre (p));
            g.setColour (colour);
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (p), 2.0f);

            if (hot)
            {
                g.setColour (colour.withAlpha (0.25f));
                g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 8.0f, r * 2.0f + 8.0f).withCentre (p));
            }
        }

        void label (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification j = juce::Justification::topRight)
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::captionFontSize + 0.5f, Theme::Weight::medium));
            g.drawText (text, area.reduced (6.0f, 3.0f), j, false);
        }

        //==============================================================================
        /** ADSR: times in seconds, sustain 0-100. Each point can be dragged. */
        class Envelope : public Display
        {
        public:
            using Display::Display;

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto pts = points();
                const auto area = plotArea();

                juce::Path curve;
                curve.startNewSubPath (area.getX(), area.getBottom());
                curve.quadraticTo ({ (area.getX() + pts[0].x) * 0.5f, area.getY() + (pts[0].y - area.getY()) * 0.2f }, pts[0]);   // a gentle attack bow
                curve.quadraticTo ({ pts[0].x + (pts[1].x - pts[0].x) * 0.25f, pts[1].y }, pts[1]);
                curve.lineTo (pts[2]);
                curve.quadraticTo ({ pts[2].x + (pts[3].x - pts[2].x) * 0.25f, area.getBottom() }, pts[3]);

                g.setColour (Theme::hairline.withAlpha (0.6f));
                g.drawVerticalLine (juce::roundToInt (pts[2].x), area.getY(), area.getBottom());   // note off
                strokeCurve (g, curve, Theme::accent, area.getBottom(), true);

                for (int i = 0; i < 4; ++i)
                    if (i != 2)
                        drawHandle (g, pts[(size_t) i], Theme::accent, i == hot);

                g.setFont (Theme::font (Theme::captionFontSize));
                g.setColour (Theme::textFaint);
                g.drawText ("note off", juce::Rectangle<float> (pts[2].x + 3.0f, area.getY(), 60.0f, 11.0f), juce::Justification::centredLeft, false);
            }

            void mouseMove (const juce::MouseEvent& e) override
            {
                const auto h = hit (e.position);

                if (h != hot)
                {
                    hot = h;
                    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
                    repaint();
                }
            }

            void mouseExit (const juce::MouseEvent&) override   { hot = -1; repaint(); }

            void mouseDown (const juce::MouseEvent& e) override
            {
                dragging = hit (e.position);

                if (dragging < 0)
                    return;

                startValues = { v (0), v (1), v (2), v (3) };

                for (const auto& id : draggedIds())
                    values.beginDrag (id);
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (dragging < 0)
                    return;

                const auto area = plotArea();
                const auto dx = (float) e.getDistanceFromDragStartX() / (area.getWidth() * segmentShare);
                const auto change = [&] (int index, float start)
                {
                    // Times feel even when dragging: the picture uses a square-root scale
                    const auto pos = juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, start) / maxTime) + dx);
                    set (index, pos * pos * maxTime);
                };

                if (dragging == 0) change (0, startValues[0]);
                if (dragging == 1)
                {
                    change (1, startValues[1]);
                    const auto dy = (float) e.getDistanceFromDragStartY() / area.getHeight();
                    set (2, juce::jlimit (0.0f, 100.0f, startValues[2] - dy * 100.0f));
                }
                if (dragging == 3) change (3, startValues[3]);
            }

            void mouseUp (const juce::MouseEvent&) override
            {
                if (dragging < 0)
                    return;

                for (const auto& id : draggedIds())
                    values.endDrag (id);

                dragging = -1;
            }

        private:
            static constexpr float maxTime = 8.0f, segmentShare = 0.27f;
            int hot = -1, dragging = -1;
            std::array<float, 4> startValues {};

            void set (int index, float value)
            {
                const auto range = values.rangeOf (ids[index]);
                values.set (ids[index], juce::jlimit (range.start, range.end, value));
            }

            juce::StringArray draggedIds() const
            {
                if (dragging == 1)  return { ids[1], ids[2] };
                return { ids[dragging] };
            }

            std::array<juce::Point<float>, 4> points() const
            {
                const auto area = plotArea();
                const auto w = area.getWidth() * segmentShare;
                const auto norm = [] (float t) { return std::sqrt (juce::jlimit (0.0f, 1.0f, t / maxTime)); };
                const auto sustainY = area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, v (2) / 100.0f);
                const auto xA = area.getX() + norm (v (0)) * w;
                const auto xD = xA + norm (v (1)) * w;
                const auto xS = xD + area.getWidth() * (1.0f - 3.0f * segmentShare);
                const auto xR = xS + norm (v (3)) * w;
                return { juce::Point<float> { xA, area.getY() }, { xD, sustainY }, { xS, sustainY }, { xR, area.getBottom() } };
            }

            int hit (juce::Point<float> p) const
            {
                const auto pts = points();
                int best = -1;
                float bestDistance = 12.0f;

                for (int i : { 0, 1, 3 })
                    if (const auto d = pts[(size_t) i].getDistanceFrom (p); d < bestDistance)
                    {
                        best = i;
                        bestDistance = d;
                    }

                return best;
            }
        };

        //==============================================================================
        /** The synth's filter: type, cut-off (as a MIDI note), resonance, slope. Drag to change. */
        class SynthFilter : public Display
        {
        public:
            using Display::Display;

            static float hzForNote (float note)     { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }
            static float noteForHz (float hz)       { return 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, hz) / 440.0f); }

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea();
                drawFrequencyGrid (g, area, 0.0f);
                const auto type = juce::roundToInt (v (0));
                const auto f0 = hzForNote (v (1));
                const auto q = 0.707f + v (2) / 100.0f * 6.0f;
                const bool steep = juce::roundToInt (v (3)) == 24;
                const auto yFor = [&area] (float db) { return juce::jmap (juce::jlimit (-36.0f, 18.0f, db), -36.0f, 18.0f, area.getBottom(), area.getY()); };

                const auto response = [&] (float hz)
                {
                    const auto stage = [&] (float qq)
                    {
                        switch (type)
                        {
                            case 1:  return Response::lowPass (hz, f0, qq);
                            case 2:  return Response::highPass (hz, f0, qq);
                            case 3:  return Response::bandPass (hz, f0, qq);
                            case 4:  return Response::notch (hz, f0, qq);
                            default: return 0.0f;
                        }
                    };

                    return stage (q) + (steep ? stage (0.707f) : 0.0f);
                };

                juce::Path curve;

                for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
                {
                    const juce::Point<float> p (x, yFor (response (hzForX (area, x))));

                    if (x == area.getX()) curve.startNewSubPath (p);
                    else                  curve.lineTo (p);
                }

                strokeCurve (g, curve, type == 0 ? Theme::textFaint : Theme::accent, area.getBottom(), type != 0);

                if (type != 0)
                    drawHandle (g, { xForHz (area, f0), yFor (response (f0)) }, Theme::accent, hot);
                else
                    label (g, "Filter off", area, juce::Justification::centred);

                label (g, juce::String (juce::roundToInt (f0)) + " Hz", area);
            }

            void mouseEnter (const juce::MouseEvent&) override  { hot = true; repaint(); }
            void mouseExit (const juce::MouseEvent&) override   { hot = false; repaint(); }

            void mouseDown (const juce::MouseEvent&) override
            {
                start = { v (1), v (2) };
                values.beginDrag (ids[1]);
                values.beginDrag (ids[2]);
                setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                const auto area = plotArea();
                const auto startX = xForHz (area, hzForNote (start.first));
                const auto note = noteForHz (hzForX (area, startX + (float) e.getDistanceFromDragStartX()));
                const auto freqRange = values.rangeOf (ids[1]), resRange = values.rangeOf (ids[2]);
                values.set (ids[1], juce::jlimit (freqRange.start, freqRange.end, note));
                values.set (ids[2], juce::jlimit (resRange.start, resRange.end, start.second - (float) e.getDistanceFromDragStartY() * 0.5f));
            }

            void mouseUp (const juce::MouseEvent&) override
            {
                values.endDrag (ids[1]);
                values.endDrag (ids[2]);
                setMouseCursor (juce::MouseCursor::NormalCursor);
            }

        private:
            bool hot = false;
            std::pair<float, float> start;
        };

        //==============================================================================
        float oscSample (int shape, float phase, float pulseWidth)
        {
            phase -= std::floor (phase);

            switch (shape)
            {
                case 1:  return std::sin (phase * 2.0f * pi);
                case 2:  return phase < pulseWidth ? 1.0f : -1.0f;
                case 3:  return 1.0f - 2.0f * phase;
                case 4:  return phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
                case 5:  return std::sin (phase * 91.0f) * std::sin (phase * 377.0f + 1.3f) * 0.9f;   // looks like noise, draws the same every time
                default: return 0.0f;
            }
        }

        /** One oscillator's wave, with its unison voices as faint copies. */
        class OscWave : public Display
        {
        public:
            using Display::Display;

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea().reduced (2.0f, 4.0f);
                const auto shape = juce::roundToInt (v (0));
                const auto pw = v (1) > 0.0f ? v (1) : 0.5f;
                const auto voices = juce::jlimit (1, 8, juce::roundToInt (v (2)));
                const auto detune = v (3);

                g.setColour (Theme::hairline.withAlpha (0.6f));
                g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());

                if (shape == 0)
                {
                    label (g, "Off", area, juce::Justification::centred);
                    return;
                }

                const auto wave = [&] (float stretch, float offset)
                {
                    juce::Path p;

                    for (float x = 0.0f; x <= area.getWidth(); x += 1.5f)
                    {
                        const auto phase = x / area.getWidth() * 2.0f * stretch + offset;
                        const juce::Point<float> pt (area.getX() + x, area.getCentreY() - oscSample (shape, phase, pw) * area.getHeight() * 0.45f);

                        if (x == 0.0f) p.startNewSubPath (pt);
                        else           p.lineTo (pt);
                    }

                    return p;
                };

                if (voices > 1 && detune > 0.0f)
                    for (int i = 1; i < voices; ++i)
                    {
                        const auto spread = ((float) i / (float) (voices - 1) - 0.5f) * 2.0f;
                        g.setColour (Theme::accent.withAlpha (0.25f));
                        g.strokePath (wave (1.0f + spread * detune * 0.3f, spread * 0.07f), juce::PathStrokeType (1.2f));
                    }

                g.setColour (Theme::accent);
                g.strokePath (wave (1.0f, 0.0f), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                if (voices > 1)
                    label (g, juce::String (voices) + " voices", area);
            }
        };

        //==============================================================================
        /** A modulation shape over one second (the synth's LFOs) or two (chorus, phaser, tremolo). */
        class Lfo : public Display
        {
        public:
            Lfo (Values& vals, juce::StringArray idsToShow, bool synthLfo) : Display (vals, std::move (idsToShow)), synth (synthLfo) {}

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea().reduced (2.0f, 4.0f);
                const auto shape = synth ? juce::roundToInt (v (0)) : 1;
                const auto rate = synth ? v (1) : v (0);
                auto depth = synth ? v (2) : v (1);

                if (! synth)
                {
                    const auto range = values.rangeOf (ids[1]);
                    depth = range.end > 1.5f ? depth / range.end : depth;
                }

                const auto seconds = synth ? 1.0f : 2.0f;
                const auto cycles = juce::jlimit (0.25f, 16.0f, rate * seconds);

                g.setColour (Theme::hairline.withAlpha (0.6f));
                g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());

                if (shape == 0)
                {
                    label (g, "Off", area, juce::Justification::centred);
                    return;
                }

                juce::Path p;

                for (float x = 0.0f; x <= area.getWidth(); x += 1.5f)
                {
                    const auto phase = x / area.getWidth() * cycles;
                    const auto frac = phase - std::floor (phase);
                    float y = 0.0f;

                    switch (shape)
                    {
                        case 1:  y = std::sin (frac * 2.0f * pi); break;
                        case 2:  y = frac < 0.5f ? 4.0f * frac - 1.0f : 3.0f - 4.0f * frac; break;
                        case 3:  y = 2.0f * frac - 1.0f; break;
                        case 4:  y = 1.0f - 2.0f * frac; break;
                        case 5:  y = frac < 0.5f ? 1.0f : -1.0f; break;
                        default: y = std::sin (std::floor (phase) * 12.9898f) * 0.9f; break;   // steps that look random
                    }

                    const juce::Point<float> pt (area.getX() + x, area.getCentreY() - y * juce::jlimit (0.05f, 1.0f, depth) * area.getHeight() * 0.45f);

                    if (x == 0.0f) p.startNewSubPath (pt);
                    else           p.lineTo (pt);
                }

                g.setColour (Theme::accent);
                g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                label (g, juce::String (rate, rate < 10.0f ? 2 : 1) + " Hz", area);
            }

        private:
            bool synth;
        };

        //==============================================================================
        /** Tracktion's 4-band EQ: the curve and a handle per band. */
        class Eq4 : public Display
        {
        public:
            using Display::Display;

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea();
                drawFrequencyGrid (g, area, dbRange);

                juce::Path curve;

                for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
                {
                    const juce::Point<float> p (x, yFor (area, total (hzForX (area, x))));

                    if (x == area.getX()) curve.startNewSubPath (p);
                    else                  curve.lineTo (p);
                }

                // Each band's own contribution, faintly in its colour
                for (int b = 0; b < 4; ++b)
                {
                    juce::Path band;

                    for (float x = area.getX(); x <= area.getRight(); x += 3.0f)
                    {
                        const juce::Point<float> p (x, yFor (area, bandDb (b, hzForX (area, x))));

                        if (x == area.getX()) band.startNewSubPath (p);
                        else                  band.lineTo (p);
                    }

                    g.setColour (bandColour (b).withAlpha (0.35f));
                    g.strokePath (band, juce::PathStrokeType (1.0f));
                }

                // Fill between the curve and 0 dB
                juce::Path fill (curve);
                fill.lineTo (area.getRight(), area.getCentreY());
                fill.lineTo (area.getX(), area.getCentreY());
                fill.closeSubPath();
                g.setColour (Theme::accent.withAlpha (0.14f));
                g.fillPath (fill);
                g.setColour (Theme::accent);
                g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                for (int b = 0; b < 4; ++b)
                    drawHandle (g, handle (b), bandColour (b), b == hot || b == dragging);

                if (hot >= 0)
                    label (g, juce::String (names[hot]) + "  " + juce::String (juce::roundToInt (v (hot * 3))) + " Hz  "
                               + (v (hot * 3 + 1) >= 0.0f ? "+" : "") + juce::String (v (hot * 3 + 1), 1) + " dB  Q " + juce::String (v (hot * 3 + 2), 2), area);
            }

            void mouseMove (const juce::MouseEvent& e) override
            {
                const auto h = hit (e.position);

                if (h != hot)
                {
                    hot = h;
                    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
                    repaint();
                }
            }

            void mouseExit (const juce::MouseEvent&) override   { hot = -1; repaint(); }

            void mouseDown (const juce::MouseEvent& e) override
            {
                dragging = hit (e.position);

                if (dragging < 0)
                    return;

                start = { v (dragging * 3), v (dragging * 3 + 1) };
                values.beginDrag (ids[dragging * 3]);
                values.beginDrag (ids[dragging * 3 + 1]);
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (dragging < 0)
                    return;

                const auto area = plotArea();
                const auto hz = hzForX (area, xForHz (area, start.first) + (float) e.getDistanceFromDragStartX());
                const auto db = start.second - (float) e.getDistanceFromDragStartY() / (area.getHeight() * 0.5f) * dbRange;
                setClamped (dragging * 3, hz);
                setClamped (dragging * 3 + 1, db);
            }

            void mouseUp (const juce::MouseEvent&) override
            {
                if (dragging < 0)
                    return;

                values.endDrag (ids[dragging * 3]);
                values.endDrag (ids[dragging * 3 + 1]);
                dragging = -1;
            }

            void mouseDoubleClick (const juce::MouseEvent& e) override
            {
                if (const auto b = hit (e.position); b >= 0)
                {
                    values.beginDrag (ids[b * 3 + 1]);
                    values.set (ids[b * 3 + 1], 0.0f);
                    values.endDrag (ids[b * 3 + 1]);
                }
            }

            void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
            {
                if (const auto b = hit (e.position); b >= 0)
                {
                    values.beginDrag (ids[b * 3 + 2]);
                    setClamped (b * 3 + 2, v (b * 3 + 2) * (wheel.deltaY > 0 ? 1.12f : 1.0f / 1.12f));
                    values.endDrag (ids[b * 3 + 2]);
                }
            }

        private:
            static constexpr float dbRange = 20.0f;
            static constexpr const char* names[] { "Low shelf", "Mid 1", "Mid 2", "High shelf" };
            int hot = -1, dragging = -1;
            std::pair<float, float> start;

            static juce::Colour bandColour (int b)
            {
                return Theme::bandColours[juce::jlimit (0, 3, b)];
            }

            void setClamped (int index, float value)
            {
                const auto range = values.rangeOf (ids[index]);
                values.set (ids[index], juce::jlimit (range.start, range.end, value));
            }

            static float yFor (juce::Rectangle<float> area, float db)
            {
                return area.getCentreY() - juce::jlimit (-dbRange, dbRange, db) / dbRange * area.getHeight() * 0.5f;
            }

            float bandDb (int b, float hz) const
            {
                const auto f = v (b * 3), gain = v (b * 3 + 1), q = v (b * 3 + 2);

                switch (b)
                {
                    case 0:  return Response::lowShelf (hz, f, gain, q);
                    case 3:  return Response::highShelf (hz, f, gain, q);
                    default: return Response::peak (hz, f, gain, q);
                }
            }

            float total (float hz) const
            {
                return bandDb (0, hz) + bandDb (1, hz) + bandDb (2, hz) + bandDb (3, hz);
            }

            juce::Point<float> handle (int b) const
            {
                const auto area = plotArea();
                return { xForHz (area, v (b * 3)), yFor (area, v (b * 3 + 1)) };
            }

            int hit (juce::Point<float> p) const
            {
                int best = -1;
                float bestDistance = 14.0f;

                for (int b = 0; b < 4; ++b)
                    if (const auto d = handle (b).getDistanceFrom (p); d < bestDistance)
                    {
                        best = b;
                        bestDistance = d;
                    }

                return best;
            }
        };

        //==============================================================================
        /** Input level (x) against output level (y), -60..0 dB, for compressor, gate and limiter. */
        class Transfer : public Display
        {
        public:
            Transfer (Values& vals, juce::StringArray idsToShow, Type t) : Display (vals, std::move (idsToShow)), type (t) {}

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                auto area = plotArea();
                area = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight() * 1.6f), area.getHeight());
                drawDbGrid (g, area, minDb, 0.0f);

                const auto toPoint = [&] (float inDb, float outDb)
                {
                    return juce::Point<float> (juce::jmap (inDb, minDb, 0.0f, area.getX(), area.getRight()),
                                               juce::jmap (juce::jlimit (minDb, 0.0f, outDb), minDb, 0.0f, area.getBottom(), area.getY()));
                };

                // 1:1 for reference
                g.setColour (Theme::textFaint.withAlpha (0.5f));
                const float dashes[] { 3.0f, 3.0f };
                g.drawDashedLine ({ toPoint (minDb, minDb), toPoint (0.0f, 0.0f) }, dashes, 2, 1.0f);

                juce::Path curve;

                for (float in = minDb; in <= 0.0f; in += 0.5f)
                {
                    const auto p = toPoint (in, out (in));

                    if (in == minDb) curve.startNewSubPath (p);
                    else             curve.lineTo (p);
                }

                strokeCurve (g, curve, Theme::accent, area.getBottom(), true);

                // The threshold (or ceiling) as a marked point
                const auto knee = kneeDb();
                drawHandle (g, toPoint (knee, out (knee)), Theme::warning, false);
                label (g, caption(), getLocalBounds().toFloat());
            }

        private:
            static constexpr float minDb = -60.0f;
            Type type;

            float out (float in) const
            {
                switch (type)
                {
                    case Type::compressor:
                    {
                        const auto threshold = juce::Decibels::gainToDecibels (juce::jmax (0.001f, v (0)));
                        const auto ratio = 1.0f / juce::jmax (0.05f, 1.0f - v (1));
                        const auto o = in < threshold ? in : threshold + (in - threshold) / ratio;
                        return o + v (2);
                    }

                    case Type::gate:
                        return in < v (0) ? in + v (1) : in;

                    case Type::limiter:
                        return juce::jmin (in + v (0), v (1));

                    default:
                        return in;
                }
            }

            float kneeDb() const
            {
                switch (type)
                {
                    case Type::compressor:  return juce::Decibels::gainToDecibels (juce::jmax (0.001f, v (0)));
                    case Type::gate:        return v (0);
                    case Type::limiter:     return juce::jlimit (minDb, 0.0f, v (1) - v (0));
                    default:                return 0.0f;
                }
            }

            juce::String caption() const
            {
                switch (type)
                {
                    case Type::compressor:  return juce::String (1.0f / juce::jmax (0.05f, 1.0f - v (1)), 1) + ":1 above " + juce::String (kneeDb(), 1) + " dB";
                    case Type::gate:        return "Closes below " + juce::String (v (0), 0) + " dB";
                    case Type::limiter:     return "Ceiling " + juce::String (v (1), 1) + " dB";
                    default:                return {};
                }
            }
        };

        //==============================================================================
        /** Frequency curves without handles: de-esser, low/high cut, filter, amp tone stack. */
        class Curve : public Display
        {
        public:
            Curve (Values& vals, juce::StringArray idsToShow, Type t) : Display (vals, std::move (idsToShow)), type (t) {}

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea();
                const auto range = type == Type::ampTone ? 15.0f : 24.0f;
                drawFrequencyGrid (g, area, range);
                const auto yFor = [&] (float db) { return area.getCentreY() - juce::jlimit (-range, range, db) / range * area.getHeight() * 0.5f; };

                juce::Path curve;

                for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
                {
                    const juce::Point<float> p (x, yFor (db (hzForX (area, x))));

                    if (x == area.getX()) curve.startNewSubPath (p);
                    else                  curve.lineTo (p);
                }

                juce::Path fill (curve);
                fill.lineTo (area.getRight(), area.getCentreY());
                fill.lineTo (area.getX(), area.getCentreY());
                fill.closeSubPath();
                g.setColour ((type == Type::deEsser ? Theme::warning : Theme::accent).withAlpha (0.14f));
                g.fillPath (fill);
                g.setColour (type == Type::deEsser ? Theme::warning : Theme::accent);
                g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                switch (type)
                {
                    case Type::deEsser:   label (g, "Cuts up to " + juce::String (-v (2), 0) + " dB around " + juce::String (v (0) / 1000.0f, 1) + " kHz", area); break;
                    case Type::cutFilter: label (g, juce::String (juce::roundToInt (v (0))) + " Hz - " + juce::String (v (1) / 1000.0f, 1) + " kHz", area); break;
                    case Type::lowpass:   label (g, juce::String (v (1) >= 0.5f ? "High-pass " : "Low-pass ") + juce::String (juce::roundToInt (v (0))) + " Hz", area); break;
                    default: break;
                }
            }

        private:
            Type type;

            float db (float hz) const
            {
                switch (type)
                {
                    // The cut deepens as the "s" goes over the threshold: drawn at its deepest
                    case Type::deEsser:   return Response::peak (hz, v (0), v (2), 1.6f);
                    case Type::cutFilter: return Response::highPass (hz, v (0), 0.707f) + Response::lowPass (hz, v (1), 0.707f);
                    case Type::lowpass:   return v (1) >= 0.5f ? Response::highPass (hz, v (0), 0.707f) : Response::lowPass (hz, v (0), 0.707f);
                    case Type::ampTone:   return Response::highPass (hz, v (0), 0.707f) + Response::lowShelf (hz, 100.0f, v (1), 0.7f)
                                               + Response::peak (hz, 700.0f, v (2), 0.7f) + Response::highShelf (hz, 3000.0f, v (3), 0.7f)
                                               + Response::peak (hz, 5000.0f, v (4), 1.0f);
                    default:              return 0.0f;
                }
            }
        };

        //==============================================================================
        /** What the drive does to a wave: input (x) against output (y). */
        class Saturator : public Display
        {
        public:
            using Display::Display;

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                auto area = plotArea();
                area = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight() * 1.6f), area.getHeight());
                g.setColour (Theme::hairline);
                g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
                g.drawVerticalLine (juce::roundToInt (area.getCentreX()), area.getY(), area.getBottom());

                const auto drive = juce::Decibels::decibelsToGain (v (0));
                const auto curveType = juce::roundToInt (v (1));
                const auto mix = juce::jlimit (0.0f, 1.0f, v (2) / 100.0f);
                const auto shape = [&] (float x)
                {
                    const auto d = x * drive;

                    switch (curveType)
                    {
                        case 0:  return std::tanh (d) / std::tanh (drive);                                       // tape: soft and even
                        case 1:  return (d >= 0.0f ? std::tanh (d) : std::tanh (d * 0.7f) / 0.7f) / std::tanh (drive);   // tube: lopsided
                        default: return juce::jlimit (-1.0f, 1.0f, d) / juce::jmin (1.0f, drive);               // hard clip
                    }
                };

                juce::Path p;

                for (float x = -1.0f; x <= 1.0f; x += 0.02f)
                {
                    const auto y = juce::jlimit (-1.2f, 1.2f, mix * shape (x) + (1.0f - mix) * x);
                    const juce::Point<float> pt (juce::jmap (x, -1.0f, 1.0f, area.getX(), area.getRight()),
                                                 juce::jmap (y, -1.2f, 1.2f, area.getBottom(), area.getY()));

                    if (x == -1.0f) p.startNewSubPath (pt);
                    else            p.lineTo (pt);
                }

                g.setColour (Theme::accent);
                g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                label (g, juce::String ("+") + juce::String (v (0), 1) + " dB drive", getLocalBounds().toFloat());
            }
        };

        //==============================================================================
        /** Echoes over time: the dry hit, then each repeat as loud as the feedback leaves it. */
        class Delay : public Display
        {
        public:
            using Display::Display;

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea().reduced (4.0f, 4.0f);
                const auto lengthMs = juce::jmax (1.0f, v (0));
                const auto windowMs = juce::jmax (1000.0f, lengthMs * 6.0f);
                const auto feedback = v (1) <= -30.0f ? 0.0f : juce::Decibels::decibelsToGain (v (1));
                const auto mix = juce::jlimit (0.0f, 1.0f, v (2));
                const auto bar = [&] (float ms, float level, juce::Colour c)
                {
                    const auto x = area.getX() + ms / windowMs * area.getWidth();
                    const auto h = area.getHeight() * juce::jlimit (0.0f, 1.0f, level);
                    g.setColour (c);
                    g.fillRoundedRectangle (juce::Rectangle<float> (x - 2.0f, area.getBottom() - h, 4.0f, h), 2.0f);
                };

                bar (2.0f, 1.0f - mix * 0.5f, Theme::textDim);   // the dry sound
                auto level = mix;

                for (int i = 1; i * lengthMs < windowMs && level > 0.01f; ++i)
                {
                    bar ((float) i * lengthMs, level, Theme::accent.withAlpha (juce::jlimit (0.35f, 1.0f, level + 0.3f)));
                    level *= feedback;
                }

                label (g, juce::String (juce::roundToInt (lengthMs)) + " ms" + noteName (lengthMs), getLocalBounds().toFloat());
            }

        private:
            juce::String noteName (float ms) const
            {
                const auto beatMs = 60000.0 / juce::jmax (20.0, values.getBpm());
                const std::pair<double, const char*> notes[] { { 4.0, "1/1" }, { 2.0, "1/2" }, { 1.0, "1/4" }, { 0.75, "1/8 dotted" }, { 2.0 / 3.0, "1/4 triplet" },
                                                               { 0.5, "1/8" }, { 1.0 / 3.0, "1/8 triplet" }, { 0.375, "1/16 dotted" }, { 0.25, "1/16" } };

                for (const auto& [beats, name] : notes)
                    if (std::abs (ms - beats * beatMs) < beats * beatMs * 0.02)
                        return juce::String (" = ") + name;

                return {};
            }
        };

        //==============================================================================
        /** The reverb tail: how long it lasts (size), how dark it gets (damping), and how loud (wet). */
        class Reverb : public Display
        {
        public:
            using Display::Display;

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea().reduced (4.0f, 4.0f);
                const auto freeze = v (4) >= 0.5f;
                const auto decaySeconds = 0.3f + v (0) * 4.5f;
                const auto wet = juce::jlimit (0.05f, 1.0f, v (2) * 1.6f);
                const auto windowSeconds = 5.0f;

                juce::Path tail;
                tail.startNewSubPath (area.getX(), area.getBottom());

                for (float x = 0.0f; x <= area.getWidth(); x += 2.0f)
                {
                    const auto t = x / area.getWidth() * windowSeconds;
                    const auto env = freeze ? 1.0f : std::exp (-t * 6.9f / decaySeconds);   // -60 dB at the decay time
                    const auto shimmer = 0.85f + 0.15f * std::sin (t * 37.0f) * std::sin (t * 13.0f);
                    tail.lineTo (area.getX() + x, area.getBottom() - area.getHeight() * wet * env * shimmer);
                }

                tail.lineTo (area.getRight(), area.getBottom());
                tail.closeSubPath();

                // Damping darkens the tail: less of the bright colour remains toward the end
                const auto bright = Theme::accent, dark = Theme::accent.interpolatedWith (Theme::display, 0.55f + v (1) * 0.35f);
                g.setGradientFill (juce::ColourGradient::horizontal (bright.withAlpha (0.55f), area.getX(), dark.withAlpha (0.35f), area.getRight()));
                g.fillPath (tail);
                g.setColour (Theme::accent);
                g.strokePath (tail, juce::PathStrokeType (1.2f));

                label (g, freeze ? juce::String ("Frozen") : juce::String (decaySeconds, 1) + " s tail", getLocalBounds().toFloat());
            }
        };

        //==============================================================================
        /** One octave with the scale's notes lit and the key's root in the accent. */
        class Keys : public Display
        {
        public:
            using Display::Display;

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea().reduced (2.0f, 4.0f);
                const auto root = juce::jlimit (0, 11, juce::roundToInt (v (0)));
                const auto scale = juce::roundToInt (v (1));
                const auto inScale = [&] (int note)
                {
                    const auto degree = (note - root + 12) % 12;
                    static const std::array<bool, 12> major { true, false, true, false, true, true, false, true, false, true, false, true };
                    static const std::array<bool, 12> minor { true, false, true, true, false, true, false, true, true, false, true, false };
                    return scale == 1 ? major[(size_t) degree] : scale == 2 ? minor[(size_t) degree] : true;
                };

                static const int whites[] { 0, 2, 4, 5, 7, 9, 11 };
                static const std::pair<int, float> blacks[] { { 1, 1.0f }, { 3, 2.0f }, { 6, 4.0f }, { 8, 5.0f }, { 10, 6.0f } };
                const auto keyWidth = area.getWidth() / 7.0f;

                for (int i = 0; i < 7; ++i)
                {
                    const auto note = whites[i];
                    const auto key = juce::Rectangle<float> (area.getX() + (float) i * keyWidth, area.getY(), keyWidth, area.getHeight()).reduced (1.0f, 0.0f);
                    g.setColour (note == root ? Theme::accent : inScale (note) ? Theme::keyWhite : Theme::keyWhiteOff);
                    g.fillRoundedRectangle (key, 3.0f);

                    if (inScale (note) && note != root)   // a dot marks every note the tuner can land on
                    {
                        g.setColour (Theme::accent);
                        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ key.getCentreX(), key.getBottom() - 8.0f }));
                    }
                }

                for (const auto& [note, position] : blacks)
                {
                    const auto key = juce::Rectangle<float> (keyWidth * 0.6f, area.getHeight() * 0.6f)
                                         .withCentre ({ area.getX() + position * keyWidth, area.getY() + area.getHeight() * 0.3f });
                    g.setColour (note == root ? Theme::accent : inScale (note) ? Theme::keyBlack : Theme::keyBlackOff);
                    g.fillRoundedRectangle (key, 2.0f);
                    g.setColour (Theme::laneSeparator);
                    g.drawRoundedRectangle (key, 2.0f, 1.0f);

                    if (inScale (note) && note != root)
                    {
                        g.setColour (Theme::accent);
                        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ key.getCentreX(), key.getBottom() - 7.0f }));
                    }
                }
            }
        };

        //==============================================================================
        /** A stereo picture: how wide the sound is, and if a side is flipped. */
        class Stereo : public Display
        {
        public:
            using Display::Display;

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea();
                const auto c = area.getCentre();
                const auto r = juce::jmin (area.getWidth(), area.getHeight()) * 0.45f;
                const auto width = v (1) >= 0.5f ? 0.0f : juce::jlimit (0.0f, 2.0f, v (0) / 100.0f);

                // L and R axes, as on a goniometer
                g.setColour (Theme::hairline);
                g.drawLine ({ c.translated (-r, -r), c.translated (r, r) }, 1.0f);
                g.drawLine ({ c.translated (r, -r), c.translated (-r, r) }, 1.0f);
                g.setFont (Theme::font (Theme::captionFontSize));
                g.setColour (Theme::textFaint);
                g.drawText ("L", juce::Rectangle<float> (12.0f, 12.0f).withCentre (c.translated (-r - 2.0f, -r - 2.0f)), juce::Justification::centred, false);
                g.drawText ("R", juce::Rectangle<float> (12.0f, 12.0f).withCentre (c.translated (r + 2.0f, -r - 2.0f)), juce::Justification::centred, false);

                // The sound: an ellipse, tall when narrow, round at 100%, wide above
                const auto ellipse = juce::Rectangle<float> (juce::jmax (2.0f, r * 0.9f * width), r * 1.8f * (1.0f - 0.25f * juce::jmax (0.0f, width - 1.0f))).withCentre (c);
                g.setColour (Theme::accent.withAlpha (0.18f));
                g.fillEllipse (ellipse);
                g.setColour (Theme::accent);
                g.drawEllipse (ellipse, 2.0f);

                juce::String text = v (1) >= 0.5f ? "Mono" : juce::String (juce::roundToInt (v (0))) + "% wide";

                if (v (2) >= 0.5f || v (3) >= 0.5f)
                    text << "  (phase flipped)";

                label (g, text, getLocalBounds().toFloat());
            }
        };
    }

    //==============================================================================
    std::unique_ptr<Display> create (Type type, Values& values, juce::StringArray ids)
    {
        switch (type)
        {
            case Type::envelope:    return std::make_unique<Envelope> (values, ids);
            case Type::synthFilter: return std::make_unique<SynthFilter> (values, ids);
            case Type::oscWave:     return std::make_unique<OscWave> (values, ids);
            case Type::lfo:         return std::make_unique<Lfo> (values, ids, true);
            case Type::wobble:      return std::make_unique<Lfo> (values, ids, false);
            case Type::eq4:         return std::make_unique<Eq4> (values, ids);
            case Type::compressor:
            case Type::gate:
            case Type::limiter:     return std::make_unique<Transfer> (values, ids, type);
            case Type::deEsser:
            case Type::cutFilter:
            case Type::lowpass:
            case Type::ampTone:     return std::make_unique<Curve> (values, ids, type);
            case Type::saturator:   return std::make_unique<Saturator> (values, ids);
            case Type::delay:       return std::make_unique<Delay> (values, ids);
            case Type::reverb:      return std::make_unique<Reverb> (values, ids);
            case Type::keys:        return std::make_unique<Keys> (values, ids);
            case Type::stereo:      return std::make_unique<Stereo> (values, ids);
        }

        return {};
    }
}
