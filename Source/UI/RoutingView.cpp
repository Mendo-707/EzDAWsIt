/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "RoutingView.h"
#include "Theme.h"

namespace ez
{
    namespace
    {
        void styleHeading (juce::Label& label, const juce::String& text)
        {
            label.setText (text, juce::dontSendNotification);
            label.setFont (juce::Font (juce::FontOptions (Theme::titleFontSize, juce::Font::bold)));
        }

        /** Fills a combo with choices (item IDs start at 1) and selects `selectedID`. */
        template <typename Choices>
        void fillCombo (juce::ComboBox& box, const Choices& choices, const juce::String& selectedID)
        {
            box.clear (juce::dontSendNotification);

            for (int i = 0; i < choices.size(); ++i)
            {
                box.addItem (choices.getReference (i).name, i + 1);

                if (choices.getReference (i).id == selectedID)
                    box.setSelectedId (i + 1, juce::dontSendNotification);
            }
        }
    }

    //==============================================================================
    struct RoutingView::OutputRow : public juce::Component
    {
        juce::Label name;
        juce::TextButton test { "Test" };

        OutputRow()
        {
            addAndMakeVisible (name);
            addAndMakeVisible (test);
            test.setTooltip ("Play a short, quiet beep on this output only");
        }

        void resized() override
        {
            auto r = getLocalBounds();
            test.setBounds (r.removeFromRight (Theme::transportButtonWidth).reduced (2));
            name.setBounds (r);
        }
    };

    struct RoutingView::TrackRow : public juce::Component
    {
        juce::Label name;
        juce::ComboBox input, output;

        TrackRow()
        {
            addAndMakeVisible (name);
            addAndMakeVisible (input);
            addAndMakeVisible (output);
            input.setTooltip ("What this track records / listens to");
            output.setTooltip ("Where this track's sound goes. 'Main mix' goes through the master channel.");
        }

        void resized() override
        {
            auto r = getLocalBounds();
            const auto third = r.getWidth() / 3;
            name.setBounds (r.removeFromLeft (third));
            input.setBounds (r.removeFromLeft (third).reduced (2));
            output.setBounds (r.reduced (2));
        }
    };

    //==============================================================================
    RoutingView::RoutingView (AudioEngine& e, Session& s)
        : audioEngine (e), session (s)
    {
        styleHeading (mainHeading, "Where you hear the song");
        styleHeading (outputsHeading, "Your outputs (press Test to find your headphones / speakers)");
        styleHeading (tracksHeading, "Tracks: input and output");

        mainLabel.setText ("Main output:", juce::dontSendNotification);
        hint.setText ("Only outputs and inputs enabled in Audio Setup are listed. Choosing an output in "
                      "'Main output' changes where the whole song plays.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, Theme::textDim);

        mainOutputBox.onChange = [this]
        {
            const auto index = mainOutputBox.getSelectedId() - 1;

            if (! juce::isPositiveAndBelow (index, outputs.size()))
                return;

            if (session.setMainOutput (outputs.getReference (index).id))
            {
                showStatus ("The song now plays through " + outputs.getReference (index).name, false);
            }
            else
            {
                showStatus (session.getLastError(), true);
                lastSignature = {};   // re-select the real main output on the next refresh
            }
        };

        testMainButton.onClick = [this]
        {
            if (! audioEngine.playTestTone (audioEngine.getMainOutputID()))
                showStatus ("No main output to test: check Audio Setup", true);
        };

        for (auto* c : std::initializer_list<juce::Component*> { &mainHeading, &mainLabel, &mainOutputBox, &testMainButton,
                                                                 &outputsHeading, &tracksHeading, &hint, &status })
            content.addAndMakeVisible (c);

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        session.addListener (this);
        rebuild();
        startTimer (1000);
        setSize (Theme::largeDialogWidth, Theme::dialogHeight);
    }

    RoutingView::~RoutingView()
    {
        session.removeListener (this);
    }

    void RoutingView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::background);
    }

    void RoutingView::resized()
    {
        viewport.setBounds (getLocalBounds());
        layoutContent();
    }

    //==============================================================================
    void RoutingView::rebuild()
    {
        outputs = audioEngine.getOutputs();

        // Main output
        juce::Array<Session::RoutingChoice> mainChoices;
        for (auto& o : outputs)
            mainChoices.add ({ o.id, o.name });

        fillCombo (mainOutputBox, mainChoices, audioEngine.getMainOutputID());
        mainOutputBox.setTextWhenNoChoicesAvailable ("No outputs: open Audio Setup");

        // One row per output, each with its own test beep
        outputRows.clear();

        for (auto& o : outputs)
        {
            auto* row = outputRows.add (new OutputRow());
            row->name.setText (o.name + (o.id == audioEngine.getMainOutputID() ? "   (main output)" : ""),
                               juce::dontSendNotification);
            row->test.onClick = [this, id = o.id] { audioEngine.playTestTone (id); };
            content.addAndMakeVisible (row);
        }

        // One row per track: input + output
        trackRows.clear();
        const auto inputChoices = session.getTrackInputChoices();
        for (int t = 0; t < session.getNumAudioTracks(); ++t)
        {
            const auto outputChoices = session.getTrackOutputChoices (t);   // per track: the buses it can play into
            auto* row = trackRows.add (new TrackRow());
            row->name.setText (juce::String (t + 1) + ". " + session.getTrackName (t), juce::dontSendNotification);
            fillCombo (row->input, inputChoices, session.getTrackInput (t));
            fillCombo (row->output, outputChoices, session.getTrackOutput (t));

            row->input.onChange = [this, t, inputChoices, box = &row->input]
            {
                const auto index = box->getSelectedId() - 1;

                if (juce::isPositiveAndBelow (index, inputChoices.size()) && ! session.setTrackInput (t, inputChoices[index].id))
                    showStatus (session.getLastError(), true);
            };

            row->output.onChange = [this, t, outputChoices, box = &row->output]
            {
                const auto index = box->getSelectedId() - 1;

                if (juce::isPositiveAndBelow (index, outputChoices.size()) && ! session.setTrackOutput (t, outputChoices[index].id))
                    showStatus (session.getLastError(), true);
            };

            content.addAndMakeVisible (row);
        }

        lastSignature = makeSignature();
        layoutContent();
    }

    juce::String RoutingView::makeSignature()
    {
        juce::String s = audioEngine.getMainOutputID();

        for (auto& o : audioEngine.getOutputs())
            s << "|" << o.id << o.name;

        for (auto& c : session.getTrackInputChoices())
            s << "|" << c.id;

        for (int t = 0; t < session.getNumAudioTracks(); ++t)
        {
            s << "|" << session.getTrackName (t) << session.getTrackInput (t) << session.getTrackOutput (t);

            for (auto& c : session.getTrackOutputChoices (t))
                s << "|o" << c.id << c.name;   // a bus renamed, added or removed
        }

        return s;
    }

    void RoutingView::layoutContent()
    {
        const auto width = juce::jmax (Theme::dialogWidth, viewport.getMaximumVisibleWidth());
        const auto rowHeight = Theme::toolbarRowHeight;
        const auto pad = Theme::gap * 2;
        auto y = pad;

        const auto place = [&] (juce::Component& c, int height)
        {
            c.setBounds (pad, y, width - 2 * pad, height);
            y += height;
        };

        place (mainHeading, rowHeight);
        {
            auto row = juce::Rectangle<int> (pad, y, width - 2 * pad, rowHeight);
            mainLabel.setBounds (row.removeFromLeft (Theme::buttonWidth));
            testMainButton.setBounds (row.removeFromRight (Theme::transportButtonWidth).reduced (2));
            mainOutputBox.setBounds (row.reduced (2));
            y += rowHeight;
        }

        y += Theme::gap;
        place (outputsHeading, rowHeight);

        for (auto* row : outputRows)
            place (*row, rowHeight);

        y += Theme::gap;
        place (tracksHeading, rowHeight);

        for (auto* row : trackRows)
            place (*row, rowHeight);

        y += Theme::gap;
        place (status, rowHeight);
        place (hint, rowHeight * 2);

        content.setSize (width, y + pad);
    }

    void RoutingView::showStatus (const juce::String& message, bool isError)
    {
        status.setText (message, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, isError ? Theme::record : Theme::displayText);
    }

    void RoutingView::timerCallback()
    {
        if (makeSignature() != lastSignature)
            rebuild();
    }
}
