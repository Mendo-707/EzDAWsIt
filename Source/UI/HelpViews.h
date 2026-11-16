/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Help (the App bar's ? button, F1):
      ShortcutsView  every command with its keys, searchable, grouped (F1)
      TourOverlay    a guided tour: the screen dims except the part being explained, with Back / Next
                     (shown once on first start; Help > Take the tour any time)
*/

#pragma once

#include "Widgets.h"

namespace ez
{
    //==============================================================================
    class ShortcutsView : public juce::Component,
                          private juce::ListBoxModel
    {
    public:
        explicit ShortcutsView (juce::ApplicationCommandManager&);

        void resized() override;
        void paint (juce::Graphics&) override;

        static constexpr int preferredWidth = 620, preferredHeight = 640;

        /** Every command as "category | name | keys | description" rows (search filters them). */
        struct Row { juce::String category, name, keys, description; bool isHeading = false; };
        static std::vector<Row> collect (juce::ApplicationCommandManager&, const juce::String& filter);

    private:
        juce::ApplicationCommandManager& commands;
        juce::TextEditor search;
        juce::ListBox list;
        std::vector<Row> rows;

        void refilter();
        int getNumRows() override                                  { return (int) rows.size(); }
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
        juce::String getTooltipForRow (int) override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShortcutsView)
    };

    std::unique_ptr<juce::DocumentWindow> createShortcutsWindow (juce::ApplicationCommandManager&, std::function<void()> onClose);

    //==============================================================================
    class TourOverlay : public juce::Component
    {
    public:
        struct Step
        {
            juce::String title, text;
            std::function<juce::Rectangle<int>()> area;   // in the overlay's parent; empty: the middle of the screen
        };

        TourOverlay (std::vector<Step>, std::function<void()> onFinished);

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void mouseDown (const juce::MouseEvent&) override   {}   // the app underneath waits

        int getStep() const noexcept    { return step; }
        void showStep (int);

    private:
        std::vector<Step> steps;
        std::function<void()> onFinished;
        int step = 0;
        juce::TextButton backButton { "Back" }, nextButton { "Next" }, skipButton { "Close" };
        juce::Rectangle<int> target, bubble;

        void finish();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TourOverlay)
    };
}
