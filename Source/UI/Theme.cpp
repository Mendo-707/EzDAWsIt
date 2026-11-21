/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "Theme.h"

namespace ez::Theme
{
    namespace
    {
        /** The family and the style names it offers for each weight, found once. Windows 11 has the
            variable Segoe UI (crisper, true medium/semibold); Windows 10 has the classic one. */
        struct FontFamily
        {
            juce::String name;
            juce::String styles[4];   // regular, medium, semibold, bold ("" = use the bold flag)

            FontFamily()
            {
               #if JUCE_WINDOWS
                const auto families = juce::Font::findAllTypefaceNames();

                for (auto candidate : { "Segoe UI Variable Text", "Segoe UI" })
                    if (families.contains (candidate))
                    {
                        name = candidate;
                        break;
                    }
               #endif

                if (name.isEmpty())
                    return;   // the platform's default sans serif, with the bold flag for weights

                const auto available = juce::Font::findAllTypefaceStyles (name);
                const auto pick = [&available] (std::initializer_list<const char*> wanted) -> juce::String
                {
                    for (auto w : wanted)
                        if (available.contains (w, true))
                            return w;

                    return {};
                };

                styles[0] = pick ({ "Regular" });
                styles[1] = pick ({ "Medium", "Semilight", "Regular" });
                styles[2] = pick ({ "Semibold", "SemiBold", "Bold" });
                styles[3] = pick ({ "Bold" });
            }
        };

        const FontFamily& family()
        {
            static const FontFamily f;
            return f;
        }
    }

    juce::Font font (float size, bool bold)
    {
        return font (size, bold ? Weight::semibold : Weight::regular);
    }

    juce::Font font (float size, Weight weight)
    {
        // Nothing smaller than the caption size: 9 px text was unreadable on a big screen
        size = juce::jmax (captionFontSize, size);
        const auto& f = family();
        const auto style = f.styles[(int) weight];

        if (f.name.isNotEmpty() && style.isNotEmpty())
            return juce::Font (juce::FontOptions (f.name, style, size));

        const auto heavy = weight == Weight::semibold || weight == Weight::bold;
        return f.name.isNotEmpty() ? juce::Font (juce::FontOptions (f.name, size, heavy ? juce::Font::bold : juce::Font::plain))
                                   : juce::Font (juce::FontOptions (size, heavy ? juce::Font::bold : juce::Font::plain));
    }

    void drawShadow (juce::Graphics& g, juce::Rectangle<float> area, float radius, int depth)
    {
        depth = juce::jlimit (1, 3, depth);
        juce::Path shape;
        shape.addRoundedRectangle (area, radius);
        juce::DropShadow (juce::Colours::black.withAlpha (0.22f + 0.08f * (float) depth), 6 * depth, { 0, 2 * depth }).drawForPath (g, shape);
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
        setDefaultSansSerifTypefaceName (juce::Font::findAllTypefaceNames().contains ("Segoe UI Variable Text") ? "Segoe UI Variable Text"
                                                                                                             : "Segoe UI");
       #endif

        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::TextButton::buttonColourId, raised);
        setColour (juce::TextButton::buttonOnColourId, activeFill);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, display);
        setColour (juce::ComboBox::outlineColourId, border);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::PopupMenu::backgroundColourId, raised);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, activeFill);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::PopupMenu::headerTextColourId, textDim);
        setColour (juce::Label::textColourId, text);
        setColour (juce::TextEditor::backgroundColourId, display);
        setColour (juce::TextEditor::outlineColourId, border);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::ListBox::outlineColourId, border);
        setColour (juce::TableHeaderComponent::backgroundColourId, raised);
        setColour (juce::TableHeaderComponent::textColourId, textDim);
        setColour (juce::TableHeaderComponent::outlineColourId, border);
        setColour (juce::TableHeaderComponent::highlightColourId, activeFill);
        setColour (juce::TableListBox::backgroundColourId, display);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff3b414e));
        setColour (juce::TooltipWindow::backgroundColourId, raised);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, border);
        setColour (juce::TabbedComponent::outlineColourId, laneSeparator);
        setColour (juce::Slider::rotarySliderOutlineColourId, border);    // the design's knob: border-coloured track,
        setColour (juce::Slider::rotarySliderFillColourId, accent);       // accent value arc
        setColour (juce::Slider::trackColourId, accent);                  // fader fill
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::BubbleComponent::backgroundColourId, raised);
        setColour (juce::BubbleComponent::outlineColourId, border);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::outlineColourId, border);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, juce::Colours::white);
        setColour (juce::ToggleButton::tickDisabledColourId, textDim);
        setColour (juce::CaretComponent::caretColourId, accent);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
        setColour (juce::ProgressBar::backgroundColourId, display);
        setColour (juce::ProgressBar::foregroundColourId, accent);
        setColour (juce::ListBox::outlineColourId, hairline);
        setColour (juce::GroupComponent::outlineColourId, hairline);
        setColour (juce::GroupComponent::textColourId, textDim);
        setColour (juce::DocumentWindow::textColourId, text);
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                            bool isMouseOverButton, bool isButtonDown)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = button.getToggleState();
        auto fill = on ? button.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;
        const auto radius = juce::jmin ((float) cornerRadius, bounds.getHeight() * 0.3f);

        if (isButtonDown)            fill = fill.brighter (0.12f);
        else if (isMouseOverButton)  fill = fill == raised ? hover : fill.brighter (0.08f);

        if (! button.isEnabled())
            fill = fill.withMultipliedAlpha (0.5f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, radius);

        // A faint top highlight gives raised buttons depth without a gradient
        if (! on && ! isButtonDown && bounds.getHeight() > 14.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.035f));
            g.fillRoundedRectangle (bounds.withHeight (bounds.getHeight() * 0.5f), radius);
        }

        const auto outline = on ? button.findColour (juce::TextButton::buttonOnColourId) == activeFill ? accent
                                                                                                       : button.findColour (juce::TextButton::buttonOnColourId).brighter (0.2f)
                                : isMouseOverButton ? border.brighter (0.12f) : border;
        g.setColour (outline.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }

    juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int height)
    {
        return font (juce::jmin (baseFontSize, (float) height * 0.62f), Weight::medium);   // 20 px buttons: 12 px text, not 10
    }

    void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
    {
        const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId).brighter (box.isMouseOver (true) ? 0.04f : 0.0f));
        g.fillRoundedRectangle (bounds, (float) cornerRadius);
        g.setColour (box.hasKeyboardFocus (true) ? accent : box.isMouseOver (true) ? border.brighter (0.15f)
                                                                                   : box.findColour (juce::ComboBox::outlineColourId));
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
        g.setColour (border);
        g.drawRect (0, 0, width, height);
    }

    juce::String tidyShortcut (const juce::String& raw)
    {
        // JUCE lists every key for a command ("ctrl + =, ctrl + shift + =, ..."): show the first, written "Ctrl+="
        auto first = raw.upToFirstOccurrenceOf (",", false, false).trim();

        // a key without modifiers is described as "shortcut: 'g'"
        if (first.startsWithIgnoreCase ("shortcut:"))
            first = first.fromFirstOccurrenceOf (":", false, false).trim().unquoted().toUpperCase();

        if (! first.containsChar (' '))
            return first;

        juce::StringArray parts;
        parts.addTokens (first, "+", {});
        juce::StringArray tidy;

        for (auto part : parts)
        {
            part = part.trim();

            if (part.isEmpty())
                part = "+";   // "ctrl + +" splits into an empty part: it was the plus key

            tidy.add (part.length() == 1 ? part.toUpperCase() : part.substring (0, 1).toUpperCase() + part.substring (1));
        }

        tidy.removeEmptyStrings();
        return tidy.joinIntoString ("+").replace ("++", "+");
    }

    std::pair<juce::String, juce::String> splitMenuText (const juce::String& itemText, const juce::String& shortcutKeyText)
    {
        // Items written "Name\tKeys" or "Name (Ctrl+X)" get the same right-hand shortcut column as command items
        auto label = itemText.upToFirstOccurrenceOf ("\t", false, false);
        auto keys = shortcutKeyText.isNotEmpty() ? tidyShortcut (shortcutKeyText) : itemText.fromFirstOccurrenceOf ("\t", false, false);

        // JUCE measures command items as "Name   keys": measure what we draw
        if (keys.isEmpty() && label.contains ("   "))
        {
            keys = tidyShortcut (label.fromFirstOccurrenceOf ("   ", false, false));
            label = label.upToFirstOccurrenceOf ("   ", false, false);
        }

        if (keys.isEmpty() && label.endsWithChar (')'))
        {
            const auto inBrackets = label.fromLastOccurrenceOf ("(", false, false).dropLastCharacters (1);

            if (inBrackets.startsWith ("Ctrl+") || inBrackets.startsWith ("Shift+") || inBrackets.startsWith ("Alt+"))
            {
                keys = inBrackets;
                label = label.upToLastOccurrenceOf ("(", false, false).trimEnd();
            }
        }

        return { label, keys };
    }

    void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                         bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& itemText,
                                         const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour)
    {
        if (isSeparator)
        {
            g.setColour (hairline.brighter (0.15f));
            g.fillRect (area.reduced (8, 0).withSizeKeepingCentre (area.getWidth() - 16, 1));
            return;
        }

        auto r = area.reduced (3, 1);
        auto colour = textColour != nullptr ? *textColour : text;

        if (isHighlighted && isActive)
        {
            g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle (r.toFloat(), (float) smallRadius);
            colour = juce::Colours::white;
        }

        if (! isActive)
            colour = colour.withMultipliedAlpha (0.45f);

        r.reduce (6, 0);
        const auto iconArea = r.removeFromLeft (r.getHeight()).toFloat();

        if (icon != nullptr)
        {
            icon->drawWithin (g, iconArea.reduced (2.0f), juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
        }
        else if (isTicked)
        {
            const auto box = iconArea.withSizeKeepingCentre (14.0f, 14.0f);
            g.setColour (isActive ? accent : textDim);
            g.fillRoundedRectangle (box, 3.0f);
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
            tick.lineTo (box.getX() + 6.0f, box.getBottom() - 4.0f);
            tick.lineTo (box.getRight() - 3.0f, box.getY() + 4.0f);
            g.setColour (juce::Colours::white);
            g.strokePath (tick, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        r.removeFromLeft (4);

        if (hasSubMenu)
        {
            const auto arrow = r.removeFromRight (12).toFloat();
            juce::Path chevron;
            chevron.startNewSubPath (arrow.getCentreX() - 2.0f, arrow.getCentreY() - 4.0f);
            chevron.lineTo (arrow.getCentreX() + 2.0f, arrow.getCentreY());
            chevron.lineTo (arrow.getCentreX() - 2.0f, arrow.getCentreY() + 4.0f);
            g.setColour (colour.withMultipliedAlpha (0.8f));
            g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const auto itemFont = getPopupMenuFont();
        const auto [label, keys] = splitMenuText (itemText, shortcutKeyText);

        if (keys.isNotEmpty())
        {
            g.setFont (font (smallFontSize + 0.5f));
            g.setColour (isHighlighted && isActive ? juce::Colours::white.withAlpha (0.75f) : textFaint.withMultipliedAlpha (isActive ? 1.0f : 0.6f));
            g.drawText (keys, r, juce::Justification::centredRight, false);
            r.removeFromRight (juce::GlyphArrangement::getStringWidthInt (font (smallFontSize + 0.5f), keys) + 12);
        }

        g.setFont (itemFont);
        g.setColour (colour);
        g.drawFittedText (label, r, juce::Justification::centredLeft, 1);
    }

    void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& itemText, bool isSeparator, int standardMenuItemHeight,
                                                 int& idealWidth, int& idealHeight)
    {
        if (isSeparator)
        {
            idealWidth = 50;
            idealHeight = 9;
            return;
        }

        idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : 26;
        // Text after a tab is the shortcut column: leave room for both
        const auto [main, keys] = splitMenuText (itemText, {});
        idealWidth = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), main)
                   + (keys.isNotEmpty() ? juce::GlyphArrangement::getStringWidthInt (font (smallFontSize + 0.5f), keys) + 24 : 0)
                   + idealHeight + 40;
    }

    void LookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
    {
        g.setFont (font (captionFontSize, Weight::semibold));
        g.setColour (textDim);
        g.drawFittedText (sectionName.toUpperCase(), area.reduced (12, 0).withTrimmedTop (4), juce::Justification::centredLeft, 1);
    }

    void LookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body)
    {
        const auto box = body.reduced (0.5f);
        g.setColour (raised);
        g.fillRoundedRectangle (box, (float) smallRadius);
        g.setColour (accent.withAlpha (0.7f));
        g.drawRoundedRectangle (box, (float) smallRadius, 1.0f);
    }

    void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
    {
        g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), (float) cornerRadius);
    }

    void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
    {
        if (! editor.isEnabled())
            return;

        const auto focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
        const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        const auto outline = editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId);

        if (outline.isTransparent())
            return;

        if (focused)
        {
            g.setColour (accentSoft);
            g.drawRoundedRectangle (bounds.reduced (1.0f), (float) cornerRadius - 1.0f, 2.0f);   // a soft focus ring inside the edge
        }

        g.setColour (outline);
        g.drawRoundedRectangle (bounds, (float) cornerRadius, 1.0f);
    }

    void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
    {
        const auto bounds = alert.getLocalBounds().toFloat();
        g.setColour (alert.findColour (juce::AlertWindow::backgroundColourId));
        g.fillRoundedRectangle (bounds, (float) windowRadius);
        g.setColour (alert.findColour (juce::AlertWindow::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), (float) windowRadius, 1.0f);

        // A thin accent line along the top: the message is from EzDAWsIt, not a system dialog
        g.setColour (alert.getAlertType() == juce::MessageBoxIconType::WarningIcon ? warning : accent);
        g.fillRoundedRectangle (bounds.withHeight (3.0f).reduced ((float) windowRadius, 0.0f), 1.5f);

        g.setColour (alert.findColour (juce::AlertWindow::textColourId));
        layout.draw (g, textArea.toFloat().withY (24.0f).withHeight ((float) alert.getHeight() - (float) getAlertWindowButtonHeight() - 30.0f)
                                    .withX (bounds.getX() + 20.0f).withWidth (bounds.getWidth() - 40.0f));
    }

    void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isMouseOverButton, bool isButtonDown)
    {
        const auto box = juce::jmin (16.0f, (float) button.getHeight() - 4.0f);
        drawTickBox (g, button, 2.0f, ((float) button.getHeight() - box) * 0.5f, box, box,
                     button.getToggleState(), button.isEnabled(), isMouseOverButton, isButtonDown);

        g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.setFont (font (baseFontSize));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (juce::roundToInt (box) + 9).withTrimmedRight (2),
                          juce::Justification::centredLeft, 2);
    }

    void LookAndFeel::drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h, bool ticked,
                                   bool isEnabled, bool isMouseOverButton, bool)
    {
        const auto box = juce::Rectangle<float> (x, y, w, h).withSizeKeepingCentre (juce::jmin (w, h, 16.0f), juce::jmin (w, h, 16.0f)).reduced (0.5f);
        const auto alpha = isEnabled ? 1.0f : 0.45f;

        g.setColour ((ticked ? accent : display).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour ((ticked ? accent : isMouseOverButton ? border.brighter (0.2f) : border).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);

        if (ticked)
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + box.getWidth() * 0.25f, box.getCentreY());
            tick.lineTo (box.getX() + box.getWidth() * 0.43f, box.getBottom() - box.getHeight() * 0.28f);
            tick.lineTo (box.getRight() - box.getWidth() * 0.22f, box.getY() + box.getHeight() * 0.3f);
            g.setColour (juce::Colours::white.withMultipliedAlpha (alpha));
            g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tip, int width, int height)
    {
        const auto bounds = juce::Rectangle<int> (width, height).toFloat();
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (bounds, (float) smallRadius);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), (float) smallRadius, 1.0f);
        g.setColour (findColour (juce::TooltipWindow::textColourId));
        g.setFont (font (smallFontSize + 0.5f));
        g.drawFittedText (tip, juce::Rectangle<int> (width, height).reduced (8, 4), juce::Justification::centredLeft, 4);
    }

    juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (tip, font (smallFontSize + 0.5f), text);
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, 380.0f);

        const auto w = (int) std::ceil (layout.getWidth()) + 18, h = (int) std::ceil (layout.getHeight()) + 10;
        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
                   .constrainedWithin (parentArea);
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
        g.setColour (hairline);
        g.drawRoundedRectangle (track, 2.0f, 1.0f);

        // The level below the cap, faintly in the accent: the fader's position reads at a glance
        g.setColour (slider.findColour (juce::Slider::trackColourId).withAlpha (enabled ? 0.45f : 0.2f));
        g.fillRoundedRectangle (track.withTop (sliderPos).reduced (1.0f, 0.0f), 1.5f);

        // The cap: a small raised block, light at the top, with a dark grip line
        const auto capWidth = juce::jlimit (16.0f, 26.0f, area.getWidth() - 2.0f);
        const auto cap = juce::Rectangle<float> (capWidth, 16.0f).withCentre ({ area.getCentreX(), sliderPos });
        drawShadow (g, cap, 3.0f, 1);
        const auto light = capColour.withMultipliedAlpha (enabled ? 1.0f : 0.5f);
        g.setGradientFill (juce::ColourGradient::vertical (light, cap.getY(), light.darker (0.25f), cap.getBottom()));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (laneSeparator);
        g.drawRoundedRectangle (cap, 3.0f, 1.0f);
        g.setColour (ink.withAlpha (0.8f));
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

        // A raised disc inside the arc (the knob face): lit from above, with a soft shadow
        if (radius >= 7.0f)
        {
            const auto face = radius - line - 1.5f;
            const auto disc = juce::Rectangle<float> (face * 2.0f, face * 2.0f).withCentre (centre);
            const auto hovered = slider.isMouseOverOrDragging() && slider.isEnabled();

            if (face >= 8.0f)
            {
                juce::Path shadowShape;
                shadowShape.addEllipse (disc);
                juce::DropShadow (juce::Colours::black.withAlpha (0.4f), juce::roundToInt (face * 0.35f), { 0, 1 }).drawForPath (g, shadowShape);
            }

            g.setGradientFill (juce::ColourGradient::vertical (hovered ? hover.brighter (0.12f) : hover.brighter (0.06f), disc.getY(),
                                                               raised.darker (0.15f), disc.getBottom()));
            g.fillEllipse (disc);
            g.setColour (laneSeparator);
            g.drawEllipse (disc, 1.0f);
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

        // Pointer: a short rounded line on the face, pointing at the value
        const auto baseRadius = radius * (radius >= 7.0f ? 0.3f : 0.1f);
        const auto tip = centre.getPointOnCircumference (juce::jmax (baseRadius + 2.5f, radius - line - 2.5f), angle);
        const auto base = centre.getPointOnCircumference (baseRadius, angle);
        juce::Path pointer;
        pointer.startNewSubPath (base);
        pointer.lineTo (tip);
        g.setColour (text.withMultipliedAlpha (slider.isEnabled() ? 0.95f : 0.4f));
        g.strokePath (pointer, juce::PathStrokeType (juce::jmax (1.5f, line * 0.75f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
