/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Help: the shortcuts list and the guided tour (see HelpViews.h).
*/

#include "HelpViews.h"

namespace ez
{
    //==============================================================================
    namespace
    {
        /** "ctrl + shift + E" as people write it: "Ctrl+Shift+E" */
        juce::String prettyKey (const juce::KeyPress& key)
        {
            auto text = key.getTextDescription();

            for (auto [from, to] : { std::pair<const char*, const char*> { "ctrl + ", "Ctrl+" }, { "shift + ", "Shift+" },
                                     { "alt + ", "Alt+" }, { "option + ", "Alt+" }, { "command + ", "Ctrl+" }, { "cmd + ", "Ctrl+" } })
                text = text.replace (from, to, true);

            return text.replace ("cursor up", "Up").replace ("cursor down", "Down").replace ("cursor left", "Left")
                       .replace ("cursor right", "Right").replace ("spacebar", "Space");
        }

        /** Keys that belong to an editor rather than a command (they work while it has the focus). */
        struct EditorKey { const char* category; const char* name; const char* keys; const char* description; };

        const EditorKey editorKeys[] =
        {
            { "Piano roll", "Draw tool",        "1",                "Click to add notes" },
            { "Piano roll", "Select tool",      "2",                "Drag a box round notes" },
            { "Piano roll", "Erase tool",       "3",                "Click notes to delete them" },
            { "Piano roll", "Quantise",         "Q",                "Snap the selected notes (or all) to the grid" },
            { "Piano roll", "Fit the view",     "F",                "See all the clip's notes" },
            { "Piano roll", "Transpose",        "Up / Down",        "Selected notes a semitone (Shift: an octave)" },
            { "Piano roll", "Move notes",       "Shift+Left/Right", "Selected notes one grid step" },
            { "Drum machine", "Pick a pad",     "Up / Down",        "The pad above or below" },
            { "Drum machine", "Hear the pad",   "Enter",            "Plays the picked pad" },
            { "Drum machine", "Mute / solo pad","M / S",            "The picked pad" },
            { "Drum machine", "Shift the row",  "Shift+Left/Right", "The picked pad's hits one step" },
            { "Drum machine", "Pages",          "1 - 4, Page Up/Down", "Long patterns: which 16 steps you see" },
            { "Drum machine", "Roll",           "Alt+click",        "A step plays 2, 3, 4, 6 or 8 hits" },
            { "Drum machine", "Accent",         "Shift+click",      "A step louder" },
            { "Mixer", "Fader",                 "Up / Down",        "The selected strip 1 dB (Shift: 0.1)" },
            { "Mixer", "Send",                  "[ / ]",            "The selected strip's first send" },
            { "Mixer", "Mute / solo",           "M / S",            "The selected strip" },
            { "Export", "Presets",              "1 - 9",            "In the Export window: pick a preset" },
            { "Tuner and tour", "Tour",         "Left / Right / Esc", "Back, next, close" },
        };
    }

    std::vector<ShortcutsView::Row> ShortcutsView::collect (juce::ApplicationCommandManager& commands, const juce::String& filter)
    {
        std::vector<Row> result;
        const auto words = juce::StringArray::fromTokens (filter.toLowerCase(), " ", {});
        const auto matchesFilter = [&words] (const Row& row)
        {
            const auto haystack = (row.category + " " + row.name + " " + row.keys + " " + row.description).toLowerCase();

            for (const auto& w : words)
                if (! haystack.contains (w))
                    return false;

            return true;
        };

        for (const auto& category : commands.getCommandCategories())
        {
            std::vector<Row> inCategory;

            for (auto id : commands.getCommandsInCategory (category))
            {
                const auto* info = commands.getCommandForID (id);

                if (info == nullptr)
                    continue;

                juce::StringArray keys;

                if (auto* mappings = commands.getKeyMappings())
                    for (const auto& key : mappings->getKeyPressesAssignedToCommand (id))
                        keys.add (prettyKey (key));

                Row row { category, info->shortName, keys.joinIntoString (",  "), info->description };

                if (matchesFilter (row))
                    inCategory.push_back (row);
            }

            if (! inCategory.empty())
            {
                result.push_back ({ category, {}, {}, {}, true });
                result.insert (result.end(), inCategory.begin(), inCategory.end());
            }
        }

        // Then the editors' own keys
        juce::String lastCategory;

        for (const auto& k : editorKeys)
        {
            const Row row { k.category, k.name, k.keys, k.description };

            if (! matchesFilter (row))
                continue;

            if (row.category != lastCategory)
            {
                result.push_back ({ row.category + " (while it has the focus)", {}, {}, {}, true });
                lastCategory = row.category;
            }

            result.push_back (row);
        }

        return result;
    }

    ShortcutsView::ShortcutsView (juce::ApplicationCommandManager& c) : commands (c)
    {
        search.setTextToShowWhenEmpty ("Search: \"record\", \"Ctrl+E\", \"loop\"...", Theme::textDim);
        search.setFont (Theme::font (Theme::baseFontSize));
        search.onTextChange = [this] { refilter(); };
        search.onEscapeKey = [this] { search.clear(); refilter(); };
        addAndMakeVisible (search);

        list.setModel (this);
        list.setRowHeight (26);
        list.setColour (juce::ListBox::backgroundColourId, Theme::panel);
        addAndMakeVisible (list);

        refilter();
        setSize (preferredWidth, preferredHeight);
    }

    void ShortcutsView::refilter()
    {
        rows = collect (commands, search.getText());
        list.updateContent();
        list.repaint();
    }

    void ShortcutsView::paintListBoxItem (int index, juce::Graphics& g, int width, int height, bool)
    {
        if (! juce::isPositiveAndBelow (index, (int) rows.size()))
            return;

        const auto& row = rows[(size_t) index];
        auto r = juce::Rectangle<int> (width, height).reduced (10, 0);

        if (row.isHeading)
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::captionFontSize, true));
            g.drawText (row.category.toUpperCase(), r.withTrimmedTop (8), juce::Justification::centredLeft, true);
            return;
        }

        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::baseFontSize));
        g.drawText (row.name, r.removeFromLeft (190), juce::Justification::centredLeft, true);

        auto keyArea = r.removeFromLeft (170);

        if (row.keys.isNotEmpty())
        {
            const auto w = juce::jmin (keyArea.getWidth(), (int) juce::GlyphArrangement::getStringWidth (Theme::font (Theme::smallFontSize, true), row.keys) + 14);
            const auto chip = keyArea.withWidth (w).withSizeKeepingCentre (w, 20).toFloat();
            g.setColour (Theme::chipFill);
            g.fillRoundedRectangle (chip, 4.0f);
            g.setColour (Theme::chipText);
            g.setFont (Theme::font (Theme::smallFontSize, true));
            g.drawText (row.keys, chip.toNearestInt(), juce::Justification::centred, true);
        }

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::smallFontSize));
        g.drawText (row.description, r.withTrimmedLeft (8), juce::Justification::centredLeft, true);
    }

    juce::String ShortcutsView::getTooltipForRow (int index)
    {
        return juce::isPositiveAndBelow (index, (int) rows.size()) ? rows[(size_t) index].description : juce::String();
    }

    void ShortcutsView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::panel);

        if (rows.empty())
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.drawText ("Nothing matches \"" + search.getText() + "\"", list.getBounds().withHeight (40), juce::Justification::centred);
        }
    }

    void ShortcutsView::resized()
    {
        auto r = getLocalBounds().reduced (12);
        search.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
        list.setBounds (r);
    }

    namespace
    {
        class SimpleWindow : public juce::DocumentWindow
        {
        public:
            SimpleWindow (const juce::String& title, juce::Component* content, std::function<void()> closed)
                : juce::DocumentWindow (title, Theme::panel, juce::DocumentWindow::closeButton), onClosed (std::move (closed))
            {
                setUsingNativeTitleBar (true);
                setContentOwned (content, true);
                setResizable (true, false);
                centreWithSize (getWidth(), getHeight());
            }

            void closeButtonPressed() override
            {
                if (onClosed != nullptr)
                    juce::MessageManager::callAsync ([cb = onClosed] { cb(); });   // the owner deletes us, after this returns
            }

        private:
            std::function<void()> onClosed;
        };
    }

    std::unique_ptr<juce::DocumentWindow> createShortcutsWindow (juce::ApplicationCommandManager& commands, std::function<void()> onClose)
    {
        auto window = std::make_unique<SimpleWindow> ("Keyboard shortcuts", new ShortcutsView (commands), std::move (onClose));
        window->setResizeLimits (480, 360, 1400, 1200);
        return window;
    }

    //==============================================================================
    TourOverlay::TourOverlay (std::vector<Step> s, std::function<void()> finished)
        : steps (std::move (s)), onFinished (std::move (finished))
    {
        setWantsKeyboardFocus (true);
        backButton.onClick = [this] { showStep (step - 1); };
        nextButton.onClick = [this] { if (step + 1 < (int) steps.size()) showStep (step + 1); else finish(); };
        skipButton.onClick = [this] { finish(); };
        backButton.setTooltip ("Back (Left)");
        nextButton.setTooltip ("Next (Right or Enter)");
        skipButton.setTooltip ("End the tour (Esc). Help > Take the tour shows it again.");

        for (auto* b : { &backButton, &nextButton, &skipButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
    }

    void TourOverlay::finish()
    {
        if (onFinished != nullptr)
            juce::MessageManager::callAsync ([cb = onFinished] { cb(); });   // the owner removes us, after this click
    }

    void TourOverlay::showStep (int index)
    {
        step = juce::jlimit (0, juce::jmax (0, (int) steps.size() - 1), index);
        backButton.setEnabled (step > 0);
        nextButton.setButtonText (step + 1 < (int) steps.size() ? "Next" : "Done");
        resized();
        repaint();
        grabKeyboardFocus();
    }

    void TourOverlay::resized()
    {
        if (steps.empty())
            return;

        const auto& s = steps[(size_t) step];
        target = s.area != nullptr ? s.area() : juce::Rectangle<int>();
        target = target.getIntersection (getLocalBounds());

        // The bubble goes beside the part it explains: below it, above it, or in the middle
        const auto w = juce::jmin (400, getWidth() - 40), h = 170;
        auto b = juce::Rectangle<int> (w, h);

        if (target.isEmpty())
            b = b.withCentre (getLocalBounds().getCentre());
        else if (target.getBottom() + h + 16 <= getHeight())
            b = b.withPosition (target.getX() + 12, target.getBottom() + 12);
        else if (target.getY() - h - 16 >= 0)
            b = b.withPosition (target.getX() + 12, target.getY() - h - 12);
        else if (target.getRight() + w + 16 <= getWidth())
            b = b.withPosition (target.getRight() + 12, target.getY() + 12);
        else
            b = b.withCentre (target.getCentre());

        bubble = b.constrainedWithin (getLocalBounds().reduced (10));

        auto buttons = bubble.reduced (14).removeFromBottom (28);
        nextButton.setBounds (buttons.removeFromRight (80));
        buttons.removeFromRight (6);
        backButton.setBounds (buttons.removeFromRight (70));
        skipButton.setBounds (buttons.removeFromLeft (80));
    }

    void TourOverlay::paint (juce::Graphics& g)
    {
        // Dim everything but the part being explained
        juce::Path dim;
        dim.addRectangle (getLocalBounds().toFloat());

        if (! target.isEmpty())
        {
            dim.setUsingNonZeroWinding (false);
            dim.addRoundedRectangle (target.toFloat().expanded (3.0f), 6.0f);
        }

        g.setColour (juce::Colours::black.withAlpha (0.62f));
        g.fillPath (dim);

        if (! target.isEmpty())
        {
            g.setColour (Theme::selection);
            g.drawRoundedRectangle (target.toFloat().expanded (3.0f), 6.0f, 2.0f);
        }

        if (steps.empty())
            return;

        const auto& s = steps[(size_t) step];
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (bubble.toFloat(), 8.0f);
        g.setColour (Theme::selection);
        g.drawRoundedRectangle (bubble.toFloat(), 8.0f, 1.5f);

        auto r = bubble.reduced (14);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::captionFontSize, true));
        g.drawText (juce::String (step + 1) + " OF " + juce::String ((int) steps.size()), r.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::titleFontSize + 2.0f, true));
        g.drawText (s.title, r.removeFromTop (26), juce::Justification::centredLeft, true);
        r.removeFromBottom (36);
        g.setColour (Theme::chipText);
        g.setFont (Theme::font (Theme::baseFontSize));
        g.drawFittedText (s.text, r, juce::Justification::topLeft, juce::jmax (1, r.getHeight() / 16));
    }

    bool TourOverlay::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)                                          { finish(); return true; }
        if (key == juce::KeyPress::leftKey)                                            { showStep (step - 1); return true; }
        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::returnKey)
        {
            if (step + 1 < (int) steps.size())  showStep (step + 1);
            else                                finish();

            return true;
        }

        return true;   // the app underneath doesn't get keys while the tour is up
    }
}
