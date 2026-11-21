/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "PluginDisplays.h"
#include "Theme.h"
#include "Widgets.h"
#include "PluginLook.h"
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
    namespace P = Theme::Plugin;

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

        if (now != last || isLive())
        {
            last = std::move (now);
            repaint();
        }
    }

    void Display::setActive (bool shouldBeActive)
    {
        if (active != shouldBeActive)
        {
            active = shouldBeActive;
            repaint();
        }
    }

    juce::Rectangle<float> Display::plotArea() const
    {
        return getLocalBounds().toFloat().reduced (8.0f, 8.0f);
    }

    void Display::drawBackground (juce::Graphics& g)
    {
        PluginLook::drawScreen (g, getLocalBounds().toFloat());
    }

    float Display::xForHz (juce::Rectangle<float> area, float hz)
    {
        return area.getX() + area.getWidth() * std::log (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / std::log (1000.0f);
    }

    float Display::hzForX (juce::Rectangle<float> area, float x)
    {
        return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth()));
    }

    juce::Path Display::curveThrough (juce::Rectangle<float> area, const std::function<float (float)>& yForX)
    {
        juce::Path p;

        for (float x = area.getX(); x <= area.getRight() + 0.5f; x += 1.0f)   // one point per pixel: steep slopes stay smooth
        {
            const juce::Point<float> pt (x, yForX (x));

            if (x == area.getX()) p.startNewSubPath (pt);
            else                  p.lineTo (pt);
        }

        return p;
    }

    void Display::drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> area, float dbRange)
    {
        g.setFont (Theme::font (Theme::captionFontSize));

        // every 1-2-...-9 step faintly, the decades stronger and labelled
        for (float decade = 10.0f; decade <= 10000.0f; decade *= 10.0f)
            for (int step : { 1, 2, 5 })
            {
                const auto hz = decade * (float) step;

                if (hz < 20.0f || hz > 20000.0f)
                    continue;

                const auto x = xForHz (area, hz);
                const bool major = step == 1;
                g.setColour (major ? P::screenGridMajor : P::screenGrid);
                g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

                if (major && hz >= 100.0f && area.getHeight() > 50.0f)
                {
                    g.setColour (P::screenLabel);
                    g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                                juce::Rectangle<float> (x + 3.0f, area.getBottom() - 12.0f, 30.0f, 11.0f), juce::Justification::centredLeft, false);
                }
            }

        if (dbRange > 0.0f)
            for (auto db : { -dbRange * 0.5f, 0.0f, dbRange * 0.5f })
            {
                const auto y = area.getCentreY() - db / dbRange * area.getHeight() * 0.5f;
                g.setColour (db == 0.0f ? P::screenGridMajor : P::screenGrid);
                g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());

                if (db != 0.0f && area.getHeight() > 70.0f)
                {
                    g.setColour (P::screenLabel);
                    g.drawText ((db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db)),
                                juce::Rectangle<float> (area.getX() + 3.0f, y - 12.0f, 30.0f, 11.0f), juce::Justification::centredLeft, false);
                }
            }
    }

    void Display::drawDbGrid (juce::Graphics& g, juce::Rectangle<float> area, float minDb, float maxDb)
    {
        g.setFont (Theme::font (Theme::captionFontSize));

        for (float db = std::ceil (minDb / 12.0f) * 12.0f; db <= maxDb; db += 12.0f)
        {
            const auto y = juce::jmap (db, minDb, maxDb, area.getBottom(), area.getY());
            const auto x = juce::jmap (db, minDb, maxDb, area.getX(), area.getRight());
            g.setColour (db == maxDb ? P::screenGridMajor : P::screenGrid);
            g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

            if (area.getHeight() > 90.0f && db > minDb && db < maxDb)
            {
                g.setColour (P::screenLabel);
                g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (x + 2.0f, area.getBottom() - 11.0f, 26.0f, 10.0f),
                            juce::Justification::centredLeft, false);
                g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (area.getX() + 2.0f, y + 1.0f, 26.0f, 10.0f),
                            juce::Justification::centredLeft, false);
            }
        }
    }

    namespace
    {
        constexpr float pi = juce::MathConstants<float>::pi;
        constexpr float pickerWidth = 22.0f;   // the ◀ ▶ zones at a screen's edges

        /** " · " between values in a tag (non-ASCII text must be built from UTF-8). */
        juce::String sep()     { return juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")); }

        /** Fills between a curve and a baseline with the colour fading away from the curve. */
        void fillToBaseline (juce::Graphics& g, const juce::Path& curve, juce::Colour colour, juce::Rectangle<float> area, float baselineY)
        {
            juce::Path shape (curve);
            const auto bounds = curve.getBounds();
            shape.lineTo (bounds.getRight(), baselineY);
            shape.lineTo (bounds.getX(), baselineY);
            shape.closeSubPath();

            {   // above the baseline: strongest at the top
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (area.withBottom (baselineY).getSmallestIntegerContainer());
                g.setGradientFill (juce::ColourGradient::vertical (colour.withAlpha (0.30f), area.getY(), colour.withAlpha (0.03f), baselineY));
                g.fillPath (shape);
            }

            if (baselineY < area.getBottom())
            {   // below it (EQ cuts): strongest at the bottom
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (area.withTop (baselineY).getSmallestIntegerContainer());
                g.setGradientFill (juce::ColourGradient::vertical (colour.withAlpha (0.03f), baselineY, colour.withAlpha (0.30f), area.getBottom()));
                g.fillPath (shape);
            }
        }

        void strokeCurve (juce::Graphics& g, const juce::Path& curve, juce::Colour colour, juce::Rectangle<float> area, float baselineY, bool fill)
        {
            if (fill)
                fillToBaseline (g, curve, colour, area, baselineY);

            PluginLook::drawGlowingPath (g, curve, colour);
        }

        void dashedLine (juce::Graphics& g, juce::Line<float> line, juce::Colour colour)
        {
            const float dashes[] { 4.0f, 4.0f };
            g.setColour (colour);
            g.drawDashedLine (line, dashes, 2, 1.0f);
        }

        void label (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification j = juce::Justification::topRight)
        {
            if (area.getWidth() < 170.0f)
                return;   // a thumbnail (device cards): the caption would sit on the curve

            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::captionFontSize + 0.5f, Theme::Weight::medium));
            g.drawText (text, area.reduced (6.0f, 3.0f), j, false);
        }

        /** A small tag next to a dragged handle with its exact values. */
        void valueTag (juce::Graphics& g, const juce::String& text, juce::Point<float> at, juce::Rectangle<float> bounds, juce::Colour colour)
        {
            const auto font = Theme::font (Theme::captionFontSize + 0.5f, Theme::Weight::semibold);
            const auto w = juce::GlyphArrangement::getStringWidth (font, text) + 14.0f;
            auto box = juce::Rectangle<float> (w, 18.0f).withPosition (at.x + 12.0f, at.y - 26.0f);

            if (box.getRight() > bounds.getRight() - 4.0f)  box.setX (at.x - 12.0f - w);
            if (box.getY() < bounds.getY() + 4.0f)          box.setY (at.y + 10.0f);

            g.setColour (P::screen.withAlpha (0.92f));
            g.fillRoundedRectangle (box, 9.0f);
            g.setColour (colour.withAlpha (0.8f));
            g.drawRoundedRectangle (box.reduced (0.5f), 9.0f, 1.0f);
            g.setColour (Theme::text);
            g.setFont (font);
            g.drawText (text, box, juce::Justification::centred, false);
        }

        /** "Off" over a dimmed screen, with a hint of what brings it back. */
        void offNote (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text)
        {
            g.setColour (Theme::textFaint);
            g.setFont (Theme::font (Theme::smallFontSize, Theme::Weight::medium));
            g.drawText (text, area, juce::Justification::centred, false);
        }

        /** ◀ and ▶ at the screen's edges, for picking a wave without a menu. */
        void drawPickers (juce::Graphics& g, juce::Rectangle<float> bounds, bool hotLeft, bool hotRight)
        {
            for (int side = 0; side < 2; ++side)
            {
                const auto zone = side == 0 ? bounds.withWidth (pickerWidth) : bounds.withLeft (bounds.getRight() - pickerWidth);
                const auto c = zone.getCentre();
                juce::Path arrow;
                const auto d = side == 0 ? -1.0f : 1.0f;
                arrow.startNewSubPath (c.x - 2.5f * d, c.y - 5.0f);
                arrow.lineTo (c.x + 2.5f * d, c.y);
                arrow.lineTo (c.x - 2.5f * d, c.y + 5.0f);
                g.setColour ((side == 0 ? hotLeft : hotRight) ? Theme::text : Theme::textFaint);
                g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        void stepChoice (Values& values, const juce::String& id, int delta, int count)
        {
            const auto now = juce::roundToInt (values.get (id));
            values.beginDrag (id);
            values.set (id, (float) ((now + delta + count) % count));
            values.endDrag (id);
        }

        /** A thin vertical meter: level in dB from -60 (bottom) to 0 (top), or reduction from 0 down. */
        void drawMeter (juce::Graphics& g, juce::Rectangle<float> bar, float db, juce::Colour colour, bool fromTop)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (bar, 2.0f);
            const auto amount = fromTop ? juce::jlimit (0.0f, 1.0f, -db / 24.0f) : juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            const auto filled = fromTop ? bar.withHeight (bar.getHeight() * amount) : bar.withTop (bar.getBottom() - bar.getHeight() * amount);
            g.setColour (! fromTop && db > -0.5f ? P::hot : colour);
            g.fillRoundedRectangle (filled, 2.0f);
        }

        //==============================================================================
        /** ADSR: times in seconds, sustain 0-100. Each corner can be dragged. */
        class Envelope : public Display
        {
        public:
            Envelope (Values& vals, juce::StringArray idsToShow, juce::Colour c) : Display (vals, std::move (idsToShow)), colour (c) {}

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto pts = points();
                const auto area = plotArea();
                const auto c = tint (colour);

                // the time axis: square-root scale, labelled when there's room
                if (area.getHeight() >= 70.0f)
                {
                    g.setFont (Theme::font (Theme::captionFontSize - 0.5f));

                    float lastLabel = -100.0f;

                    for (auto t : { 0.1f, 1.0f })
                    {
                        const auto x = area.getX() + std::sqrt (t / maxTime) * area.getWidth() * segmentShare;

                        if (x - lastLabel < 44.0f)
                            continue;   // too close to the last label to read

                        lastLabel = x;
                        g.setColour (P::screenGrid);
                        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
                        g.setColour (P::screenLabel);
                        g.drawText (t < 1.0f ? juce::String (juce::roundToInt (t * 1000.0f)) + " ms" : "1 s",
                                    juce::Rectangle<float> (x + 2.0f, area.getBottom() - 10.0f, 40.0f, 10.0f), juce::Justification::centredLeft, false);
                    }
                }

                // attack and decay as real curves, a held plateau, release after the note-off line
                juce::Path curve;
                curve.startNewSubPath (area.getX(), area.getBottom());
                curve.quadraticTo ({ area.getX() + (pts[0].x - area.getX()) * 0.35f, area.getY() + (pts[0].y - area.getY()) * 0.1f }, pts[0]);
                curve.quadraticTo ({ pts[0].x + (pts[1].x - pts[0].x) * 0.15f, pts[1].y }, pts[1]);
                curve.lineTo (pts[2]);
                curve.quadraticTo ({ pts[2].x + (pts[3].x - pts[2].x) * 0.15f, area.getBottom() }, pts[3]);

                dashedLine (g, { pts[2].x, area.getY(), pts[2].x, area.getBottom() }, Theme::textFaint.withAlpha (0.6f));
                strokeCurve (g, curve, c, area, area.getBottom(), true);

                for (int i = 0; i < 4; ++i)
                    PluginLook::drawHandle (g, pts[(size_t) i], c, i == hot, i == dragging);

                g.setFont (Theme::font (Theme::captionFontSize - 0.5f));
                g.setColour (P::screenLabel);
                g.drawText ("note off", juce::Rectangle<float> (pts[2].x + 4.0f, area.getY(), 60.0f, 11.0f), juce::Justification::centredLeft, false);

                if (dragging >= 0)
                    valueTag (g, tagText(), pts[(size_t) dragging], area, c);
            }

            void mouseMove (const juce::MouseEvent& e) override
            {
                const auto h = hit (e.position);

                if (h != hot)
                {
                    hot = h;
                    setMouseCursor (h == 2 ? juce::MouseCursor::UpDownResizeCursor
                                  : h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
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

                repaint();
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (dragging < 0)
                    return;

                const auto area = plotArea();
                const auto fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
                const auto dx = (float) e.getDistanceFromDragStartX() * fine / (area.getWidth() * segmentShare);
                const auto dy = (float) e.getDistanceFromDragStartY() * fine / area.getHeight();
                const auto change = [&] (int index, float start)
                {
                    // Times feel even when dragging: the picture uses a square-root scale
                    const auto pos = juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, start) / maxTime) + dx);
                    set (index, pos * pos * maxTime);
                };

                if (dragging == 0) change (0, startValues[0]);
                if (dragging == 1) change (1, startValues[1]);
                if (dragging == 1 || dragging == 2) set (2, juce::jlimit (0.0f, 100.0f, startValues[2] - dy * 100.0f));
                if (dragging == 3) change (3, startValues[3]);
            }

            void mouseUp (const juce::MouseEvent&) override
            {
                if (dragging < 0)
                    return;

                for (const auto& id : draggedIds())
                    values.endDrag (id);

                dragging = -1;
                repaint();
            }

        private:
            static constexpr float maxTime = 8.0f, segmentShare = 0.27f;
            juce::Colour colour;
            int hot = -1, dragging = -1;
            std::array<float, 4> startValues {};

            static juce::String timeText (float seconds)
            {
                return seconds < 1.0f ? juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms" : juce::String (seconds, 2) + " s";
            }

            juce::String tagText() const
            {
                switch (dragging)
                {
                    case 0:  return "Attack " + timeText (v (0));
                    case 1:  return "Decay " + timeText (v (1)) + sep() + "Sustain " + juce::String (juce::roundToInt (v (2))) + " %";
                    case 2:  return "Sustain " + juce::String (juce::roundToInt (v (2))) + " %";
                    default: return "Release " + timeText (v (3));
                }
            }

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
                float bestDistance = P::handleHitRadius;

                for (int i = 0; i < 4; ++i)
                    if (const auto d = pts[(size_t) i].getDistanceFrom (p); d < bestDistance)
                    {
                        best = i;
                        bestDistance = d;
                    }

                return best;
            }
        };

        //==============================================================================
        /** The synth's filter: type, cut-off (as a MIDI note), resonance, slope [, envelope amount]. */
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
                const auto c = tint (P::signal);

                if (type == 0)
                {
                    dashedLine (g, { area.getX(), yFor (area, 0.0f), area.getRight(), yFor (area, 0.0f) }, Theme::textFaint.withAlpha (0.7f));
                    offNote (g, area.withTrimmedTop (area.getHeight() * 0.45f), "Bypassed: click to use a low-pass filter");
                    return;
                }

                // Where the filter envelope takes the cut-off, faintly in the envelope colour
                if (ids.size() > 4 && std::abs (v (4)) > 0.01f && active)
                {
                    const auto peak = hzForNote (juce::jlimit (0.0f, 135.0f, v (1) + v (4) * 137.0f));
                    const auto ghost = curveThrough (area, [&] (float x) { return yFor (area, response (hzForX (area, x), peak)); });
                    g.setColour (P::env.withAlpha (0.45f));
                    g.strokePath (ghost, juce::PathStrokeType (1.2f));
                    g.setColour (P::env.withAlpha (0.7f));
                    g.setFont (Theme::font (Theme::captionFontSize - 0.5f));
                    g.drawText ("envelope", juce::Rectangle<float> (xForHz (area, peak) + 4.0f, area.getY() + 2.0f, 60.0f, 10.0f), juce::Justification::centredLeft, false);
                }

                const auto curve = curveThrough (area, [&] (float x) { return yFor (area, response (hzForX (area, x), f0)); });
                strokeCurve (g, curve, c, area, area.getBottom(), true);

                const juce::Point<float> handle (xForHz (area, f0), yFor (area, response (f0, f0)));
                PluginLook::drawHandle (g, handle, c, hot, dragging);

                if (dragging)
                    valueTag (g, hzText (f0) + sep() + "Res " + juce::String (juce::roundToInt (v (2))) + " %", handle, area, c);
                else
                    label (g, hzText (f0), area);
            }

            void mouseMove (const juce::MouseEvent& e) override
            {
                const auto area = plotArea();
                const auto f0 = hzForNote (v (1));
                const auto near = juce::Point<float> (xForHz (area, f0), yFor (area, response (f0, f0))).getDistanceFrom (e.position) < P::handleHitRadius * 1.5f;

                if (near != hot)
                {
                    hot = near;
                    repaint();
                }

                setMouseCursor (juce::roundToInt (v (0)) == 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
            }

            void mouseExit (const juce::MouseEvent&) override   { hot = false; repaint(); }

            void mouseDown (const juce::MouseEvent&) override
            {
                if (juce::roundToInt (v (0)) == 0)   // the off state's hint: a click switches a low-pass on
                {
                    values.beginDrag (ids[0]);
                    values.set (ids[0], 1.0f);
                    values.endDrag (ids[0]);
                    return;
                }

                start = { v (1), v (2) };
                values.beginDrag (ids[1]);
                values.beginDrag (ids[2]);
                dragging = true;
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (! dragging)
                    return;

                const auto area = plotArea();
                const auto fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
                const auto startX = xForHz (area, hzForNote (start.first));
                const auto note = noteForHz (hzForX (area, startX + (float) e.getDistanceFromDragStartX() * fine));
                const auto freqRange = values.rangeOf (ids[1]), resRange = values.rangeOf (ids[2]);
                values.set (ids[1], juce::jlimit (freqRange.start, freqRange.end, note));
                values.set (ids[2], juce::jlimit (resRange.start, resRange.end, start.second - (float) e.getDistanceFromDragStartY() * 0.5f * fine));
            }

            void mouseUp (const juce::MouseEvent&) override
            {
                if (! dragging)
                    return;

                values.endDrag (ids[1]);
                values.endDrag (ids[2]);
                dragging = false;
                repaint();
            }

        private:
            bool hot = false, dragging = false;
            std::pair<float, float> start;

            static float yFor (juce::Rectangle<float> area, float db)
            {
                return juce::jmap (juce::jlimit (-36.0f, 18.0f, db), -36.0f, 18.0f, area.getBottom(), area.getY());
            }

            static juce::String hzText (float hz)
            {
                return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
            }

            float response (float hz, float f0) const
            {
                const auto type = juce::roundToInt (v (0));
                const auto q = 0.707f + v (2) / 100.0f * 6.0f;
                const bool steep = juce::roundToInt (v (3)) == 24;
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
            }
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

        /** A shape you pick with ◀ ▶ on the screen's edges (the oscillators and the synth's LFOs). */
        class PickerDisplay : public Display
        {
        public:
            PickerDisplay (Values& vals, juce::StringArray idsToShow, juce::StringArray shapeNames)
                : Display (vals, std::move (idsToShow)), names (std::move (shapeNames)) {}

            void mouseMove (const juce::MouseEvent& e) override
            {
                const auto zone = zoneAt (e.position);

                if (zone != hotZone)
                {
                    hotZone = zone;
                    setMouseCursor (zone != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                    repaint();
                }
            }

            void mouseExit (const juce::MouseEvent&) override   { hotZone = 0; repaint(); }

            void mouseDown (const juce::MouseEvent& e) override
            {
                if (e.mods.isPopupMenu())
                    return showMenu();

                if (const auto zone = zoneAt (e.position); zone != 0)
                    stepChoice (values, ids[0], zone, names.size());
                else
                    showMenu();   // a click on the wave lists every shape
            }

            void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
            {
                if (wheel.deltaY != 0.0f)
                    stepChoice (values, ids[0], wheel.deltaY > 0 ? -1 : 1, names.size());
            }

        protected:
            juce::StringArray names;
            int hotZone = 0;

            int shape() const   { return juce::jlimit (0, names.size() - 1, juce::roundToInt (v (0))); }

            void drawChrome (juce::Graphics& g)
            {
                auto bounds = getLocalBounds().toFloat();
                drawPickers (g, bounds, hotZone < 0, hotZone > 0);
                g.setColour (shape() == 0 ? Theme::textFaint : Theme::textDim);
                g.setFont (Theme::font (Theme::captionFontSize + 0.5f, Theme::Weight::semibold));
                g.drawText (names[shape()], bounds.removeFromBottom (16.0f), juce::Justification::centred, false);
            }

        private:
            int zoneAt (juce::Point<float> p) const
            {
                if (p.x < pickerWidth)                     return -1;
                if (p.x > (float) getWidth() - pickerWidth) return 1;
                return 0;
            }

            void showMenu()
            {
                juce::PopupMenu menu;

                for (int i = 0; i < names.size(); ++i)
                    menu.addItem (i + 1, names[i], true, i == shape());

                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                    [safe = juce::Component::SafePointer<PickerDisplay> (this)] (int r)
                                    {
                                        if (safe != nullptr && r > 0)
                                        {
                                            safe->values.beginDrag (safe->ids[0]);
                                            safe->values.set (safe->ids[0], (float) (r - 1));
                                            safe->values.endDrag (safe->ids[0]);
                                        }
                                    });
            }
        };

        /** One oscillator's wave (two cycles), with its unison voices as faint copies. */
        class OscWave : public PickerDisplay
        {
        public:
            OscWave (Values& vals, juce::StringArray idsToShow)
                : PickerDisplay (vals, std::move (idsToShow), { "Off", "Sine", "Square", "Saw", "Triangle", "Noise" }) {}

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea().reduced (pickerWidth - 6.0f, 2.0f).withTrimmedBottom (10.0f);
                const auto s = shape();
                const auto pw = v (1) > 0.0f ? v (1) : 0.5f;
                const auto voices = juce::jlimit (1, 8, juce::roundToInt (v (2)));
                const auto detune = v (3);
                const auto c = tint (P::signal);

                const auto wave = [&] (int sh, float stretch, float offset, float scale)
                {
                    return curveThrough (area, [&] (float x)
                    {
                        const auto phase = (x - area.getX()) / area.getWidth() * 2.0f * stretch + offset;
                        return area.getCentreY() - oscSample (sh, phase, pw) * area.getHeight() * 0.45f * scale;
                    });
                };

                if (s == 0)
                {
                    // Off: a ghost of a sine and the hint of what to do
                    g.setColour (Theme::textFaint.withAlpha (0.12f));
                    g.strokePath (wave (1, 1.0f, 0.0f, 1.0f), juce::PathStrokeType (1.5f));
                    dashedLine (g, { area.getX(), area.getCentreY(), area.getRight(), area.getCentreY() }, Theme::textFaint.withAlpha (0.5f));
                    offNote (g, area.withTrimmedTop (area.getHeight() * 0.5f), "Off: pick a wave");
                    drawChrome (g);
                    return;
                }

                g.setColour (P::screenGridMajor);
                g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());

                if (voices > 1)
                    for (int i = 0; i < voices; ++i)
                    {
                        const auto spread = ((float) i / (float) (voices - 1) - 0.5f) * 2.0f;
                        g.setColour (c.withAlpha (0.28f));
                        g.strokePath (wave (s, 1.0f + spread * juce::jmax (0.02f, detune) * 0.3f, spread * 0.07f, 0.92f), juce::PathStrokeType (1.1f));
                    }

                strokeCurve (g, wave (s, 1.0f, 0.0f, 1.0f), c, area.expanded (0.0f, 6.0f), area.getCentreY(), true);

                if (voices > 1)
                    label (g, juce::String (voices) + " voices", getLocalBounds().toFloat());

                drawChrome (g);
            }
        };

        //==============================================================================
        /** A modulation shape over one second (the synth's LFOs) or two (chorus, phaser, tremolo). */
        class Lfo : public PickerDisplay
        {
        public:
            Lfo (Values& vals, juce::StringArray idsToShow, bool synthLfo)
                : PickerDisplay (vals, std::move (idsToShow), { "Off", "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random" }), synth (synthLfo) {}

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                auto area = plotArea().reduced (synth ? pickerWidth - 6.0f : 2.0f, 2.0f);

                if (synth)
                    area = area.withTrimmedBottom (10.0f);

                const auto s = synth ? shape() : 1;
                const auto rate = synth ? v (1) : v (0);
                auto depth = synth ? v (2) : v (1);
                const auto c = tint (synth ? P::mod : P::signal);

                if (! synth)
                {
                    const auto range = values.rangeOf (ids[1]);
                    depth = range.end > 1.5f ? depth / range.end : depth;
                }

                const auto seconds = synth ? 1.0f : 2.0f;
                const auto cycles = juce::jlimit (0.25f, 16.0f, rate * seconds);

                if (s == 0)
                {
                    dashedLine (g, { area.getX(), area.getCentreY(), area.getRight(), area.getCentreY() }, Theme::textFaint.withAlpha (0.5f));
                    offNote (g, area.withTrimmedTop (area.getHeight() * 0.5f), "Off: pick a shape");
                    drawChrome (g);
                    return;
                }

                g.setColour (P::screenGridMajor);
                g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());

                const auto curve = curveThrough (area, [&] (float x)
                {
                    const auto phase = (x - area.getX()) / area.getWidth() * cycles;
                    const auto frac = phase - std::floor (phase);
                    float y = 0.0f;

                    switch (s)
                    {
                        case 1:  y = std::sin (frac * 2.0f * pi); break;
                        case 2:  y = frac < 0.5f ? 4.0f * frac - 1.0f : 3.0f - 4.0f * frac; break;
                        case 3:  y = 2.0f * frac - 1.0f; break;
                        case 4:  y = 1.0f - 2.0f * frac; break;
                        case 5:  y = frac < 0.5f ? 1.0f : -1.0f; break;
                        default: y = std::sin (std::floor (phase) * 12.9898f) * 0.9f; break;   // steps that look random
                    }

                    return area.getCentreY() - y * juce::jlimit (0.05f, 1.0f, depth) * area.getHeight() * 0.45f;
                });

                strokeCurve (g, curve, c, area, area.getCentreY(), true);
                label (g, juce::String (rate, rate < 10.0f ? 2 : 1) + " Hz", getLocalBounds().toFloat());

                if (synth)
                    drawChrome (g);
            }

            void mouseMove (const juce::MouseEvent& e) override     { if (synth) PickerDisplay::mouseMove (e); }
            void mouseDown (const juce::MouseEvent& e) override     { if (synth) PickerDisplay::mouseDown (e); }
            void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
            {
                if (synth) PickerDisplay::mouseWheelMove (e, w);
                else       Display::mouseWheelMove (e, w);
            }

        private:
            bool synth;
        };

        //==============================================================================
        /** Tracktion's 4-band EQ: the summed curve, each band's own, and a numbered handle per band. */
        class Eq4 : public Display
        {
        public:
            using Display::Display;

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea();
                drawFrequencyGrid (g, area, dbRange * 2.0f);
                const auto zeroY = area.getCentreY();

                // Each band's own contribution, faintly in its colour (the touched one stronger)
                for (int b = 0; b < 4; ++b)
                {
                    const auto band = curveThrough (area, [&] (float x) { return yFor (area, bandDb (b, hzForX (area, x))); });
                    const bool focus = b == hot || b == dragging;
                    const auto colour = tint (bandColour (b));

                    juce::Path shape (band);
                    shape.lineTo (area.getRight(), zeroY);
                    shape.lineTo (area.getX(), zeroY);
                    shape.closeSubPath();
                    g.setColour (colour.withAlpha (focus ? 0.12f : 0.05f));
                    g.fillPath (shape);
                    g.setColour (colour.withAlpha (focus ? 0.75f : 0.35f));
                    g.strokePath (band, juce::PathStrokeType (focus ? 1.5f : 1.0f));
                }

                const auto curve = curveThrough (area, [&] (float x) { return yFor (area, total (hzForX (area, x))); });
                strokeCurve (g, curve, tint (P::signal), area, zeroY, true);

                for (int b = 0; b < 4; ++b)
                    PluginLook::drawHandle (g, handle (b), tint (bandColour (b)), b == hot, b == dragging, juce::String (b + 1));

                const auto shown = dragging >= 0 ? dragging : hot;

                if (shown >= 0)
                    valueTag (g, juce::String (names[shown]) + sep() + hzText (v (shown * 3)) + sep()
                                   + (v (shown * 3 + 1) >= 0.0f ? "+" : "") + juce::String (v (shown * 3 + 1), 1) + " dB" + sep() + "Q " + juce::String (v (shown * 3 + 2), 2),
                              handle (shown), area, bandColour (shown));
                else
                    label (g, "Drag a band" + sep() + "wheel or Alt-drag: width" + sep() + "double-click: 0 dB", area, juce::Justification::topRight);
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

                start = { v (dragging * 3), v (dragging * 3 + 1), v (dragging * 3 + 2) };

                for (int k = 0; k < 3; ++k)
                    values.beginDrag (ids[dragging * 3 + k]);

                repaint();
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (dragging < 0)
                    return;

                const auto area = plotArea();
                const auto fine = e.mods.isShiftDown() ? 0.25f : 1.0f;

                if (e.mods.isAltDown())   // Alt: up and down change the width (Q)
                {
                    setClamped (dragging * 3 + 2, std::get<2> (start) * std::pow (2.0f, -(float) e.getDistanceFromDragStartY() * fine / 60.0f));
                    return;
                }

                const auto hz = hzForX (area, xForHz (area, std::get<0> (start)) + (float) e.getDistanceFromDragStartX() * fine);
                const auto db = std::get<1> (start) - (float) e.getDistanceFromDragStartY() * fine / (area.getHeight() * 0.5f) * dbRange;
                setClamped (dragging * 3, hz);
                setClamped (dragging * 3 + 1, db);
            }

            void mouseUp (const juce::MouseEvent&) override
            {
                if (dragging < 0)
                    return;

                for (int k = 0; k < 3; ++k)
                    values.endDrag (ids[dragging * 3 + k]);

                dragging = -1;
                repaint();
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
            static constexpr float dbRange = 18.0f;
            static constexpr const char* names[] { "Low shelf", "Mid 1", "Mid 2", "High shelf" };
            int hot = -1, dragging = -1;
            std::tuple<float, float, float> start;

            static juce::Colour bandColour (int b)      { return Theme::bandColours[juce::jlimit (0, 3, b)]; }

            static juce::String hzText (float hz)
            {
                return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
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
                float bestDistance = P::handleHitRadius;

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
        /** Input level (x) against output level (y), -60..0 dB, for compressor, gate and limiter.
            Where the plugin publishes levels, a ball rides the curve at the input level and meters show
            the input, the output and the gain reduction. The threshold handle can be dragged. */
        class Transfer : public Display
        {
        public:
            Transfer (Values& vals, juce::StringArray idsToShow, Type t) : Display (vals, std::move (idsToShow)), type (t) {}

            bool isLive() const override    { return values.getLevel ("in").has_value(); }

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto live = isLive() && active;
                auto full = plotArea();
                auto meters = live ? full.removeFromRight (40.0f) : juce::Rectangle<float>();
                const auto area = square (full);
                drawDbGrid (g, area, minDb, 0.0f);
                const auto c = tint (P::signal);

                // the region where it works, faintly amber
                const auto knee = kneeDb();
                const auto kneeX = xFor (area, knee);
                g.setColour (tint (P::gainReduction).withAlpha (0.06f));

                if (type == Type::gate)
                    g.fillRect (area.withRight (kneeX));
                else
                    g.fillRect (area.withLeft (kneeX));

                dashedLine (g, { toPoint (area, minDb, minDb), toPoint (area, 0.0f, 0.0f) }, Theme::textFaint.withAlpha (0.35f));

                juce::Path curve;

                for (float in = minDb; in <= 0.0f; in += 0.25f)
                {
                    const auto p = toPoint (area, in, out (in));

                    if (in == minDb) curve.startNewSubPath (p);
                    else             curve.lineTo (p);
                }

                strokeCurve (g, curve, c, area, area.getBottom(), true);

                const auto handle = toPoint (area, knee, out (knee));
                PluginLook::drawHandle (g, handle, tint (P::gainReduction), hot, dragging);

                if (live)
                {
                    // the ball: where the sound is on the curve right now, with a short trail
                    const auto in = smoothed (inLevel, *values.getLevel ("in"));
                    trail.insert (trail.begin(), in);
                    trail.resize (juce::jmin<size_t> (trail.size(), 8));

                    for (size_t i = trail.size(); i-- > 1;)
                    {
                        g.setColour (Theme::text.withAlpha (0.25f * (1.0f - (float) i / 8.0f)));
                        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (toPoint (area, trail[i], out (trail[i]))));
                    }

                    if (in > minDb)
                    {
                        g.setColour (Theme::text);
                        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (toPoint (area, in, out (in))));
                    }

                    drawMeters (g, meters, in);
                }

                if (dragging)
                    valueTag (g, caption(), handle, area, P::gainReduction);
                else
                    label (g, caption(), full, juce::Justification::topLeft);   // the handle lives top right
            }

            void mouseMove (const juce::MouseEvent& e) override
            {
                const auto near = handlePoint().getDistanceFrom (e.position) < P::handleHitRadius;

                if (near != hot)
                {
                    hot = near;
                    setMouseCursor (near ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
                    repaint();
                }
            }

            void mouseExit (const juce::MouseEvent&) override   { hot = false; repaint(); }

            void mouseDown (const juce::MouseEvent& e) override
            {
                if (handlePoint().getDistanceFrom (e.position) >= P::handleHitRadius)
                    return;

                dragging = true;
                start = { v (0), v (1) };
                values.beginDrag (ids[0]);
                values.beginDrag (ids[1]);
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (! dragging)
                    return;

                const auto area = square (plotArea().withTrimmedRight (isLive() ? 40.0f : 0.0f));
                const auto perDb = area.getWidth() / -minDb;
                const auto fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
                const auto dxDb = (float) e.getDistanceFromDragStartX() / perDb * fine;
                const auto dyDb = -(float) e.getDistanceFromDragStartY() / perDb * fine;

                switch (type)
                {
                    case Type::compressor:
                        if (e.mods.isAltDown())   // Alt: up and down change the ratio
                            setClamped (1, start.second - dyDb * 0.02f);
                        else
                            setClamped (0, juce::Decibels::decibelsToGain (juce::Decibels::gainToDecibels (juce::jmax (0.001f, start.first)) + dxDb));
                        break;

                    case Type::gate:        setClamped (0, start.first + dxDb); break;
                    case Type::limiter:     setClamped (1, start.second + dyDb); break;
                    default:                break;
                }
            }

            void mouseUp (const juce::MouseEvent&) override
            {
                if (! dragging)
                    return;

                values.endDrag (ids[0]);
                values.endDrag (ids[1]);
                dragging = false;
                repaint();
            }

        private:
            static constexpr float minDb = -60.0f;
            Type type;
            bool hot = false, dragging = false;
            std::pair<float, float> start;
            float inLevel = minDb, outLevel = minDb, reduction = 0.0f, reductionHold = 0.0f;
            int holdFrames = 0;
            std::vector<float> trail;

            static juce::Rectangle<float> square (juce::Rectangle<float> r)
            {
                const auto side = juce::jmin (r.getWidth(), r.getHeight());
                return r.withSizeKeepingCentre (side, side);
            }

            static float xFor (juce::Rectangle<float> area, float db)    { return juce::jmap (db, minDb, 0.0f, area.getX(), area.getRight()); }

            static juce::Point<float> toPoint (juce::Rectangle<float> area, float inDb, float outDb)
            {
                return { xFor (area, juce::jlimit (minDb, 0.0f, inDb)),
                         juce::jmap (juce::jlimit (minDb, 0.0f, outDb), minDb, 0.0f, area.getBottom(), area.getY()) };
            }

            juce::Point<float> handlePoint() const
            {
                const auto area = square (plotArea().withTrimmedRight (isLive() && active ? 40.0f : 0.0f));
                return toPoint (area, kneeDb(), out (kneeDb()));
            }

            void setClamped (int index, float value)
            {
                const auto range = values.rangeOf (ids[index]);
                values.set (ids[index], juce::jlimit (range.start, range.end, value));
            }

            /** Meters fall at about 20 dB a second (the editor refreshes at 20 Hz). */
            static float smoothed (float& state, float db)
            {
                state = juce::jmax (db, state - 1.0f);
                return state;
            }

            void drawMeters (juce::Graphics& g, juce::Rectangle<float> area, float in)
            {
                const auto outNow = smoothed (outLevel, values.getLevel ("out").value_or (minDb));
                const auto gr = values.getLevel ("gr").value_or (juce::jmin (0.0f, out (in) - in));
                reduction = juce::jmin (gr, reduction + 1.0f);

                if (reduction <= reductionHold)
                {
                    reductionHold = reduction;
                    holdFrames = 30;   // 1.5 s
                }
                else if (--holdFrames <= 0)
                {
                    reductionHold = juce::jmin (0.0f, reductionHold + 1.0f);
                }

                area = area.reduced (4.0f, 0.0f);
                const auto top = area.removeFromTop (14.0f);
                const auto w = 6.0f;
                const auto bars = area.withTrimmedBottom (12.0f);
                const auto inBar = bars.withWidth (w);
                const auto outBar = inBar.translated (w + 3.0f, 0.0f);
                const auto grBar = juce::Rectangle<float> (bars.getRight() - 10.0f, bars.getY(), 10.0f, bars.getHeight());

                drawMeter (g, inBar, in, P::signal.withAlpha (0.8f), false);
                drawMeter (g, outBar, outNow, P::signal, false);
                drawMeter (g, grBar, reduction, P::gainReduction, true);

                // peak hold tick
                const auto holdY = grBar.getY() + grBar.getHeight() * juce::jlimit (0.0f, 1.0f, -reductionHold / 24.0f);
                g.setColour (P::gainReduction);
                g.drawHorizontalLine (juce::roundToInt (holdY), grBar.getX(), grBar.getRight());

                g.setFont (Theme::font (Theme::captionFontSize - 0.5f, Theme::Weight::semibold));
                g.setColour (reduction < -0.1f ? P::gainReduction : P::screenLabel);
                g.drawText (juce::String (reduction, 1), top, juce::Justification::centredRight, false);
                g.setColour (P::screenLabel);
                g.drawText ("IO", juce::Rectangle<float> (inBar.getX(), bars.getBottom() + 1.0f, 18.0f, 10.0f), juce::Justification::centredLeft, false);
                g.drawText ("GR", juce::Rectangle<float> (grBar.getX() - 4.0f, bars.getBottom() + 1.0f, 16.0f, 10.0f), juce::Justification::centredRight, false);
            }

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
                const auto colour = tint (type == Type::deEsser ? P::gainReduction : P::signal);

                if (type == Type::ampTone)   // where each tone knob works
                {
                    const std::pair<float, const char*> marks[] { { v (0), "Tight" }, { 100.0f, "Bass" }, { 700.0f, "Mid" }, { 3000.0f, "Treble" }, { 5000.0f, "Presence" } };
                    g.setFont (Theme::font (Theme::captionFontSize - 0.5f));

                    int index = 0;

                    for (const auto& [hz, name] : marks)
                    {
                        // labels alternate between two lines so close ones (treble, presence) don't collide
                        const auto x = xForHz (area, hz);
                        const auto labelY = area.getY() + (index++ % 2 == 0 ? 0.0f : 11.0f);
                        g.setColour (juce::Colours::white.withAlpha (0.07f));
                        g.drawVerticalLine (juce::roundToInt (x), labelY + 11.0f, area.getBottom());
                        g.setColour (P::screenLabel);
                        g.drawText (name, juce::Rectangle<float> (x - 30.0f, labelY, 60.0f, 11.0f), juce::Justification::centred, false);
                    }
                }

                const auto curve = curveThrough (area, [&] (float x) { return yFor (db (hzForX (area, x))); });
                strokeCurve (g, curve, colour, area, area.getCentreY(), true);

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

            bool isLive() const override    { return values.getLevel ("in").has_value(); }

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                auto area = plotArea();
                area = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight() * 1.6f), area.getHeight());
                g.setColour (P::screenGridMajor);
                g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
                g.drawVerticalLine (juce::roundToInt (area.getCentreX()), area.getY(), area.getBottom());
                dashedLine (g, { area.getX(), juce::jmap (-1.0f, -1.2f, 1.2f, area.getBottom(), area.getY()),
                                 area.getRight(), juce::jmap (1.0f, -1.2f, 1.2f, area.getBottom(), area.getY()) }, Theme::textFaint.withAlpha (0.3f));

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

                const auto curve = curveThrough (area, [&] (float px)
                {
                    const auto x = juce::jmap (px, area.getX(), area.getRight(), -1.0f, 1.0f);
                    const auto y = juce::jlimit (-1.2f, 1.2f, mix * shape (x) + (1.0f - mix) * x);
                    return juce::jmap (y, -1.2f, 1.2f, area.getBottom(), area.getY());
                });

                strokeCurve (g, curve, tint (P::signal), area, area.getCentreY(), false);

                // how hard the input is driving it right now: the curve's live range
                if (isLive() && active)
                {
                    const auto peak = juce::Decibels::decibelsToGain (juce::jmax (-60.0f, smoothedIn = juce::jmax (*values.getLevel ("in"), smoothedIn - 1.0f)));
                    const auto x0 = juce::jmap (-peak, -1.0f, 1.0f, area.getX(), area.getRight());
                    const auto x1 = juce::jmap (peak, -1.0f, 1.0f, area.getX(), area.getRight());
                    g.setColour (P::signal.withAlpha (0.10f));
                    g.fillRect (juce::Rectangle<float> (x0, area.getY(), x1 - x0, area.getHeight()));
                }

                label (g, juce::String ("+") + juce::String (v (0), 1) + " dB drive", getLocalBounds().toFloat());
            }

        private:
            float smoothedIn = -60.0f;
        };

        //==============================================================================
        /** Echoes over time as pings on a beat ruler: the dry hit, then each repeat as loud as the
            feedback leaves it (above and below the line for ping-pong). Drag the first echo: time
            (left-right, snapping to note values; Alt: free) and feedback (up-down). */
        class Delay : public Display
        {
        public:
            Delay (Values& vals, juce::StringArray idsToShow, bool timeInBeats)
                : Display (vals, std::move (idsToShow)), inBeats (timeInBeats) {}

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                const auto area = plotArea().reduced (4.0f, 2.0f).withTrimmedBottom (12.0f);
                const auto lengthMs = juce::jmax (1.0f, inBeats ? v (0) * beatMs() : v (0));
                const auto windowMs = windowFor (lengthMs);
                const auto feedback = v (1) <= -30.0f ? 0.0f : juce::Decibels::decibelsToGain (v (1));
                const auto mix = juce::jlimit (0.0f, 1.0f, v (2));
                const bool pingPong = ids.size() > 3 && v (3) > -60.0f;
                const auto baseline = pingPong ? area.getCentreY() : area.getBottom();
                const auto c = tint (P::signal);

                drawRuler (g, area, windowMs);

                const auto ping = [&] (float ms, float level, juce::Colour colour, bool below, float glow)
                {
                    const auto x = area.getX() + ms / windowMs * area.getWidth();
                    const auto h = (pingPong ? area.getHeight() * 0.5f : area.getHeight()) * juce::jlimit (0.0f, 1.0f, level);
                    const auto top = below ? baseline + h : baseline - h;
                    g.setColour (colour.withAlpha (colour.getFloatAlpha() * 0.35f));
                    g.fillRect (juce::Rectangle<float> (x - 1.0f, juce::jmin (top, baseline), 2.0f, std::abs (top - baseline)));

                    if (glow > 0.0f)
                    {
                        g.setColour (colour.withAlpha (glow * 0.25f));
                        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ x, top }));
                    }

                    g.setColour (colour);
                    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, top }));
                    return juce::Point<float> (x, top);
                };

                if (pingPong)
                    g.setColour (P::screenGridMajor), g.drawHorizontalLine (juce::roundToInt (baseline), area.getX(), area.getRight());

                ping (0.0f, 1.0f - mix * 0.5f, Theme::textDim, false, 0.0f);   // the dry sound
                auto level = mix;

                for (int i = 1; i * lengthMs < windowMs && level > 0.01f; ++i)
                {
                    const auto age = (float) (i - 1) / 8.0f;
                    const auto p = ping ((float) i * lengthMs, level, c.withAlpha (juce::jlimit (0.3f, 1.0f, 1.0f - age)), pingPong && i % 2 == 0, i == 1 ? 1.0f : 0.0f);

                    if (i == 1)
                        firstEcho = p;

                    level *= feedback;
                }

                if (hot || dragging)
                    PluginLook::drawHandle (g, firstEcho, c, hot, dragging);

                const auto text = juce::String (juce::roundToInt (lengthMs)) + " ms" + noteName (lengthMs);

                if (dragging)
                    valueTag (g, text + sep() + "feedback " + juce::String (v (1), 1) + " dB", firstEcho, area, c);
                else
                    label (g, text, getLocalBounds().toFloat());
            }

            void mouseMove (const juce::MouseEvent& e) override
            {
                const auto near = ! inBeats && firstEcho.getDistanceFrom (e.position) < P::handleHitRadius;

                if (near != hot)
                {
                    hot = near;
                    setMouseCursor (near ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
                    repaint();
                }
            }

            void mouseExit (const juce::MouseEvent&) override   { hot = false; repaint(); }

            void mouseDown (const juce::MouseEvent& e) override
            {
                if (inBeats || firstEcho.getDistanceFrom (e.position) >= P::handleHitRadius)
                    return;

                dragging = true;
                start = { v (0), v (1) };
                startWindow = windowFor (start.first);
                values.beginDrag (ids[0]);
                values.beginDrag (ids[1]);
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (! dragging)
                    return;

                const auto area = plotArea();
                const auto fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
                auto ms = start.first + (float) e.getDistanceFromDragStartX() * fine / area.getWidth() * startWindow;

                if (! e.mods.isAltDown())
                    ms = snapToNote (ms);

                const auto lengthRange = values.rangeOf (ids[0]), feedbackRange = values.rangeOf (ids[1]);
                values.set (ids[0], juce::jlimit (lengthRange.start, lengthRange.end, std::round (ms)));
                values.set (ids[1], juce::jlimit (feedbackRange.start, feedbackRange.end, start.second - (float) e.getDistanceFromDragStartY() * fine * 0.25f));
            }

            void mouseUp (const juce::MouseEvent&) override
            {
                if (! dragging)
                    return;

                values.endDrag (ids[0]);
                values.endDrag (ids[1]);
                dragging = false;
                repaint();
            }

        private:
            bool inBeats, hot = false, dragging = false;
            std::pair<float, float> start;
            float startWindow = 1000.0f;
            juce::Point<float> firstEcho;

            static float windowFor (float lengthMs)     { return juce::jmax (1000.0f, lengthMs * 6.0f); }
            float beatMs() const                        { return (float) (60000.0 / juce::jmax (20.0, values.getBpm())); }

            void drawRuler (juce::Graphics& g, juce::Rectangle<float> area, float windowMs) const
            {
                const auto sixteenth = beatMs() / 4.0f;
                g.setFont (Theme::font (Theme::captionFontSize - 0.5f));

                for (int i = 0; (float) i * sixteenth <= windowMs; ++i)
                {
                    const auto x = area.getX() + (float) i * sixteenth / windowMs * area.getWidth();
                    const bool beat = i % 4 == 0, bar = i % 16 == 0;
                    g.setColour (bar ? P::screenGridMajor : beat ? P::screenGrid.withMultipliedAlpha (1.6f) : P::screenGrid);
                    g.drawVerticalLine (juce::roundToInt (x), bar ? area.getY() : area.getBottom() - (beat ? 10.0f : 5.0f), area.getBottom() + 2.0f);

                    if (bar && area.getHeight() > 60.0f)
                    {
                        g.setColour (P::screenLabel);
                        g.drawText ("bar " + juce::String (i / 16 + 1), juce::Rectangle<float> (x + 3.0f, area.getBottom() + 2.0f, 40.0f, 10.0f),
                                    juce::Justification::centredLeft, false);
                    }
                }
            }

            float snapToNote (float ms) const
            {
                const auto beat = beatMs();

                for (auto beats : { 4.0f, 3.0f, 2.0f, 1.5f, 1.0f, 2.0f / 3.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.375f, 0.25f, 1.0f / 6.0f, 0.125f })
                    if (std::abs (ms - beats * beat) < beats * beat * 0.04f)
                        return beats * beat;

                return ms;
            }

            juce::String noteName (float ms) const
            {
                const auto beat = (double) beatMs();
                const std::pair<double, const char*> notes[] { { 4.0, "1/1" }, { 2.0, "1/2" }, { 3.0, "1/2 dotted" }, { 1.0, "1/4" }, { 1.5, "1/4 dotted" },
                                                               { 0.75, "1/8 dotted" }, { 2.0 / 3.0, "1/4 triplet" }, { 0.5, "1/8" }, { 1.0 / 3.0, "1/8 triplet" },
                                                               { 0.375, "1/16 dotted" }, { 0.25, "1/16" }, { 1.0 / 6.0, "1/16 triplet" } };

                for (const auto& [beats, name] : notes)
                    if (std::abs (ms - beats * beat) < beats * beat * 0.02)
                        return juce::String (" = ") + name;

                return {};
            }
        };

        //==============================================================================
        /** The synth's distortion: a soft-clip curve that bends harder as the drive goes up. */
        class SoftClip : public Display
        {
        public:
            using Display::Display;

            void paint (juce::Graphics& g) override
            {
                drawBackground (g);
                auto area = plotArea();
                area = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight() * 1.8f), area.getHeight());
                g.setColour (P::screenGridMajor);
                g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
                const auto drive = 1.0f + juce::jlimit (0.0f, 1.0f, v (0)) * 12.0f;
                const auto curve = curveThrough (area, [&] (float px)
                {
                    const auto x = juce::jmap (px, area.getX(), area.getRight(), -1.0f, 1.0f);
                    return juce::jmap (std::tanh (x * drive) / std::tanh (drive), -1.1f, 1.1f, area.getBottom(), area.getY());
                });

                strokeCurve (g, curve, tint (P::signal), area, area.getCentreY(), false);
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
                const auto area = plotArea().reduced (2.0f, 2.0f);
                const auto freeze = v (4) >= 0.5f;
                const auto decaySeconds = 0.3f + v (0) * 4.5f;
                const auto wet = juce::jlimit (0.05f, 1.0f, v (2) * 1.6f);
                const auto windowSeconds = 5.0f;
                const auto c = tint (P::signal);

                for (int s = 1; s < 5; ++s)
                {
                    const auto x = area.getX() + area.getWidth() * (float) s / windowSeconds;
                    g.setColour (P::screenGrid);
                    g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
                    g.setColour (P::screenLabel);
                    g.setFont (Theme::font (Theme::captionFontSize - 0.5f));
                    g.drawText (juce::String (s) + " s", juce::Rectangle<float> (x + 2.0f, area.getBottom() - 10.0f, 24.0f, 10.0f), juce::Justification::centredLeft, false);
                }

                const auto tail = curveThrough (area, [&] (float px)
                {
                    const auto t = (px - area.getX()) / area.getWidth() * windowSeconds;
                    const auto env = freeze ? 1.0f : std::exp (-t * 6.9f / decaySeconds);   // -60 dB at the decay time
                    const auto shimmer = 0.85f + 0.15f * std::sin (t * 37.0f) * std::sin (t * 13.0f);
                    return area.getBottom() - area.getHeight() * wet * env * shimmer;
                });

                // Damping darkens the tail: less of the bright colour remains toward the end
                juce::Path fill (tail);
                fill.lineTo (area.getRight(), area.getBottom());
                fill.lineTo (area.getX(), area.getBottom());
                fill.closeSubPath();
                const auto dark = c.interpolatedWith (P::screen, 0.55f + v (1) * 0.35f);
                g.setGradientFill (juce::ColourGradient::horizontal (c.withAlpha (0.45f), area.getX(), dark.withAlpha (0.15f), area.getRight()));
                g.fillPath (fill);
                PluginLook::drawGlowingPath (g, tail, c, 1.5f);

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
            case Type::envelope:    return std::make_unique<Envelope> (values, ids, Theme::Plugin::env);
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
            case Type::delay:       return std::make_unique<Delay> (values, ids, false);
            case Type::reverb:      return std::make_unique<Reverb> (values, ids);
            case Type::keys:        return std::make_unique<Keys> (values, ids);
            case Type::stereo:      return std::make_unique<Stereo> (values, ids);
            case Type::modEnvelope: return std::make_unique<Envelope> (values, ids, Theme::Plugin::mod);
            case Type::softClip:    return std::make_unique<SoftClip> (values, ids);
            case Type::synthDelay:  return std::make_unique<Delay> (values, ids, true);
        }

        return {};
    }
}

//==============================================================================
#include "PluginLayouts.h"

namespace ez::PluginDisplays
{
    namespace
    {
        /** Reads a plugin's values for a preview: parameters, and the settings its layout lists. Read-only. */
        class PreviewValues : public Values
        {
        public:
            PreviewValues (std::function<te::Plugin*()> r, std::vector<PluginLayouts::Setting> s)
                : resolve (std::move (r)), settings (std::move (s)) {}

            float get (const juce::String& id) const override
            {
                auto* plugin = resolve();

                if (plugin == nullptr)
                    return 0.0f;

                if (auto param = plugin->getAutomatableParameterByID (id))
                    return param->getCurrentValue();

                for (const auto& s : settings)
                    if (s.id == id)
                    {
                        const auto stored = plugin->state.getProperty (juce::Identifier (id));

                        if (stored.isVoid())
                            return s.defaultValue;

                        if (! s.textValues.isEmpty())
                        {
                            const auto index = juce::jmax (0, s.textValues.indexOf (stored.toString()));
                            return index < (int) s.choiceValues.size() ? s.choiceValues[(size_t) index] : s.min + (float) index;
                        }

                        return (float) stored;
                    }

                return 0.0f;
            }

            juce::NormalisableRange<float> rangeOf (const juce::String& id) const override
            {
                if (auto* plugin = resolve())
                    if (auto param = plugin->getAutomatableParameterByID (id))
                        return param->valueRange;

                for (const auto& s : settings)
                    if (s.id == id)
                        return { s.min, s.max };

                return { 0.0f, 1.0f };
            }

            void beginDrag (const juce::String&) override   {}
            void set (const juce::String&, float) override  {}
            void endDrag (const juce::String&) override     {}

            double getBpm() const override
            {
                auto* plugin = resolve();
                return plugin != nullptr ? plugin->edit.tempoSequence.getBpmAt (te::TimePosition()) : 120.0;
            }

            std::optional<float> getLevel (const juce::String&) const override   { return {}; }

        private:
            std::function<te::Plugin*()> resolve;
            std::vector<PluginLayouts::Setting> settings;
        };

        class Preview : public juce::Component, private juce::Timer
        {
        public:
            Preview (std::unique_ptr<PreviewValues> v, Type type, juce::StringArray ids) : values (std::move (v))
            {
                display = create (type, *values, std::move (ids));
                addAndMakeVisible (*display);
                setInterceptsMouseClicks (false, false);   // a picture: the card's Open button edits
                startTimerHz (10);
            }

            void setActive (bool a)     { display->setActive (a); }
            void resized() override     { display->setBounds (getLocalBounds()); }

        private:
            std::unique_ptr<PreviewValues> values;
            std::unique_ptr<Display> display;

            void timerCallback() override   { display->refresh(); }
        };
    }

    std::unique_ptr<juce::Component> createPreview (te::Plugin& plugin, std::function<te::Plugin*()> resolve)
    {
        const auto layout = PluginLayouts::forPlugin (plugin);

        if (! layout.has_value())
            return {};

        for (const auto& page : layout->pages)
            for (const auto& row : page.rows)
                for (const auto& section : row)
                    if (section.display.has_value())
                        return std::make_unique<Preview> (std::make_unique<PreviewValues> (std::move (resolve), layout->settings),
                                                          *section.display, section.displayIds);

        return {};
    }
}
