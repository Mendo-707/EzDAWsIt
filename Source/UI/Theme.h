/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    All colours, sizes and fonts live here. Components never hard-code them.
    The look follows the UI design canvas (docs/ui/DESIGN-NOTES.md).
*/

#pragma once

#include "../Core/Common.h"

namespace ez::Theme
{
    //==============================================================================
    // Colours
    inline const juce::Colour background   { 0xff1b1d22 };   // window
    inline const juce::Colour panel        { 0xff262931 };   // panels: browser, headers, transport
    inline const juce::Colour bar          { 0xff202329 };   // app bar, panel title rows
    inline const juce::Colour raised       { 0xff2e323b };   // buttons and other raised controls
    inline const juce::Colour border       { 0xff3a3f4b };   // control outlines
    inline const juce::Colour statusBar    { 0xff16181c };
    inline const juce::Colour selectedRow  { 0xff2f3a4a };
    inline const juce::Colour activeFill   { 0xff1d3550 };   // a toggle that is on (with an accent border)
    inline const juce::Colour ink          { 0xff0e1114 };   // dark text on light/colour fills
    inline const juce::Colour display      { 0xff101114 };   // LCD-style readouts and text fields
    inline const juce::Colour displayText  { 0xff7fe0a8 };
    inline const juce::Colour displayLabel { 0xff7fa592 };   // small captions inside LCDs
    inline const juce::Colour text         { 0xffe6e6e6 };
    inline const juce::Colour textDim      { 0xffa3a9b4 };   // 6.2:1 on panel (the old #8A8F99 was 4.48:1)
    inline const juce::Colour accent       { 0xff4aa3ff };
    inline const juce::Colour play         { 0xff2f9a5c };
    inline const juce::Colour record       { 0xffd03a3a };
    inline const juce::Colour recordIcon   { 0xffff6b6b };
    inline const juce::Colour recordIdle   { 0xff5a2a2a };
    inline const juce::Colour danger       { 0xffff8a8a };

    inline const juce::Colour laneBackground    { 0xff20232a };
    inline const juce::Colour laneSeparator     { 0xff15171c };
    inline const juce::Colour gridBeat          { 0xff2a2e37 };
    inline const juce::Colour gridBar           { 0xff3a3f4b };
    inline const juce::Colour clipAudio         { 0xff3b78b8 };
    inline const juce::Colour clipMidi          { 0xff3fa36b };
    inline const juce::Colour clipText          { 0xfff2f2f2 };
    inline const juce::Colour selection         { 0xfff2c94c };
    inline const juce::Colour playhead          { 0xfff2f2f2 };
    inline const juce::Colour loopRegion        { 0x553fa36b };
    inline const juce::Colour loopRegionOff     { 0x33808080 };

    // Planned (roadmap) features: drawn complete, but dimmed, with a phase chip
    inline const juce::Colour chipFill     { 0xff2b2f38 };
    inline const juce::Colour chipBorder   { 0xff5b6272 };
    inline const juce::Colour chipText     { 0xffd3d8e0 };
    constexpr float plannedAlpha = 0.5f;

    //==============================================================================
    // Window and layout (see the "shell" in docs/ui/DESIGN-NOTES.md)
    constexpr int windowWidth      = 1280;
    constexpr int windowHeight     = 760;
    constexpr int minWindowWidth   = 900;
    constexpr int minWindowHeight  = 500;

    constexpr int appBarHeight       = 40;
    constexpr int transportHeight    = 44;
    constexpr int browserWidth       = 240;
    constexpr int detailHeaderHeight = 32;
    constexpr int statusBarHeight    = 22;
    constexpr int panelTitleHeight   = 34;

    constexpr int buttonHeight     = 26;   // app bar, panels
    constexpr int transportButtonHeight = 28;
    constexpr int iconButtonWidth  = 34;
    constexpr int cornerRadius     = 4;
    constexpr int gap              = 8;
    constexpr int groupGap         = 12;   // between groups of controls
    constexpr int toolbarRowHeight = 34;   // dialogs

    constexpr int positionDisplayWidth = 170;
    constexpr int bpmDisplayWidth  = 96;

    constexpr int dialogWidth      = 520;
    constexpr int dialogHeight     = 600;
    constexpr int largeDialogWidth = 800;
    constexpr int setupWidth       = 1000;
    constexpr int setupHeight      = 660;

    constexpr int rulerHeight      = 28;
    constexpr int tempoLaneHeight  = 22;
    constexpr int sectionsLaneHeight = 22;
    constexpr int trackHeight      = 84;
    constexpr int trackHeaderWidth = 260;
    constexpr int scrollBarSize    = 12;

    // Kept for the dialogs that still use them
    constexpr int buttonWidth      = 100;
    constexpr int wideButtonWidth  = 112;
    constexpr int transportButtonWidth = 80;
    constexpr int toggleButtonWidth = 90;
    constexpr int labelWidth       = 40;
    constexpr int bpmControlWidth  = 110;

    //==============================================================================
    // Type
    constexpr float baseFontSize     = 12.5f;
    constexpr float smallFontSize    = 11.0f;
    constexpr float captionFontSize  = 10.0f;   // UPPERCASE section labels
    constexpr float statusFontSize   = 11.5f;
    constexpr float titleFontSize    = 13.5f;
    constexpr float positionFontSize = 19.0f;

    juce::Font font (float size, bool bold = false);

    //==============================================================================
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOverButton, bool isButtonDown) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                            int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
        int getDefaultScrollbarWidth() override     { return scrollBarSize; }
        /** Knobs fill their bounds (JUCE's shrinks by 10 px a side: an 18 px knob drew nothing). Ranges
            through 0 (pan) draw their arc from 0. */
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
        /** The design's faders (thin track, accent fill, light thumb; a fader cap in the mixer). */
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
        int getSliderThumbRadius (juce::Slider& s) override     { return s.isHorizontal() ? 5 : 10; }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LookAndFeel)
    };
}
