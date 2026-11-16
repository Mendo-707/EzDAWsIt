/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The look of every plug-in window (docs/ui/DESIGN-NOTES.md, "Plug-in windows"): machined knobs
    with a glowing arc, recessed segmented controls, lit toggles and the value bubble. Components
    choose a variant with properties instead of code, so one change here restyles every plug-in.

    Knob (juce::Slider) properties:
        "ezDefault"   double  the arc grows from here (default: 0 when the range crosses it, else the left stop)
        "ezModLo/Hi"  double  0..1 modulation range, drawn as a purple ring outside the track
        "ezAutomated" bool    an amber dot: automation is playing this knob
        "ezOff"       bool    the module is off: grey arc, dimmed cap
        "ezHero"      bool    the one big knob of a plug-in: a dot instead of a line, warmer near the top
    TextButton properties:
        "ezSegment"   bool    one segment of a recessed segmented control
        "ezToggle"    bool    an on/off button with a lamp
        "ezLamp"      int     colour of the lamp (ARGB), default the signal blue
        "ezIcon"      String  draw a filter shape instead of the text (see drawShapeIcon)
*/

#pragma once

#include "Theme.h"

namespace ez
{
    class PluginLook : public Theme::LookAndFeel
    {
    public:
        PluginLook();   // plug-in windows share one through juce::SharedResourcePointer<PluginLook>

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOverButton, bool isButtonDown) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
        juce::Font getSliderPopupFont (juce::Slider&) override;
        int getSliderPopupPlacement (juce::Slider&) override    { return juce::BubbleComponent::above; }

        /** The arc's start for a knob: its "ezDefault", else 0 inside the range, else the minimum (as a proportion). */
        static float arcOrigin (juce::Slider&);

        /** Shared drawing used by cards and screens. */
        static void drawModule (juce::Graphics&, juce::Rectangle<float> area, bool on);
        static void drawScreen (juce::Graphics&, juce::Rectangle<float> area);
        static void drawGlowingPath (juce::Graphics&, const juce::Path&, juce::Colour, float thickness = 2.0f);
        static void drawHandle (juce::Graphics&, juce::Point<float> centre, juce::Colour, bool hot, bool dragging, const juce::String& label = {});
        static void drawPower (juce::Graphics&, juce::Rectangle<float> area, bool on, bool hovered);
        /** A filter shape for buttons ("ezIcon" property): "off", "lp", "hp", "bp", "notch". */
        static void drawShapeIcon (juce::Graphics&, juce::Rectangle<float> area, const juce::String& name, juce::Colour);

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginLook)
    };
}
