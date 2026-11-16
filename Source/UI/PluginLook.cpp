/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "PluginLook.h"

namespace ez
{
    namespace P = Theme::Plugin;

    PluginLook::PluginLook()
    {
        setColour (juce::Slider::rotarySliderFillColourId, P::signal);
        setColour (juce::Slider::rotarySliderOutlineColourId, P::knobTrack);
        setColour (juce::ComboBox::backgroundColourId, P::recess);
        setColour (juce::TextButton::buttonOnColourId, Theme::activeFill);
    }

    //==============================================================================
    float PluginLook::arcOrigin (juce::Slider& s)
    {
        const auto def = s.getProperties()["ezDefault"];

        if (! def.isVoid())
            return (float) s.valueToProportionOfLength ((double) def);

        if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
            return (float) s.valueToProportionOfLength (0.0);

        return 0.0f;
    }

    void PluginLook::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto& props = slider.getProperties();
        const bool off = (bool) props["ezOff"] || ! slider.isEnabled();
        const bool hero = (bool) props["ezHero"];
        const bool active = slider.isMouseOverOrDragging() && ! off;
        const auto size = (float) juce::jmin (width, height);
        const auto ring = size >= 60.0f ? 3.5f : size >= 40.0f ? 3.0f : 2.5f;
        const auto modGap = 3.5f;   // room outside the track for the modulation ring
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                                .withSizeKeepingCentre (size, size).reduced (modGap + ring * 0.5f);
        const auto radius = bounds.getWidth() * 0.5f;
        const auto centre = bounds.getCentre();

        if (radius <= 4.0f)
            return;

        const auto angleAt = [&] (float proportion) { return startAngle + juce::jlimit (0.0f, 1.0f, proportion) * (endAngle - startAngle); };
        const auto angle = angleAt (sliderPos);
        const auto stroke = juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // 1. Track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (active ? P::knobTrackHover : P::knobTrack);
        g.strokePath (track, stroke);

        // 2. Value arc from the default (bipolar knobs grow both ways from the centre), with a soft glow
        auto colour = off ? P::off : slider.findColour (juce::Slider::rotarySliderFillColourId);

        if (hero && ! off)   // the hero knob warms towards amber as it nears the top
            colour = colour.interpolatedWith (P::gainReduction, juce::jlimit (0.0f, 1.0f, (sliderPos - 0.6f) / 0.4f));

        const auto from = angleAt (arcOrigin (slider));

        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);

            if (! off)
            {
                g.setColour (colour.withAlpha (active ? 0.32f : 0.22f));
                g.strokePath (arc, juce::PathStrokeType (ring + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.setColour (colour);
            g.strokePath (arc, stroke);
        }

        // 3. Modulation range: a thin purple ring just outside the track
        if (props.contains ("ezModLo") && ! off)
        {
            const auto lo = angleAt ((float) (double) props["ezModLo"]);
            const auto hi = angleAt ((float) (double) props["ezModHi"]);
            const auto r = radius + ring * 0.5f + 2.5f;

            if (std::abs (hi - lo) > 0.01f)
            {
                juce::Path modArc;
                modArc.addCentredArc (centre.x, centre.y, r, r, 0.0f, juce::jmin (lo, hi), juce::jmax (lo, hi), true);
                g.setColour (P::mod);
                g.strokePath (modArc, juce::PathStrokeType (1.75f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        // 4. Cap: a machined disc lit from the top left, with a shadow below
        const auto capRadius = radius - ring * 0.5f - (size >= 40.0f ? 4.0f : 3.0f);

        if (capRadius > 3.0f)
        {
            const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
            const auto alpha = off ? P::offModuleAlpha : 1.0f;

            g.setColour (juce::Colours::black.withAlpha (0.45f * alpha));
            g.fillEllipse (cap.translated (0.0f, 1.5f).expanded (1.0f));

            auto top = P::capTop, bottom = P::capBottom;

            if (active)
            {
                top = top.brighter (0.12f);
                bottom = bottom.brighter (0.08f);
            }

            g.setGradientFill (juce::ColourGradient (top.withMultipliedAlpha (alpha), cap.getX(), cap.getY(),
                                                     bottom.withMultipliedAlpha (alpha), cap.getRight(), cap.getBottom(), false));
            g.fillEllipse (cap);

            // a lit rim along the top
            juce::Path rim;
            rim.addCentredArc (centre.x, centre.y, capRadius - 0.5f, capRadius - 0.5f, 0.0f,
                               -juce::MathConstants<float>::pi * 0.6f, juce::MathConstants<float>::pi * 0.6f, true);
            g.setColour (juce::Colours::white.withAlpha (0.09f * alpha));
            g.strokePath (rim, juce::PathStrokeType (1.0f));

            // 5. Indicator: a line (a dot on the hero knob), blue while touched
            const auto pointerColour = off ? Theme::textFaint : active ? P::signal : Theme::text;
            g.setColour (pointerColour);

            if (hero)
            {
                const auto dot = centre.getPointOnCircumference (capRadius * 0.72f, angle);
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));
            }
            else
            {
                juce::Path pointer;
                pointer.startNewSubPath (centre.getPointOnCircumference (capRadius * 0.32f, angle));
                pointer.lineTo (centre.getPointOnCircumference (capRadius * 0.82f, angle));
                g.strokePath (pointer, juce::PathStrokeType (size >= 40.0f ? 2.5f : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        // Automation is playing this knob: an amber dot at six o'clock
        if ((bool) props["ezAutomated"])
        {
            g.setColour (P::gainReduction);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ centre.x, centre.y + radius + 1.0f }));
        }
    }

    //==============================================================================
    void PluginLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                                           bool over, bool down)
    {
        const auto& props = b.getProperties();
        auto area = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        const bool enabled = b.isEnabled();

        if ((bool) props["ezSegment"])
        {
            // The recessed track (each segment draws its share, rounded only at the outer ends)
            const auto r = (float) P::controlRadius;
            const bool left = ! b.isConnectedOnLeft(), right = ! b.isConnectedOnRight();
            juce::Path trackShape;
            trackShape.addRoundedRectangle (area.getX(), area.getY(), area.getWidth(), area.getHeight(), r, r, left, right, left, right);
            g.setColour (P::recess);
            g.fillPath (trackShape);
            g.setColour (P::moduleEdgeLo);
            g.strokePath (trackShape, juce::PathStrokeType (1.0f));

            // the selected segment is a raised pill with a blue underline
            const auto pill = area.reduced (2.5f, 2.5f);

            if (on)
            {
                g.setGradientFill (juce::ColourGradient::vertical (P::capTop, pill.getY(), P::moduleBottom, pill.getBottom()));
                g.fillRoundedRectangle (pill, 4.5f);
                g.setColour (P::moduleEdgeHi.withMultipliedAlpha (2.0f));
                g.drawRoundedRectangle (pill, 4.5f, 1.0f);

                if (enabled)
                {
                    g.setColour (P::signal);
                    g.fillRoundedRectangle (pill.withTop (pill.getBottom() - 2.0f).reduced (pill.getWidth() * 0.22f, 0.0f), 1.0f);
                }
            }
            else if (over && enabled)
            {
                g.setColour (juce::Colours::white.withAlpha (down ? 0.07f : 0.04f));
                g.fillRoundedRectangle (pill, 4.5f);
            }

            return;
        }

        if ((bool) props["ezToggle"])
        {
            const auto box = area.reduced (0.5f);
            const auto lamp = props.contains ("ezLamp") ? juce::Colour ((juce::uint32) (int) props["ezLamp"]) : P::signal;

            g.setGradientFill (juce::ColourGradient::vertical (on ? Theme::activeFill.brighter (0.08f) : P::capTop.withAlpha (over ? 1.0f : 0.85f), box.getY(),
                                                               on ? Theme::activeFill : P::moduleBottom, box.getBottom()));
            g.fillRoundedRectangle (box, (float) P::controlRadius);
            g.setColour (on && enabled ? lamp.withAlpha (0.55f) : P::moduleEdgeLo);
            g.drawRoundedRectangle (box, (float) P::controlRadius, 1.0f);

            // the lamp
            const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ box.getX() + 11.0f, box.getCentreY() });

            if (on && enabled)
            {
                g.setColour (lamp.withAlpha (0.3f));
                g.fillEllipse (dot.expanded (3.0f));
                g.setColour (lamp);
            }
            else
            {
                g.setColour (Theme::textFaint.withAlpha (0.6f));
            }

            g.fillEllipse (dot);
            return;
        }

        Theme::LookAndFeel::drawButtonBackground (g, b, backgroundColour, over, down);
    }

    void PluginLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
    {
        const auto& props = b.getProperties();

        if (! (bool) props["ezSegment"] && ! (bool) props["ezToggle"])
            return Theme::LookAndFeel::drawButtonText (g, b, over, down);

        const bool on = b.getToggleState();
        auto area = b.getLocalBounds();
        const auto colour = ! b.isEnabled() ? Theme::textFaint : on ? Theme::text : over ? Theme::text.withAlpha (0.85f) : Theme::textDim;

        if (const auto icon = props["ezIcon"].toString(); icon.isNotEmpty())
            return drawShapeIcon (g, area.toFloat().reduced (8.0f, 7.0f), icon, on && b.isEnabled() ? P::signal : colour);

        if ((bool) props["ezToggle"])
            area.removeFromLeft (16);

        g.setFont (Theme::font (Theme::baseFontSize - 0.5f, on ? Theme::Weight::semibold : Theme::Weight::medium));
        g.setColour (colour);
        g.drawFittedText (b.getButtonText(), area.reduced (4, 0), juce::Justification::centred, 1, 0.85f);
    }

    void PluginLook::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
    {
        const auto area = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        const bool over = box.isMouseOver (true) && box.isEnabled();
        g.setColour (P::recess);
        g.fillRoundedRectangle (area, (float) P::controlRadius);
        g.setColour (over ? Theme::border.brighter (0.2f) : P::moduleEdgeLo);
        g.drawRoundedRectangle (area, (float) P::controlRadius, 1.0f);

        // a chevron on the right
        const auto c = juce::Point<float> ((float) width - 13.0f, (float) height * 0.5f);
        juce::Path chevron;
        chevron.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
        chevron.lineTo (c.x, c.y + 2.0f);
        chevron.lineTo (c.x + 4.0f, c.y - 2.0f);
        g.setColour (box.isEnabled() ? Theme::textDim : Theme::textFaint);
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void PluginLook::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body)
    {
        const auto box = body.reduced (0.5f);
        const auto radius = box.getHeight() * 0.5f;
        Theme::drawShadow (g, box, radius, 2);
        g.setColour (P::screen);
        g.fillRoundedRectangle (box, radius);
        g.setColour (P::signal.withAlpha (0.85f));
        g.drawRoundedRectangle (box, radius, 1.0f);
    }

    juce::Font PluginLook::getSliderPopupFont (juce::Slider&)
    {
        return Theme::font (Theme::smallFontSize + 0.5f, Theme::Weight::semibold);
    }

    void PluginLook::drawShapeIcon (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name, juce::Colour colour)
    {
        // a filter's shape, as it looks on the response screen
        area = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 30.0f), juce::jmin (area.getHeight(), 14.0f));
        const auto l = area.getX(), r = area.getRight(), t = area.getY(), b = area.getBottom(), m = area.getCentreX(), mid = area.getCentreY();
        juce::Path p;

        if (name == "lp")           { p.startNewSubPath (l, mid); p.lineTo (m, mid); p.quadraticTo (m + 6.0f, mid - 4.0f, r, b); }
        else if (name == "hp")      { p.startNewSubPath (l, b); p.quadraticTo (m - 6.0f, mid - 4.0f, m, mid); p.lineTo (r, mid); }
        else if (name == "bp")      { p.startNewSubPath (l, b); p.quadraticTo (m - 3.0f, t - 2.0f, m, t); p.quadraticTo (m + 3.0f, t - 2.0f, r, b); }
        else if (name == "notch")   { p.startNewSubPath (l, t + 2.0f); p.lineTo (m - 5.0f, t + 2.0f); p.quadraticTo (m, b + 4.0f, m + 5.0f, t + 2.0f); p.lineTo (r, t + 2.0f); }
        else
        {
            // off: a flat line through a circle
            g.setColour (colour);
            g.drawEllipse (area.withSizeKeepingCentre (11.0f, 11.0f), 1.4f);
            p.startNewSubPath (m - 7.0f, mid + 7.0f);
            p.lineTo (m + 7.0f, mid - 7.0f);
        }

        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    //==============================================================================
    void PluginLook::drawModule (juce::Graphics& g, juce::Rectangle<float> area, bool on)
    {
        const auto r = (float) P::cardRadius;
        juce::Path shape;
        shape.addRoundedRectangle (area, r);
        juce::DropShadow (P::moduleShadow, 12, { 0, 4 }).drawForPath (g, shape);

        g.setGradientFill (juce::ColourGradient::vertical (on ? P::moduleTop : P::moduleTop.darker (0.12f), area.getY(),
                                                           on ? P::moduleBottom : P::moduleBottom.darker (0.12f), area.getBottom()));
        g.fillPath (shape);
        g.setColour (P::moduleEdgeLo);
        g.strokePath (shape, juce::PathStrokeType (1.0f));

        // the lit top edge
        g.setColour (P::moduleEdgeHi);
        g.drawHorizontalLine (juce::roundToInt (area.getY()) + 1, area.getX() + r * 0.7f, area.getRight() - r * 0.7f);
    }

    void PluginLook::drawScreen (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto r = (float) P::screenRadius;
        juce::Path shape;
        shape.addRoundedRectangle (area, r);
        g.setColour (P::screen);
        g.fillPath (shape);

        // inner shadow along the top: the screen sits below the card's surface
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shape);
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::black.withAlpha (0.7f), area.getY(),
                                                           juce::Colours::black.withAlpha (0.0f), area.getY() + 7.0f));
        g.fillRect (area.withHeight (7.0f));
        g.setColour (juce::Colours::white.withAlpha (0.035f));   // a faint lower lip
        g.drawHorizontalLine (juce::roundToInt (area.getBottom()) - 1, area.getX() + r, area.getRight() - r);
    }

    void PluginLook::drawGlowingPath (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float thickness)
    {
        g.setColour (colour.withAlpha (0.2f));
        g.strokePath (path, juce::PathStrokeType (thickness + 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void PluginLook::drawHandle (juce::Graphics& g, juce::Point<float> centre, juce::Colour colour, bool hot, bool dragging, const juce::String& label)
    {
        const auto r = P::handleRadius + (hot || dragging ? 1.0f : 0.0f);
        const auto circle = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre);

        if (dragging || hot)
        {
            g.setColour (colour.withAlpha (dragging ? 0.22f : 0.12f));
            g.fillEllipse (circle.expanded (dragging ? 7.0f : 4.0f));
        }

        g.setColour (hot || dragging ? colour : P::screen);
        g.fillEllipse (circle);
        g.setColour (colour);
        g.drawEllipse (circle.reduced (1.0f), 2.0f);

        if (label.isNotEmpty())
        {
            g.setColour (hot || dragging ? Theme::ink : colour);
            g.setFont (Theme::font (Theme::captionFontSize - 0.5f, Theme::Weight::bold));
            g.drawText (label, circle, juce::Justification::centred, false);
        }
    }

    void PluginLook::drawPower (juce::Graphics& g, juce::Rectangle<float> area, bool on, bool hovered)
    {
        const auto circle = area.withSizeKeepingCentre (22.0f, 22.0f);

        if (on)
        {
            g.setColour (P::signal.withAlpha (0.18f));
            g.fillEllipse (circle.expanded (3.0f));
            g.setColour (Theme::activeFill);
            g.fillEllipse (circle);
            g.setColour (P::signal.withAlpha (0.65f));
            g.drawEllipse (circle.reduced (0.5f), 1.0f);
        }
        else if (hovered)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillEllipse (circle);
        }

        // the power symbol: an open ring and a stem
        const auto c = circle.getCentre();
        const auto rr = 5.0f;
        juce::Path symbol;
        symbol.addCentredArc (c.x, c.y + 0.5f, rr, rr, 0.0f, juce::MathConstants<float>::pi * 0.22f, juce::MathConstants<float>::twoPi - juce::MathConstants<float>::pi * 0.22f, true);
        symbol.startNewSubPath (c.x, c.y - 6.5f);
        symbol.lineTo (c.x, c.y - 0.5f);
        g.setColour (on ? P::signal : hovered ? Theme::textDim : Theme::textFaint);
        g.strokePath (symbol, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
