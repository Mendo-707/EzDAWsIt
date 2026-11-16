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
    // Colours. A layered dark palette (never pure black): each step up is one surface nearer
    // the user. One accent (electric blue), used for what is on, selected or the next action.
    inline const juce::Colour statusBar    { 0xff0f1115 };   // the deepest layer
    inline const juce::Colour background   { 0xff12141a };   // window, behind the main view
    inline const juce::Colour bar          { 0xff161920 };   // app bar, panel title rows
    inline const juce::Colour panel        { 0xff1a1d24 };   // panels: browser, track headers, transport
    inline const juce::Colour raised       { 0xff22262f };   // buttons, cards and other raised controls
    inline const juce::Colour hover        { 0xff2a2f3a };   // a raised control under the mouse
    inline const juce::Colour hairline     { 0xff2a2e38 };   // 1 px separators between regions
    inline const juce::Colour border       { 0xff343a46 };   // control outlines (a step stronger than hairlines)
    inline const juce::Colour selectedRow  { 0xff1f2b3d };
    inline const juce::Colour activeFill   { 0xff17304f };   // a toggle that is on (with an accent border)
    inline const juce::Colour ink          { 0xff0b0d10 };   // dark text on light/colour fills
    inline const juce::Colour display      { 0xff0d0f13 };   // readouts and text fields (recessed)
    inline const juce::Colour displayText  { 0xffeef1f5 };
    inline const juce::Colour displayLabel { 0xff7f8898 };   // small captions inside readouts
    inline const juce::Colour text         { 0xffeceef2 };
    inline const juce::Colour textDim      { 0xff9ba3b1 };   // secondary text: 6.6:1 on panel
    inline const juce::Colour textFaint    { 0xff6b7383 };   // tertiary (hints, units): 3.6:1, large or non-essential text only
    inline const juce::Colour accent       { 0xff4d9fff };
    inline const juce::Colour accentSoft   { 0x334d9fff };   // accent glow and tints
    inline const juce::Colour play         { 0xff30a46c };
    inline const juce::Colour record       { 0xffe5484d };
    inline const juce::Colour recordIcon   { 0xffff6369 };
    inline const juce::Colour recordIdle   { 0xff4a2326 };
    inline const juce::Colour mute         { 0xfff5b544 };   // mute buttons that are on
    inline const juce::Colour solo         { 0xff4d9fff };   // solo buttons that are on
    inline const juce::Colour warning      { 0xfff2b84c };
    inline const juce::Colour success      { 0xff3dd68c };
    inline const juce::Colour danger       { 0xffff8a8a };

    inline const juce::Colour laneBackground    { 0xff15181e };
    inline const juce::Colour laneAlternate     { 0xff171a21 };   // every other lane, so long rows stay easy to follow
    inline const juce::Colour laneSeparator     { 0xff0c0e12 };
    inline const juce::Colour gridBeat          { 0xff1d2028 };
    inline const juce::Colour gridBar           { 0xff2b303b };
    inline const juce::Colour clipAudio         { 0xff3b82d6 };   // clips on a track with no colour
    inline const juce::Colour clipMidi          { 0xff30a46c };
    inline const juce::Colour clipText          { 0xfff4f6f8 };
    inline const juce::Colour selection         { 0xfff2c94c };
    inline const juce::Colour playhead          { 0xfff4f6f8 };
    inline const juce::Colour loopRegion        { 0x5530a46c };
    inline const juce::Colour loopRegionOff     { 0x33808080 };

    // Meters: green, amber from -6 dB, red at 0 dB
    inline const juce::Colour meterLow     { 0xff3dd68c };
    inline const juce::Colour meterMid     { 0xfff5b544 };
    inline const juce::Colour meterHigh    { 0xffe5484d };

    // Planned (roadmap) features: drawn complete, but dimmed, with a phase chip
    inline const juce::Colour chipFill     { 0xff22262f };
    inline const juce::Colour chipBorder   { 0xff4a5160 };
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
    // Spacing scale (everything lines up on a 4 px grid)
    constexpr int space1 = 4, space2 = 8, space3 = 12, space4 = 16, space5 = 24;

    constexpr int cornerRadius     = 6;    // buttons, fields, menus
    constexpr int smallRadius      = 4;    // chips, clips, small toggles
    constexpr int cardRadius       = 8;    // cards and grouped controls
    constexpr int windowRadius     = 10;   // popups and floating panels
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
    constexpr float readoutFontSize  = 24.0f;   // big numbers on cards (loudness)

    /** The weights the UI uses: regular for labels and values, medium for controls,
        semibold for names and section headers, bold for big readouts. */
    enum class Weight { regular, medium, semibold, bold };

    /** The UI font (Segoe UI Variable where Windows has it, else Segoe UI). bold = semibold. */
    juce::Font font (float size, bool bold = false);
    juce::Font font (float size, Weight);

    /** Soft elevation under floating things (popups, cards being dragged): depth 1-3. */
    void drawShadow (juce::Graphics&, juce::Rectangle<float> area, float radius, int depth = 1);

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
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
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

        // Menus: rounded highlight inset from the edge, dimmed shortcut column, tick in the accent
        void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
        void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                        int& idealWidth, int& idealHeight) override;
        int getPopupMenuBorderSize() override                   { return 5; }
        void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;

        // The value bubble shown while dragging a fader or knob
        void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
        juce::Font getSliderPopupFont (juce::Slider&) override  { return font (smallFontSize + 0.5f, Weight::semibold); }
        int getSliderPopupPlacement (juce::Slider&) override    { return juce::BubbleComponent::above; }

        // Text fields: recessed, accent ring while typing
        void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

        // Messages and questions
        void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
        int getAlertWindowButtonHeight() override               { return buttonHeight + 4; }
        juce::Font getAlertWindowTitleFont() override           { return font (titleFontSize + 2.5f, Weight::semibold); }
        juce::Font getAlertWindowMessageFont() override         { return font (baseFontSize + 1.0f); }
        juce::Font getAlertWindowFont() override                { return font (baseFontSize); }

        // Check boxes: a rounded square, accent fill with a white tick when on; the label in the UI font
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isMouseOverButton, bool isButtonDown) override;
        void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h, bool ticked,
                          bool isEnabled, bool isMouseOverButton, bool isButtonDown) override;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LookAndFeel)
    };
}
