/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The help guide (F1, the App bar's ? button, the browser's Learn category):

      Tutorials        short lessons that do something real ("Make your first beat"). Each step ticks itself
                       when you've done it in the song, and a "Show me" button runs the command it needs.
      Guides           how things work: recording, mixing, mastering, exporting, plug-ins...
      Troubleshooting  no sound, latency, crackles, a plug-in that won't load...

    Searchable; the window stays open beside the song so a tutorial can be followed while you work.
*/

#pragma once

#include "../Engine/Session.h"
#include "Widgets.h"

namespace ez
{
    namespace HelpGuide
    {
        struct Step
        {
            juce::String text;
            std::function<bool (Session&)> isDone;   // nullptr: a step to read, ticked by hand
            juce::CommandID command = 0;             // "Show me": runs this (0: none)
            juce::String commandLabel = {};
        };

        struct Topic
        {
            juce::String id, section, title, summary;
            juce::StringArray body;                  // paragraphs; "- " starts a bullet, "# " a heading
            std::vector<Step> steps;                 // tutorials only
            juce::StringArray seeAlso = {};          // topic ids
        };

        /** Every topic: tutorials, then guides, then troubleshooting. */
        const std::vector<Topic>& topics();
        const Topic* find (const juce::String& id);
        juce::StringArray sections();

        /** Every word of the query in the title, summary or text. */
        bool matches (const Topic&, const juce::String& query);
    }

    //==============================================================================
    class HelpGuideView : public juce::Component,
                          private juce::ListBoxModel,
                          private juce::Timer
    {
    public:
        HelpGuideView (Session&, juce::ApplicationCommandManager&);
        ~HelpGuideView() override;

        void showTopic (const juce::String& id);
        juce::String getShownTopic() const      { return shown; }

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int preferredWidth = 860, preferredHeight = 620;

    private:
        class Article;

        Session& session;
        juce::ApplicationCommandManager& commands;
        juce::TextEditor search;
        juce::ListBox list;
        std::unique_ptr<Article> article;
        juce::Viewport articleView;
        struct Row { bool isHeading; juce::String text, id; };
        std::vector<Row> rows;
        juce::String shown;

        void refilter();
        int getNumRows() override                                   { return (int) rows.size(); }
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
        void listBoxItemClicked (int, const juce::MouseEvent&) override;
        void timerCallback() override;   // ticks tutorial steps as they're done

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpGuideView)
    };

    std::unique_ptr<juce::DocumentWindow> createHelpGuideWindow (Session&, juce::ApplicationCommandManager&,
                                                                 const juce::String& topicId, std::function<void()> onClose);
}
