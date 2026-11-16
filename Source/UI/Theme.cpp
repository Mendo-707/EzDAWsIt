/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "Theme.h"

namespace ez::Theme
{
    juce::Font font (float size, bool bold)
    {
        // Nothing smaller than the caption size: 9 px text was unreadable on a big screen
        return juce::Font (juce::FontOptions (juce::jmax (captionFontSize, size), bold ? juce::Font::bold : juce::Font::plain));
    }

    LookAndFeel::LookAndFeel()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme {
              background,   // windowBackground
              raised,       // widgetBackground
              bar,          // menuBackground
              border,       // outline
              text,         // defaultText
              raised,       // defaultFill
              text,         // highlightedText
              accent,       // highlightedFill
              text })       // menuText
    {
       #if JUCE_WINDOWS
        setDefaultSansSerifTypefaceName ("Segoe UI");   // crisper than the default at small sizes
       #endif

        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::TextButton::buttonColourId, raised);
        setColour (juce::TextButton::buttonOnColourId, activeFill);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, display);
        setColour (juce::ComboBox::outlineColourId, border);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff2b2f37));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, selectedRow);
        setColour (juce::Label::textColourId, text);
        setColour (juce::TextEditor::backgroundColourId, display);
        setColour (juce::TextEditor::outlineColourId, border);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff4a505c));
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff2b2f37));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, border);
        setColour (juce::TabbedComponent::outlineColourId, laneSeparator);
        setColour (juce::Slider::rotarySliderOutlineColourId, border);    // the design's knob: border-coloured track,
        setColour (juce::Slider::rotarySliderFillColourId, accent);       // accent value arc
        setColour (juce::Slider::trackColourId, accent);                  // fader fill
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                            bool isMouseOverButton, bool isButtonDown)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = button.getToggleState();
        auto fill = on ? button.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;

        if (isButtonDown)            fill = fill.brighter (0.15f);
        else if (isMouseOverButton)  fill = fill.brighter (0.07f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, (float) cornerRadius);
        g.setColour (on ? accent : border);
        g.drawRoundedRectangle (bounds, (float) cornerRadius, 1.0f);
    }

    juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int height)
    {
        return font (juce::jmin (baseFontSize, (float) height * 0.62f));   // 20 px buttons: 12 px text, not 10
    }

    void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
    {
        const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, (float) cornerRadius);
        g.setColour (box.hasKeyboardFocus (true) ? accent : box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, (float) cornerRadius, 1.0f);

        // Small chevron, right-aligned
        const auto cx = (float) width - 12.0f, cy = (float) height * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 3.5f, cy - 1.5f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)      { return font (baseFontSize - 0.5f); }
    juce::Font LookAndFeel::getPopupMenuFont()                      { return font (baseFontSize + 0.5f); }

    void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (juce::Colour (0xff4a505c));
        g.drawRect (0, 0, width, height);
    }

    void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tip, int width, int height)
    {
        const auto bounds = juce::Rectangle<int> (width, height).toFloat();
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (findColour (juce::TooltipWindow::textColourId));
        g.setFont (font (smallFontSize + 0.5f));
        g.drawFittedText (tip, juce::Rectangle<int> (width, height).reduced (6, 3), juce::Justification::centredLeft, 4);
    }

    void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& scrollBar, int x, int y, int width, int height, bool isVertical,
                                     int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
    {
        g.fillAll (Theme::bar);

        if (thumbSize <= 0)
            return;

        const auto thumb = isVertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                                      : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
        auto colour = scrollBar.findColour (juce::ScrollBar::thumbColourId);

        if (isMouseDown)       colour = colour.brighter (0.3f);
        else if (isMouseOver)  colour = colour.brighter (0.15f);

        g.setColour (colour);
        g.fillRoundedRectangle (thumb.toFloat().reduced (3.0f, 3.0f), 3.0f);
    }

    void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        // The design's faders: a thin dark track; horizontal ones fill with the accent up to a small light
        // thumb, vertical ones (the mixer) get a light fader cap with a dark centre line
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const auto enabled = slider.isEnabled();
        const auto capColour = slider.isColourSpecified (juce::Slider::thumbColourId) ? slider.findColour (juce::Slider::thumbColourId)
                                                                                       : juce::Colour (0xffd9dde3);

        if (style == juce::Slider::LinearHorizontal)
        {
            const auto track = area.withSizeKeepingCentre (area.getWidth(), 4.0f);
            g.setColour (display);
            g.fillRoundedRectangle (track, 2.0f);
            g.setColour (border);
            g.drawRoundedRectangle (track, 2.0f, 1.0f);

            g.setColour (slider.findColour (juce::Slider::trackColourId).withMultipliedAlpha (enabled ? 1.0f : 0.4f));
            g.fillRoundedRectangle (track.withRight (sliderPos).reduced (1.0f), 1.5f);

            const auto thumb = juce::Rectangle<float> (10.0f, juce::jmin (12.0f, area.getHeight())).withCentre ({ sliderPos, area.getCentreY() });
            g.setColour (capColour.withMultipliedAlpha (enabled ? 1.0f : 0.5f));
            g.fillRoundedRectangle (thumb, 2.0f);
            return;
        }

        const auto track = area.withSizeKeepingCentre (4.0f, area.getHeight() - 6.0f);
        g.setColour (display);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (border);
        g.drawRoundedRectangle (track, 2.0f, 1.0f);

        const auto capWidth = juce::jlimit (16.0f, 28.0f, area.getWidth() - 2.0f);
        const auto cap = juce::Rectangle<float> (capWidth, 20.0f).withCentre ({ area.getCentreX(), sliderPos });
        g.setColour (capColour.withMultipliedAlpha (enabled ? 1.0f : 0.5f));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (laneSeparator);
        g.drawRoundedRectangle (cap, 3.0f, 1.0f);
        g.setColour (background);
        g.fillRect (cap.withSizeKeepingCentre (capWidth - 8.0f, 2.0f));
    }

    void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto size = (float) juce::jmin (width, height);
        const auto line = juce::jlimit (1.5f, 3.0f, size * 0.12f);
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                                .withSizeKeepingCentre (size, size).reduced (line * 0.5f + 0.5f);
        const auto radius = bounds.getWidth() * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = startAngle + sliderPos * (endAngle - startAngle);

        if (radius <= 1.0f)
            return;

        // A raised disc inside the arc (the design's knob face)
        if (radius >= 7.0f)
        {
            const auto face = radius - line - 1.5f;
            g.setColour (raised);
            g.fillEllipse (juce::Rectangle<float> (face * 2.0f, face * 2.0f).withCentre (centre));
            g.setColour (laneSeparator);
            g.drawEllipse (juce::Rectangle<float> (face * 2.0f, face * 2.0f).withCentre (centre), 1.0f);
        }

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
        g.strokePath (track, juce::PathStrokeType (line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // The value arc starts at 0 when the range goes through it (pan: from the centre), else at the bottom
        const auto min = slider.getMinimum(), max = slider.getMaximum();
        const auto from = min < 0.0 && max > 0.0 ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle)
                                                 : startAngle;

        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.5f));
            g.strokePath (value, juce::PathStrokeType (line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Pointer from near the centre to the edge
        const auto tip = centre.getPointOnCircumference (radius, angle);
        const auto base = centre.getPointOnCircumference (radius * 0.25f, angle);
        g.setColour (text.withMultipliedAlpha (slider.isEnabled() ? 0.9f : 0.4f));
        g.drawLine ({ base, tip }, juce::jmax (1.5f, line * 0.8f));
    }
}
