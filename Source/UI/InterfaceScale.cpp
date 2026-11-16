/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "InterfaceScale.h"
#include "Theme.h"

namespace ez::InterfaceScale
{
    namespace
    {
        const char* settingKey = "ezInterfaceScale";

        /** The work area of the primary screen at 100% (after Windows' own scaling). */
        juce::Rectangle<float> unscaledScreen()
        {
            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();

            if (display == nullptr)
                return { 1280.0f, 760.0f };

            const auto now = juce::Desktop::getInstance().getGlobalScaleFactor();
            return { display->userBounds.getWidth() * now, display->userBounds.getHeight() * now };
        }
    }

    void fitWindows()
    {
        auto& desktop = juce::Desktop::getInstance();

        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            auto* window = dynamic_cast<juce::ResizableWindow*> (desktop.getComponent (i));

            if (window == nullptr || ! window->isVisible() || window->isFullScreen() || window->isMinimised())
                continue;

            const auto* display = desktop.getDisplays().getDisplayForRect (window->getScreenBounds());

            if (display == nullptr)
                continue;

            // Include the native title bar, or clamping could push it off the top (and the window can't be dragged)
            juce::BorderSize<int> frame;

            if (auto* peer = window->getPeer())
                if (const auto size = peer->getFrameSizeIfPresent())
                    frame = *size;

            const auto area = display->userBounds.getSmallestIntegerContainer().reduced (1);
            const auto outer = frame.addedTo (window->getBounds());

            if (area.contains (outer))
                continue;

            const auto fitted = outer.withSize (juce::jmin (outer.getWidth(), area.getWidth()),
                                                juce::jmin (outer.getHeight(), area.getHeight()))
                                     .constrainedWithin (area);
            window->setBounds (frame.subtractedFrom (fitted));
        }
    }

    namespace
    {
        /** The biggest size that still leaves room for the smallest window on this screen. */
        float largestThatFits()
        {
            const auto screen = unscaledScreen();
            return juce::jmax (1.0f, juce::jmin (screen.getWidth() / (float) (Theme::minWindowWidth + 40),
                                                 screen.getHeight() / (float) (Theme::minWindowHeight + 80)));
        }
    }

    const juce::Array<float>& steps()
    {
        static const juce::Array<float> list { 0.9f, 1.0f, 1.1f, 1.25f, 1.4f, 1.5f, 1.75f, 2.0f };
        return list;
    }

    float getSetting (te::Engine& engine)
    {
        const auto value = (float) engine.getPropertyStorage().getPropertiesFile().getDoubleValue (settingKey, 0.0);
        return value >= 0.5f && value <= 3.0f ? value : 0.0f;
    }

    float largest()
    {
        auto best = steps().getFirst();

        for (auto s : steps())
            if (s <= largestThatFits() + 0.001f)
                best = s;

        return best;
    }

    float automatic()
    {
        // Logical width: what Windows' own scaling already left. A 2560 px wide screen at 100%
        // shows the design's 12 px text tiny; at 150% Windows scaling it's already 1707 wide
        const auto screen = unscaledScreen();
        const auto width = screen.getWidth();
        auto choice = width >= 3400 ? 1.75f : width >= 2900 ? 1.5f : width >= 2300 ? 1.25f : width >= 1900 ? 1.1f : 1.0f;

        // Ultrawide screens: the usual 1280 x 760 window must still fit in height
        while (choice > 1.0f && screen.getHeight() / choice < 760.0f + 100.0f)
            choice = steps()[juce::jmax (0, steps().indexOf (choice) - 1)];

        return choice;
    }

    float current()
    {
        return juce::Desktop::getInstance().getGlobalScaleFactor();
    }

    void apply (te::Engine& engine, float setting)
    {
        auto& props = engine.getPropertyStorage().getPropertiesFile();
        props.setValue (settingKey, (double) setting);
        props.saveIfNeeded();

        const auto scale = setting > 0 ? juce::jmin (setting, largest()) : automatic();

        if (std::abs (scale - current()) > 0.001f)
        {
            juce::Desktop::getInstance().setGlobalScaleFactor (scale);
            fitWindows();
        }
    }

    void step (te::Engine& engine, int direction)
    {
        const auto& list = steps();
        const auto now = current();
        auto next = now;

        if (direction > 0)
        {
            for (auto s : list)
                if (s > now + 0.001f) { next = juce::jmin (s, largest()); break; }
        }
        else
        {
            for (int i = list.size(); --i >= 0;)
                if (list[i] < now - 0.001f) { next = list[i]; break; }
        }

        apply (engine, next);
    }

    void applySaved (te::Engine& engine)
    {
        const auto setting = getSetting (engine);
        juce::Desktop::getInstance().setGlobalScaleFactor (setting > 0 ? juce::jmin (setting, largest()) : automatic());
    }

    juce::String describe (float setting)
    {
        const auto percent = [] (float s) { return juce::String (juce::roundToInt (s * 100.0f)) + "%"; };
        return setting > 0 ? percent (setting) : "Automatic (" + percent (automatic()) + ")";
    }
}
