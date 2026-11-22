/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Small building blocks shared by every panel:
      Icons        - a 24x24 stroke icon set, drawn from SVG path data
      IconButton   - icon and/or text, in a few styles; works with commands
      Planned      - greys out a roadmap feature and labels it with its phase
      drawLcd      - the dark "display" box used for position, tempo, etc.
*/

#pragma once

#include "Theme.h"

namespace ez
{
    //==============================================================================
    enum class Icon
    {
        none, play, stop, record, returnToStart, loop, metronome, countIn, magnet, undo, redo,
        newFile, open, save, sliders, headphones, speaker, chevronDown, chevronUp, chevronRight,
        search, piano, mic, plug, exportFile, plus, close, layers, chain, file, note, bookmark,
        drum, help, clock, zoomIn, zoomOut, wand, flag, power,
        pointer, scissors, eraser, snowflake, bolt
    };

    namespace Icons
    {
        /** Draws an icon (24x24 design) scaled into the area. */
        void draw (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour, float strokeWidth = 1.8f);
    }

    //==============================================================================
    /** A slider whose right-click opens a menu (onMenu) instead of starting a drag. */
    /** Dims the window while a dialog is open (fades in and out), so the dialog reads as the thing to answer.
        Ignores popup menus. Never takes clicks: the dialog is modal anyway. */
    class ModalBackdrop : public juce::Component, private juce::Timer
    {
    public:
        ModalBackdrop();
        void paint (juce::Graphics&) override;
        static bool isDialogOpen();

    private:
        void timerCallback() override;
        float alpha = 0.0f;
    };

    class MenuSlider : public juce::Slider
    {
    public:
        std::function<void()> onMenu;

        void mouseDown (const juce::MouseEvent& e) override
        {
            menuClick = e.mods.isPopupMenu();

            if (! menuClick)
                juce::Slider::mouseDown (e);
            else if (onMenu)
                onMenu();
        }

        void mouseDrag (const juce::MouseEvent& e) override     { if (! menuClick) juce::Slider::mouseDrag (e); }
        void mouseUp (const juce::MouseEvent& e) override       { if (! std::exchange (menuClick, false)) juce::Slider::mouseUp (e); }

    private:
        bool menuClick = false;
    };

    /** A text button whose right-click opens a menu (onMenu) instead of clicking it. */
    class MenuTextButton : public juce::TextButton
    {
    public:
        std::function<void()> onMenu;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onMenu)
                onMenu();
            else
                juce::TextButton::mouseDown (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! (e.mods.isPopupMenu() && onMenu))
                juce::TextButton::mouseUp (e);
        }
    };

    class IconButton : public juce::Button
    {
    public:
        enum class Style
        {
            normal,     // raised, outlined
            ghost,      // no background until hovered
            primary,    // accent fill
            play        // green fill, dark icon
        };

        IconButton (const juce::String& name, Icon, const juce::String& label = {}, Style = Style::normal);

        void setIcon (Icon newIcon)                         { icon = newIcon; repaint(); }
        void setIconColour (juce::Colour c)                 { iconColour = c; repaint(); }
        void setShowText (bool shouldShow)                  { showText = shouldShow; repaint(); }

        /** A solid fill that overrides the style (e.g. Record turns red while recording). */
        void setFillOverride (std::optional<juce::Colour> c) { fillOverride = c; repaint(); }

        /** Width this button wants at a height (icon, gaps and text). */
        int getIdealWidth (int height) const;
        int getIconOnlyWidth (int height) const             { return height + 4; }

        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    private:
        Icon icon;
        Style style;
        std::optional<juce::Colour> iconColour, fillOverride;
        bool showText = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
    };

    //==============================================================================
    /** A full-width row in a list of choices (browser categories, setup pages): icon, name, optional chevron. */
    class NavRow : public juce::Button
    {
    public:
        NavRow (const juce::String& name, Icon, bool showChevron);
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    private:
        Icon icon;
        bool showChevron;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NavRow)
    };

    //==============================================================================
    namespace Planned
    {
        /** Marks a roadmap feature: disabled, dimmed, tooltip "Coming in ...".
            Never on a command-bound button (the command manager re-enables those): bind the command
            when the feature ships, in the same change that removes the mark.
            `phase` is the roadmap tag: "2C", "2D", "2E", "P3", "P4", "P5" or "Next". */
        void mark (juce::Component&, const juce::String& phase);

        bool isPlanned (const juce::Component&);

        /** Call from a parent's paintOverChildren: draws the phase chip on each marked, visible child.
            Children marked with an empty phase are dimmed but get no chip (repeats of one feature). */
        void paintChips (juce::Graphics&, juce::Component& parent);

        /** Draws one chip with its top-right corner near a point. */
        void drawChip (juce::Graphics&, const juce::String& phase, juce::Point<int> topRight);

        juce::String describe (const juce::String& phase);
    }

    //==============================================================================
    /** A raised card (modules, panels, strips): soft shadow when elevated, a gradient from a touch
        lighter at the top, a hairline edge and a 1 px highlight along the top. Highlighted = selected. */
    void drawCard (juce::Graphics&, juce::Rectangle<float>, bool elevated = true, bool highlighted = false,
                   float radius = (float) Theme::cardRadius);

    /** A recessed well (readouts, graphs, empty slots): darker than its card, shaded at the top. */
    void drawWell (juce::Graphics&, juce::Rectangle<float>, float radius = (float) Theme::cornerRadius);

    /** A small UPPERCASE section label (INSERTS, SENDS, ...). */
    void drawCaption (juce::Graphics&, const juce::String&, juce::Rectangle<int>,
                      juce::Justification = juce::Justification::centredLeft);

    //==============================================================================
    /** The dark readout box (position, tempo, ...). */
    void drawLcd (juce::Graphics&, juce::Rectangle<int>);

    /** A caption above a value inside an LCD box: returns the width used. */
    int drawLcdField (juce::Graphics&, juce::Rectangle<int> area, const juce::String& caption,
                      const juce::String& value, float valueSize);
}
