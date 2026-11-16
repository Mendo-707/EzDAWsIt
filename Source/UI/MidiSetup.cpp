/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Setup > MIDI & Controllers (see MidiSetup.h).
*/

#include "MidiSetup.h"

namespace ez
{
    namespace
    {
        constexpr int linkRowHeight = 28;

        void heading (juce::Label& l, const juce::String& text)
        {
            l.setText (text.toUpperCase(), juce::dontSendNotification);
            l.setFont (Theme::font (Theme::captionFontSize, true));
            l.setColour (juce::Label::textColourId, Theme::textDim);
        }

        void hint (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setFont (Theme::font (Theme::smallFontSize));
            l.setColour (juce::Label::textColourId, Theme::textDim);
            l.setJustificationType (juce::Justification::topLeft);
            l.setMinimumHorizontalScale (1.0f);
        }
    }

    /** One link: "CC 21 (ch 1) -> Bass: Volume", and Remove. */
    struct MidiSetup::LinkRow : public juce::Component
    {
        LinkRow (const MidiLearn::Mapping& m, std::function<void()> onRemove)
        {
            text = (m.isNote ? "Pad / note " : "Knob CC ") + juce::String (m.number) + "  (channel " + juce::String (m.channel) + ")";
            target = m.target;
            remove.setTooltip ("Unlink this knob or pad");
            remove.onClick = std::move (onRemove);
            addAndMakeVisible (remove);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().withTrimmedRight (remove.getWidth() + 8);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.setColour (Theme::textDim);
            g.drawFittedText (text, r.removeFromLeft (juce::jmin (220, r.getWidth() / 2)), juce::Justification::centredLeft, 1);
            g.setColour (Theme::text);
            g.drawFittedText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92  ")) + target, r, juce::Justification::centredLeft, 1);
            g.setColour (Theme::laneSeparator);
            g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        }

        void resized() override   { remove.setBounds (getLocalBounds().removeFromRight (80).reduced (0, 3)); }

        juce::String text, target;
        juce::TextButton remove { "Remove" };
    };

    MidiSetup::MidiSetup (Session& s)
        : session (s), learn (s.getMidiLearn())
    {
        heading (inputsHeading, "Controllers heard");
        hint (inputsList, {});
        heading (clockHeading, "Send MIDI clock (tempo, play and stop) to");
        hint (clockHint, "Tick your drum machine or groovebox (an MC-707, say): it plays in time with the song and starts and stops with it. "
                         "Set it to follow external clock. Ctrl+Shift+Y or Sync in the transport switches it on and off.");
        heading (linksHeading, "Knobs, faders and pads you've linked (saved with the song)");
        hint (linksHint, "To link a knob or fader: close Setup, press Ctrl+Shift+L, touch a control in EzDAWsIt (a fader, a knob), "
                         "then move the knob on your controller. To make a pad or button do something, pick it here and press Learn.");

        for (auto* l : { &inputsHeading, &inputsList, &clockHeading, &clockHint, &linksHeading, &linksHint, &learnStatus })
            addAndMakeVisible (l);

        learnStatus.setFont (Theme::font (Theme::baseFontSize, true));
        learnStatus.setColour (juce::Label::textColourId, Theme::selection);

        actionBox.addItemList (MidiLearn::getActionNames(), 1);
        actionBox.setSelectedItemIndex (0, juce::dontSendNotification);
        actionBox.setTooltip ("What the pad or button should do");
        addAndMakeVisible (actionBox);

        learnActionButton.setTooltip ("Then press the pad or button on your controller");
        learnActionButton.onClick = [this]
        {
            if (learn.isLearning())
                learn.stopLearning();
            else
                learn.startLearningAction (actionBox.getText());

            rebuild();
        };
        addAndMakeVisible (learnActionButton);

        clearButton.setTooltip ("Unlink every knob, fader and pad in this song");
        clearButton.onClick = [this] { learn.clearMappings(); rebuild(); };
        addAndMakeVisible (clearButton);

        linksViewport.setViewedComponent (&linksContent, false);
        linksViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (linksViewport);

        rebuild();
        startTimerHz (10);
    }

    MidiSetup::~MidiSetup()
    {
        if (learn.isLearning())
            learn.stopLearning();   // learning from a page that's gone would surprise later
    }

    void MidiSetup::timerCallback()
    {
        rebuild();
    }

    void MidiSetup::rebuild()
    {
        const auto inputs = learn.getInputNames();
        const auto clocks = learn.getClockOutputs();
        const auto links = learn.getMappings();

        juce::String signature;
        signature << inputs.joinIntoString ("|") << "#" << (int) learn.isLearning() << learn.getLearnStatus() << "#";

        for (const auto& c : clocks)   signature << c.id << (int) c.sending << "|";
        for (const auto& l : links)    signature << l.channel << "." << l.number << (int) l.isNote << l.target << "|";

        if (signature == lastSignature)
            return;

        lastSignature = signature;

        inputsList.setText (inputs.isEmpty() ? juce::String ("No MIDI controllers found: plug one in (USB) and it shows here.")
                                             : inputs.joinIntoString (",  "), juce::dontSendNotification);

        clockButtons.clear();
        clockIDs.clear();

        for (const auto& c : clocks)
        {
            auto* b = clockButtons.add (new juce::ToggleButton (c.name));
            b->setToggleState (c.sending, juce::dontSendNotification);
            b->onClick = [this, id = c.id, b] { learn.setSendingClock (id, b->getToggleState()); };
            clockIDs.add (c.id);
            addAndMakeVisible (b);
        }

        linkRows.clear();

        for (int i = 0; i < links.size(); ++i)
            linksContent.addAndMakeVisible (linkRows.add (new LinkRow (links.getReference (i),
                                                                       [this, ch = links.getReference (i).channel, num = links.getReference (i).number,
                                                                        note = links.getReference (i).isNote]
            {
                learn.removeMapping (ch, num, note);
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MidiSetup> (this)]
                {
                    if (safe != nullptr)
                        safe->rebuild();   // after the click that removed this row has finished
                });
            })));

        learnStatus.setText (learn.getLearnStatus(), juce::dontSendNotification);
        learnActionButton.setButtonText (learn.isLearning() ? "Stop" : "Learn");
        clearButton.setEnabled (! links.isEmpty());
        resized();
        repaint();
    }

    void MidiSetup::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::panel);

        if (linkRows.isEmpty())
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.drawFittedText ("Nothing linked yet.", linksViewport.getBounds().reduced (4), juce::Justification::topLeft, 1);
        }

        if (clockButtons.isEmpty())
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.drawText ("No MIDI outputs found.", clockHeading.getBounds().translated (0, 22), juce::Justification::centredLeft);
        }
    }

    void MidiSetup::resized()
    {
        auto r = getLocalBounds().reduced (4);
        const auto line = [&r] (int h) { auto row = r.removeFromTop (h); r.removeFromTop (4); return row; };

        inputsHeading.setBounds (line (18));
        inputsList.setBounds (line (34));
        r.removeFromTop (6);

        clockHeading.setBounds (line (18));

        // The outputs in two columns
        const auto rows = (clockButtons.size() + 1) / 2;
        auto clockArea = line (juce::jmax (22, rows * 26));

        for (int i = 0; i < clockButtons.size(); ++i)
            clockButtons[i]->setBounds (clockArea.getX() + (i % 2) * (clockArea.getWidth() / 2), clockArea.getY() + (i / 2) * 26,
                                        clockArea.getWidth() / 2 - 8, 24);

        clockHint.setBounds (line (34));
        r.removeFromTop (6);

        linksHeading.setBounds (line (18));
        linksHint.setBounds (line (34));

        auto actionRow = line (26);
        actionBox.setBounds (actionRow.removeFromLeft (200));
        actionRow.removeFromLeft (8);
        learnActionButton.setBounds (actionRow.removeFromLeft (80));
        clearButton.setBounds (actionRow.removeFromRight (110));
        actionRow.removeFromLeft (12);
        learnStatus.setBounds (actionRow);

        linksViewport.setBounds (r);
        const auto width = linksViewport.getWidth() - linksViewport.getScrollBarThickness();
        linksContent.setSize (juce::jmax (0, width), linkRows.size() * linkRowHeight);

        for (int i = 0; i < linkRows.size(); ++i)
            linkRows[i]->setBounds (0, i * linkRowHeight, linksContent.getWidth(), linkRowHeight);
    }
}
