/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "Widgets.h"

namespace ez
{
    //==============================================================================
    namespace
    {
        struct IconPart
        {
            const char* svg;
            bool filled = false;
        };

        // Path data in a 24x24 box (the same set as the UI design canvas). Circles are written as two arcs.
        std::vector<IconPart> partsFor (Icon icon)
        {
            switch (icon)
            {
                case Icon::play:          return { { "M7 5L19 12L7 19Z", true } };
                case Icon::stop:          return { { "M6 6H18V18H6Z", true } };
                case Icon::record:        return { { "M6 12A6 6 0 1 0 18 12A6 6 0 1 0 6 12Z", true } };
                case Icon::returnToStart: return { { "M6 5V19" }, { "M18 6L10 12L18 18Z", true } };
                case Icon::loop:          return { { "M17 2L21 6L17 10" }, { "M3 11V10A4 4 0 0 1 7 6H21" }, { "M7 22L3 18L7 14" }, { "M21 13V14A4 4 0 0 1 17 18H3" } };
                case Icon::metronome:     return { { "M9 3H15L18 21H6Z" }, { "M12 15L17 7" }, { "M7.5 16H16.5" } };
                case Icon::countIn:       return { { "M4 13A8 8 0 1 0 20 13A8 8 0 1 0 4 13Z" }, { "M12 9V13L14.5 15" }, { "M10 2H14" } };
                case Icon::magnet:        return { { "M5 4V11A7 7 0 0 0 19 11V4" }, { "M9 4V11A3 3 0 0 0 15 11V4" }, { "M5 8H9" }, { "M15 8H19" } };
                case Icon::undo:          return { { "M9 14L4 9L9 4" }, { "M4 9H14.5A5.5 5.5 0 0 1 14.5 20H11" } };
                case Icon::redo:          return { { "M15 14L20 9L15 4" }, { "M20 9H9.5A5.5 5.5 0 0 0 9.5 20H13" } };
                case Icon::newFile:       return { { "M14 3H6A2 2 0 0 0 4 5V19A2 2 0 0 0 6 21H18A2 2 0 0 0 20 19V9Z" }, { "M14 3V9H20" }, { "M12 12V18" }, { "M9 15H15" } };
                case Icon::open:          return { { "M3 7A2 2 0 0 1 5 5H9L11 7H19A2 2 0 0 1 21 9V17A2 2 0 0 1 19 19H5A2 2 0 0 1 3 17Z" } };
                case Icon::save:          return { { "M5 3H16L21 8V19A2 2 0 0 1 19 21H5A2 2 0 0 1 3 19V5A2 2 0 0 1 5 3Z" }, { "M7 3V8H15" }, { "M7 21V14H17V21" } };
                case Icon::sliders:       return { { "M4 6H13" }, { "M17 6H20" }, { "M13 6A2 2 0 1 0 17 6A2 2 0 1 0 13 6Z" }, { "M4 12H7" }, { "M11 12H20" },
                                                   { "M7 12A2 2 0 1 0 11 12A2 2 0 1 0 7 12Z" }, { "M4 18H15" }, { "M19 18H20" }, { "M15 18A2 2 0 1 0 19 18A2 2 0 1 0 15 18Z" } };
                case Icon::headphones:    return { { "M3 15V12A9 9 0 0 1 21 12V15" }, { "M3 15H6V21H4A1 1 0 0 1 3 20Z" }, { "M21 15H18V21H20A1 1 0 0 0 21 20Z" } };
                case Icon::speaker:       return { { "M11 5L6 9H2V15H6L11 19Z" }, { "M15.5 8.5A5 5 0 0 1 15.5 15.5" }, { "M19 5A10 10 0 0 1 19 19" } };
                case Icon::chevronDown:   return { { "M6 9L12 15L18 9" } };
                case Icon::chevronUp:     return { { "M6 15L12 9L18 15" } };
                case Icon::chevronRight:  return { { "M9 6L15 12L9 18" } };
                case Icon::search:        return { { "M4 11A7 7 0 1 0 18 11A7 7 0 1 0 4 11Z" }, { "M21 21L16.7 16.7" } };
                case Icon::piano:         return { { "M5 4H19A2 2 0 0 1 21 6V18A2 2 0 0 1 19 20H5A2 2 0 0 1 3 18V6A2 2 0 0 1 5 4Z" }, { "M8 4V13" }, { "M12 4V13" }, { "M16 4V13" }, { "M3 13H21" } };
                case Icon::mic:           return { { "M12 3A3 3 0 0 1 15 6V11A3 3 0 0 1 9 11V6A3 3 0 0 1 12 3Z" }, { "M5 11A7 7 0 0 0 19 11" }, { "M12 18V21" } };
                case Icon::plug:          return { { "M12 22V17" }, { "M9 8V2" }, { "M15 8V2" }, { "M18 8V13A4 4 0 0 1 14 17H10A4 4 0 0 1 6 13V8Z" } };
                case Icon::exportFile:    return { { "M12 15V3" }, { "M7 8L12 3L17 8" }, { "M5 15V19A2 2 0 0 0 7 21H17A2 2 0 0 0 19 19V15" } };
                case Icon::plus:          return { { "M12 5V19" }, { "M5 12H19" } };
                case Icon::close:         return { { "M18 6L6 18" }, { "M6 6L18 18" } };
                case Icon::layers:        return { { "M12 3L21 8L12 13L3 8Z" }, { "M3 13L12 18L21 13" } };
                case Icon::chain:         return { { "M10 13A5 5 0 0 0 17 13L20 10A5 5 0 0 0 13 3L12 4" }, { "M14 11A5 5 0 0 0 7 11L4 14A5 5 0 0 0 11 21L12 20" } };
                case Icon::file:          return { { "M14 3H6A2 2 0 0 0 4 5V19A2 2 0 0 0 6 21H18A2 2 0 0 0 20 19V9Z" }, { "M14 3V9H20" } };
                case Icon::note:          return { { "M9 18V5L21 3V16" }, { "M3 18A3 3 0 1 0 9 18A3 3 0 1 0 3 18Z" }, { "M15 16A3 3 0 1 0 21 16A3 3 0 1 0 15 16Z" } };
                case Icon::bookmark:      return { { "M6 3H18V21L12 17L6 21Z" } };
                case Icon::drum:          return { { "M4 7A8 3 0 1 0 20 7A8 3 0 1 0 4 7Z" }, { "M4 7V16C4 17.7 7.6 19 12 19S20 17.7 20 16V7" } };
                case Icon::help:          return { { "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12Z" }, { "M9.1 9A3 3 0 0 1 14.9 10C14.9 12 11.9 13 11.9 13" }, { "M12 17H12.01" } };
                case Icon::clock:         return { { "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12Z" }, { "M12 7V12L15 14" } };
                case Icon::zoomIn:        return { { "M4 11A7 7 0 1 0 18 11A7 7 0 1 0 4 11Z" }, { "M21 21L16.7 16.7" }, { "M8 11H14" }, { "M11 8V14" } };
                case Icon::zoomOut:       return { { "M4 11A7 7 0 1 0 18 11A7 7 0 1 0 4 11Z" }, { "M21 21L16.7 16.7" }, { "M8 11H14" } };
                case Icon::wand:          return { { "M15 4V2" }, { "M15 16V14" }, { "M8 9H10" }, { "M20 9H22" }, { "M3 21L12 12" } };
                case Icon::flag:          return { { "M5 21V4" }, { "M5 4H16L14 8L16 12H5" } };
                case Icon::power:         return { { "M12 2V12" }, { "M18.4 6.6A9 9 0 1 1 5.6 6.6" } };
                case Icon::none:          break;
            }

            return {};
        }

        struct ParsedPart
        {
            juce::Path path;
            bool filled;
        };

        const std::vector<ParsedPart>& parsedPartsFor (Icon icon)
        {
            // Parsed once per icon (message thread only)
            static std::map<Icon, std::vector<ParsedPart>> cache;
            auto& entry = cache[icon];

            if (entry.empty())
                for (const auto& part : partsFor (icon))
                    entry.push_back ({ juce::Drawable::parseSVGPath (part.svg), part.filled });

            return entry;
        }
    }

    void Icons::draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth)
    {
        if (icon == Icon::none || area.isEmpty())
            return;

        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        const auto scale = size / 24.0f;
        const auto transform = juce::AffineTransform::scale (scale)
                                   .translated (area.getCentreX() - size * 0.5f, area.getCentreY() - size * 0.5f);
        const juce::PathStrokeType stroke (strokeWidth * juce::jmax (0.75f, scale), juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded);
        g.setColour (colour);

        for (const auto& part : parsedPartsFor (icon))
        {
            auto path = part.path;
            path.applyTransform (transform);

            if (part.filled)
                g.fillPath (path);
            else
                g.strokePath (path, stroke);
        }
    }

    //==============================================================================
    IconButton::IconButton (const juce::String& name, Icon i, const juce::String& label, Style s)
        : juce::Button (name), icon (i), style (s)
    {
        setButtonText (label);
        setWantsKeyboardFocus (false);   // shortcuts must keep working after a click
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int IconButton::getIdealWidth (int height) const
    {
        const auto hasText = showText && getButtonText().isNotEmpty();

        if (! hasText)
            return getIconOnlyWidth (height);

        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (Theme::font (Theme::baseFontSize - 0.5f), getButtonText());
        const auto iconWidth = icon != Icon::none ? 16 + 6 : 0;
        return 10 + iconWidth + textWidth + 10;
    }

    void IconButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        const auto radius = (float) Theme::cornerRadius;

        // Segmented groups: square the joined edges
        const bool flatLeft = isConnectedOnLeft(), flatRight = isConnectedOnRight();
        juce::Path shape;
        shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius, radius,
                                   ! flatLeft, ! flatRight, ! flatLeft, ! flatRight);

        juce::Colour fill, outline, content;

        switch (style)
        {
            case Style::primary: fill = Theme::accent; outline = Theme::accent; content = Theme::ink; break;
            case Style::play:    fill = Theme::play;   outline = Theme::play;   content = Theme::ink; break;
            case Style::ghost:   fill = juce::Colours::transparentBlack; outline = juce::Colours::transparentBlack; content = Theme::text; break;
            case Style::normal:  fill = Theme::raised; outline = Theme::border; content = Theme::text; break;
        }

        if (on && (style == Style::normal || style == Style::ghost))
        {
            fill = Theme::activeFill;
            outline = Theme::accent;
            content = juce::Colours::white;
        }

        if (fillOverride.has_value())
        {
            fill = *fillOverride;
            outline = *fillOverride;
        }

        if (style == Style::ghost && ! on && (isMouseOver || isButtonDown))
            fill = Theme::raised;
        else if (isButtonDown)
            fill = fill.brighter (0.15f);
        else if (isMouseOver)
            fill = fill.brighter (0.07f);

        g.setColour (fill);
        g.fillPath (shape);
        g.setColour (outline);
        g.strokePath (shape, juce::PathStrokeType (1.0f));

        // Content: icon then text, centred as a group
        const auto hasText = showText && getButtonText().isNotEmpty();
        const auto iconSize = juce::jmin (16.0f, (float) getHeight() - 10.0f);
        const auto font = Theme::font (Theme::baseFontSize - 0.5f, on || style == Style::primary);
        const auto textWidth = hasText ? (float) juce::GlyphArrangement::getStringWidthInt (font, getButtonText()) : 0.0f;
        const auto iconWidth = icon != Icon::none ? iconSize : 0.0f;
        const auto contentWidth = iconWidth + (hasText && icon != Icon::none ? 6.0f : 0.0f) + textWidth;
        auto x = bounds.getCentreX() - contentWidth * 0.5f;

        if (icon != Icon::none)
        {
            Icons::draw (g, icon, { x, bounds.getCentreY() - iconSize * 0.5f, iconSize, iconSize }, iconColour.value_or (content));
            x += iconSize + 6.0f;
        }

        if (hasText)
        {
            g.setColour (content);
            g.setFont (font);
            g.drawText (getButtonText(), juce::Rectangle<float> (x, bounds.getY(), textWidth + 2.0f, bounds.getHeight()),
                        juce::Justification::centredLeft, false);
        }
    }

    //==============================================================================
    NavRow::NavRow (const juce::String& name, Icon i, bool chevron)
        : juce::Button (name), icon (i), showChevron (chevron)
    {
        setWantsKeyboardFocus (false);
    }

    void NavRow::paintButton (juce::Graphics& g, bool isMouseOver, bool)
    {
        const auto on = getToggleState();
        auto r = getLocalBounds();

        if (on || isMouseOver)
        {
            g.setColour (on ? Theme::selectedRow : Theme::raised);
            g.fillRoundedRectangle (r.toFloat(), (float) Theme::cornerRadius);
        }

        r.removeFromLeft (10);

        if (icon != Icon::none)
        {
            Icons::draw (g, icon, r.removeFromLeft (14).toFloat().withSizeKeepingCentre (14.0f, 14.0f), on ? Theme::accent : Theme::textDim);
            r.removeFromLeft (8);
        }

        if (showChevron)
            Icons::draw (g, Icon::chevronRight, r.removeFromRight (22).toFloat().withSizeKeepingCentre (11.0f, 11.0f), Theme::textDim);

        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::baseFontSize - 0.5f, on));
        g.drawText (getName(), r, juce::Justification::centredLeft, true);
    }

    //==============================================================================
    namespace
    {
        const juce::Identifier phaseProperty ("ezPlannedPhase");
    }

    juce::String Planned::describe (const juce::String& phase)
    {
        if (phase == "2C")   return "Coming in Phase 2C (piano roll)";
        if (phase == "2D")   return "Coming in Phase 2D (drum machine and step sequencer)";
        if (phase == "2E")   return "Coming in Phase 2E (tempo, key and sections)";
        if (phase == "P3")   return "Coming in Phase 3 (mixing)";
        if (phase == "P4")   return "Coming in Phase 4 (mastering, export and presets)";
        if (phase == "P5")   return "Coming in Phase 5 (pro polish)";
        if (phase == "Next") return "Coming next";
        return "Coming later";
    }

    void Planned::mark (juce::Component& component, const juce::String& phase)
    {
        // Command-bound buttons get re-enabled by the command manager, which would undo this
        jassert (dynamic_cast<juce::Button*> (&component) == nullptr
                 || dynamic_cast<juce::Button*> (&component)->getCommandID() == 0);

        component.getProperties().set (phaseProperty, phase);
        component.setEnabled (false);
        component.setAlpha (Theme::plannedAlpha);

        if (auto* tooltipClient = dynamic_cast<juce::SettableTooltipClient*> (&component))
            if (tooltipClient->getTooltip().isEmpty())
                tooltipClient->setTooltip (describe (phase));
    }

    bool Planned::isPlanned (const juce::Component& component)
    {
        return component.getProperties().contains (phaseProperty);
    }

    void Planned::drawChip (juce::Graphics& g, const juce::String& phase, juce::Point<int> topRight)
    {
        const auto chipFont = Theme::font (9.0f, true);
        static std::map<juce::String, int> widths;   // only a handful of phase names; measured once each
        auto [it, isNew] = widths.try_emplace (phase, 0);

        if (isNew)
            it->second = juce::GlyphArrangement::getStringWidthInt (chipFont, phase) + 8;

        const auto width = it->second;
        const juce::Rectangle<int> chip (topRight.x - width, topRight.y, width, 14);

        g.setColour (Theme::chipFill);
        g.fillRoundedRectangle (chip.toFloat(), 3.0f);
        g.setColour (Theme::chipBorder);
        g.drawRoundedRectangle (chip.toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setColour (Theme::chipText);
        g.setFont (chipFont);
        g.drawText (phase, chip, juce::Justification::centred, false);
    }

    void Planned::paintChips (juce::Graphics& g, juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (! child->isVisible() || ! isPlanned (*child))
                continue;

            const auto phase = child->getProperties()[phaseProperty].toString();

            if (phase.isEmpty())
                continue;

            // A badge over the top-right corner, kept inside the parent
            const auto b = child->getBounds();
            const auto x = juce::jmin (parent.getWidth() - 1, b.getRight() + 5);
            const auto y = juce::jmax (0, b.getY() - 7);
            drawChip (g, phase, { x, y });
        }
    }

    //==============================================================================
    void drawLcd (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setColour (Theme::display);
        g.fillRoundedRectangle (area.toFloat(), (float) Theme::cornerRadius);
        g.setColour (Theme::laneSeparator);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), (float) Theme::cornerRadius, 1.0f);
    }

    int drawLcdField (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& caption,
                      const juce::String& value, float valueSize)
    {
        const auto valueFont = Theme::font (valueSize, true);
        const auto width = juce::jmax (juce::GlyphArrangement::getStringWidthInt (valueFont, value),
                                       juce::GlyphArrangement::getStringWidthInt (Theme::font (9.0f, true), caption));

        auto column = area.withWidth (width);
        g.setColour (Theme::displayLabel);
        g.setFont (Theme::font (9.0f, true));
        g.drawText (caption, column.removeFromTop (area.getHeight() / 3), juce::Justification::bottomLeft, false);
        g.setColour (Theme::displayText);
        g.setFont (valueFont);
        g.drawText (value, column, juce::Justification::centredLeft, false);
        return width;
    }
}
