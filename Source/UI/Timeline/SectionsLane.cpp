/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "SectionsLane.h"
#include "../Theme.h"

namespace ez
{
    namespace
    {
        constexpr int edgeZone = 5, handleWidth = 14;
        constexpr int defaultSectionBars = 8;

        const std::pair<const char*, juce::uint32> sectionColours[] =
        {
            { "Grey", 0xff5a6170 }, { "Blue", 0xff4b6a8a }, { "Amber", 0xff8a6a3a }, { "Purple", 0xff6a4b8a },
            { "Red", 0xff8a3a4b }, { "Green", 0xff3a7a55 }, { "Teal", 0xff2f7a78 }, { "Pink", 0xff8a4b78 },
        };
    }

    const juce::StringArray& SectionsLane::getSectionNames()
    {
        static const juce::StringArray names { "Intro", "Verse", "Pre-hook", "Hook", "Bridge", "Break", "Drop", "Outro" };
        return names;
    }

    SectionsLane::SectionsLane (Session& s, TimelineViewState& v) : session (s), view (v)
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);   // Delete / Esc after clicking in the lane
        nameEditor.setFont (Theme::font (Theme::smallFontSize, true));
        nameEditor.setIndents (4, 2);
        nameEditor.onReturnKey = [this] { commitRename(); };
        nameEditor.onEscapeKey = [this] { renaming = {}; nameEditor.setVisible (false); returnFocus(); };
        nameEditor.onFocusLost = [this] { commitRename(); };
        addChildComponent (nameEditor);
    }

    //==============================================================================
    int SectionsLane::xForBeat (double beat) const
    {
        auto* edit = session.getEdit();
        return edit != nullptr ? view.timeToX (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (beat))) : 0;
    }

    double SectionsLane::beatAtX (int x) const
    {
        auto* edit = session.getEdit();
        return edit != nullptr ? juce::jmax (0.0, edit->tempoSequence.toBeats (view.xToTime (x)).inBeats()) : 0.0;
    }

    double SectionsLane::snap (double beat, bool toBeat) const
    {
        if (toBeat)
            return juce::jmax (0.0, std::round (beat));

        const auto [bar, inBar] = session.getBarAt (beat);
        const auto barLength = session.getBarStartBeat (bar + 1) - session.getBarStartBeat (bar);
        return session.getBarStartBeat (inBar > barLength * 0.5 ? bar + 1 : bar);
    }

    juce::Rectangle<int> SectionsLane::blockFor (const Session::Section& s) const
    {
        const auto x1 = xForBeat (s.startBeat), x2 = xForBeat (s.endBeat);
        return { x1 + 1, 2, juce::jmax (4, x2 - x1 - 2), getHeight() - 4 };
    }

    juce::Rectangle<int> SectionsLane::repeatHandleFor (const Session::Section& s) const
    {
        const auto block = blockFor (s);
        return block.getWidth() >= 3 * handleWidth ? block.withLeft (block.getRight() - handleWidth - edgeZone).withWidth (handleWidth) : juce::Rectangle<int>();
    }

    std::optional<Session::Section> SectionsLane::sectionAt (int x) const
    {
        for (auto& s : session.getSections())
            if (blockFor (s).expanded (2, 0).contains (x, getHeight() / 2))
                return s;

        return std::nullopt;
    }

    SectionsLane::Mode SectionsLane::modeAt (const Session::Section& s, int x) const
    {
        const auto block = blockFor (s);

        if (repeatHandleFor (s).contains (x, getHeight() / 2))        return Mode::repeat;
        if (x <= block.getX() + edgeZone && block.getWidth() > 16)    return Mode::resizeStart;
        if (x >= block.getRight() - edgeZone && block.getWidth() > 16) return Mode::resizeEnd;
        return Mode::move;
    }

    //==============================================================================
    void SectionsLane::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::bar);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        if (session.getEdit() == nullptr)
            return;

        const auto sections = session.getSections();

        if (sections.empty())
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::captionFontSize));
            g.drawText ("Sections: double-click to add Intro, Verse, Hook...", getLocalBounds().withTrimmedLeft (8), juce::Justification::centredLeft, false);
            return;
        }

        const auto font = Theme::font (Theme::captionFontSize + 0.5f, true);

        for (auto& s : sections)
        {
            auto shown = s;

            if (dragged.has_value() && dragged->id == s.id && mode != Mode::repeat && mode != Mode::none)
            {
                shown.startBeat = previewStart;
                shown.endBeat = previewEnd;
            }

            const auto block = blockFor (shown);

            if (block.getRight() < 0 || block.getX() > getWidth())
                continue;

            const bool isSelected = s.id == selected;
            g.setColour (s.colour.withMultipliedBrightness (s.id == hovered ? 1.15f : 1.0f));
            g.fillRoundedRectangle (block.toFloat(), 3.0f);

            if (isSelected)
            {
                g.setColour (Theme::selection);
                g.drawRoundedRectangle (block.toFloat().reduced (0.5f), 3.0f, 1.5f);
            }

            auto text = block.reduced (6, 0);

            // Repeat handle, on hover or when selected
            if ((s.id == hovered || isSelected) && ! repeatHandleFor (shown).isEmpty())
            {
                const auto handle = repeatHandleFor (shown).toFloat().reduced (1.0f, 2.0f);
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.drawRoundedRectangle (handle.withTrimmedLeft (3.0f).withTrimmedBottom (3.0f), 2.0f, 1.2f);
                g.drawRoundedRectangle (handle.withTrimmedRight (3.0f).withTrimmedTop (3.0f), 2.0f, 1.2f);
                text.setRight (repeatHandleFor (shown).getX() - 2);
            }

            g.setColour (juce::Colours::white);
            g.setFont (font);
            g.drawText (s.name, text, juce::Justification::centredLeft, true);
        }

        // Repeat preview: ghost copies after the section
        if (dragged.has_value() && mode == Mode::repeat && repeatCount > 0)
        {
            const auto length = dragged->endBeat - dragged->startBeat;

            for (int k = 1; k <= repeatCount; ++k)
            {
                auto ghost = *dragged;
                ghost.startBeat += k * length;
                ghost.endBeat += k * length;
                const auto block = blockFor (ghost);
                g.setColour (dragged->colour.withAlpha (0.45f));
                g.fillRoundedRectangle (block.toFloat(), 3.0f);
                g.setColour (Theme::selection);
                g.drawRoundedRectangle (block.toFloat().reduced (0.5f), 3.0f, 1.0f);
                g.setColour (juce::Colours::white);
                g.setFont (font);
                g.drawText (dragged->name + (k == repeatCount ? "  x" + juce::String (repeatCount + 1) : juce::String()), block.reduced (6, 0),
                            juce::Justification::centredLeft, true);
            }
        }
    }

    //==============================================================================
    void SectionsLane::report (bool ok, const juce::String& title)
    {
        if (! ok && onProblem)
            onProblem (title, session.getLastError());

        repaint();
    }

    void SectionsLane::mouseMove (const juce::MouseEvent& e)
    {
        const auto s = sectionAt (e.x);
        const auto id = s.has_value() ? s->id : te::EditItemID();

        if (id != hovered)
        {
            hovered = id;
            repaint();
        }

        if (! s.has_value())
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
            setTooltip ("Sections: double-click to add one, right-click for a list of names");
            return;
        }

        const auto m = modeAt (*s, e.x);
        setMouseCursor (m == Mode::repeat ? juce::MouseCursor::CopyingCursor
                      : m == Mode::move   ? juce::MouseCursor::DraggingHandCursor
                                          : juce::MouseCursor::LeftRightResizeCursor);
        setTooltip (m == Mode::repeat ? "Drag right to repeat " + s->name + " (one copy per length; the rest of the song moves along)"
                                      : s->name + ": click to go there, drag to move, drag an edge to resize, double-click to rename, right-click for more");
    }

    void SectionsLane::mouseExit (const juce::MouseEvent&)
    {
        hovered = {};
        repaint();
    }

    void SectionsLane::mouseDown (const juce::MouseEvent& e)
    {
        mode = Mode::none;
        dragged.reset();
        repeatCount = 0;
        const auto s = sectionAt (e.x);

        if (e.mods.isPopupMenu())
        {
            if (s.has_value())
            {
                selected = s->id;
                showSectionMenu (*s);
            }
            else
            {
                showEmptyMenu (beatAtX (e.x));
            }

            repaint();
            return;
        }

        if (! s.has_value())
        {
            selected = {};
            repaint();
            return;
        }

        selected = s->id;
        dragged = s;
        mode = modeAt (*s, e.x);
        previewStart = s->startBeat;
        previewEnd = s->endBeat;
        repaint();
    }

    void SectionsLane::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragged.has_value() || mode == Mode::none || e.getDistanceFromDragStart() < 3)
            return;

        if (juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::escapeKey))   // Esc cancels, even without the keyboard
        {
            dragged.reset();
            mode = Mode::none;
            repeatCount = 0;
            repaint();
            return;
        }

        const bool toBeat = e.mods.isAltDown();
        const auto delta = beatAtX (e.x) - beatAtX (e.getMouseDownX());
        const auto length = dragged->endBeat - dragged->startBeat;

        if (mode == Mode::move)
        {
            previewStart = snap (juce::jmax (0.0, dragged->startBeat + delta), toBeat);
            previewEnd = previewStart + length;
        }
        else if (mode == Mode::resizeStart)
        {
            previewStart = juce::jmin (snap (dragged->startBeat + delta, toBeat), dragged->endBeat - Session::minSectionBeats);
            previewEnd = dragged->endBeat;
        }
        else if (mode == Mode::resizeEnd)
        {
            previewStart = dragged->startBeat;
            previewEnd = juce::jmax (snap (dragged->endBeat + delta, toBeat), dragged->startBeat + Session::minSectionBeats);
        }
        else if (mode == Mode::repeat)
        {
            repeatCount = juce::jlimit (0, 16, (int) std::floor (juce::jmax (0.0, delta) / length + 0.5));
        }

        repaint();
    }

    void SectionsLane::mouseUp (const juce::MouseEvent& e)
    {
        const auto finished = std::exchange (mode, Mode::none);
        const auto section = std::exchange (dragged, std::nullopt);
        const auto count = std::exchange (repeatCount, 0);

        if (! section.has_value() || finished == Mode::none || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::escapeKey))
        {
            repaint();
            return;
        }

        if (! e.mouseWasDraggedSinceMouseDown())
        {
            // A click: go to the section
            if (auto* edit = session.getEdit())
                session.setPosition (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (section->startBeat)));

            grabKeyboardFocus();
            repaint();
            return;
        }

        if (finished == Mode::repeat)
        {
            if (count > 0)
                report (session.repeatSection (section->id, count), "Could not repeat the section");
        }
        else if (finished == Mode::move)
        {
            if (std::abs (previewStart - section->startBeat) > 1.0e-6)
                report (session.moveSection (section->id, previewStart), "Could not move the section");
        }
        else if (std::abs (previewStart - section->startBeat) > 1.0e-6 || std::abs (previewEnd - section->endBeat) > 1.0e-6)
        {
            report (session.resizeSection (section->id, previewStart, previewEnd), "Could not resize the section");
        }

        repaint();
    }

    void SectionsLane::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;

        mode = Mode::none;
        dragged.reset();

        if (const auto s = sectionAt (e.x))
            startRenaming (*s);
        else
            showEmptyMenu (beatAtX (e.x));
    }

    void SectionsLane::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (onWheel)
            onWheel (e, wheel);
    }

    bool SectionsLane::keyPressed (const juce::KeyPress& key)
    {
        if (! lastClick.hasFocus())
            return false;   // the keyboard stayed here, but you have since clicked elsewhere

        if (key == juce::KeyPress::escapeKey && dragged.has_value())
        {
            dragged.reset();
            mode = Mode::none;
            repeatCount = 0;
            repaint();
            return true;
        }

        if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selected.isValid())
        {
            report (session.deleteSection (std::exchange (selected, {})), "Could not delete the section");
            return true;
        }

        return false;   // everything else (Space, shortcuts) goes on to the app
    }

    //==============================================================================
    void SectionsLane::addSectionAt (double beat, const juce::String& name)
    {
        const auto bar = session.getBarAt (beat).first;
        const auto start = session.getBarStartBeat (bar);
        selected = {};   // only the new section, if it was made
        report (session.addSection (start, session.getBarStartBeat (bar + defaultSectionBars), name), "Could not add a section");

        for (auto& s : session.getSections())
            if (std::abs (s.startBeat - start) < 1.0e-6)
                selected = s.id;
    }

    void SectionsLane::showEmptyMenu (double beat)
    {
        const auto bar = session.getBarAt (beat).first;
        juce::PopupMenu menu;
        menu.addSectionHeader ("Add a section at bar " + juce::String (bar + 1));

        for (int i = 0; i < getSectionNames().size(); ++i)
            menu.addItem (1 + i, getSectionNames()[i]);

        menu.addItem (100, "Other (type a name)...");

        juce::Component::SafePointer<SectionsLane> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, beat] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            if (result == 100)
            {
                safe->addSectionAt (beat, "Section");

                if (safe->selected.isValid())
                    if (const auto s = safe->session.getSection (safe->selected))
                        safe->startRenaming (*s);
            }
            else
            {
                safe->addSectionAt (beat, getSectionNames()[result - 1]);
            }
        });
    }

    void SectionsLane::showSectionMenu (const Session::Section& s)
    {
        juce::PopupMenu menu, colours, repeats;
        menu.addSectionHeader (s.name + " (bar " + juce::String (session.getBarAt (s.startBeat).first + 1) + ")");
        menu.addItem (1, "Rename...");

        for (int i = 0; i < (int) std::size (sectionColours); ++i)
            colours.addItem (100 + i, sectionColours[i].first, true, s.colour.getARGB() == sectionColours[i].second);

        menu.addSubMenu ("Colour", colours);
        menu.addSeparator();
        menu.addItem (2, "Loop this section");
        menu.addItem (3, "Select its clips");
        menu.addItem (4, "Move the playhead here");
        menu.addSeparator();

        for (int times : { 1, 2, 3, 4, 8 })
            repeats.addItem (200 + times, times == 1 ? juce::String ("Once") : juce::String (times) + " times");

        menu.addSubMenu ("Repeat (the rest moves along)", repeats);
        menu.addSeparator();
        menu.addItem (5, "Delete the section (keep the music)", true);
        menu.addItem (6, "Delete the section and its music (close the gap)");

        juce::Component::SafePointer<SectionsLane> safe (this);
        const auto id = s.id;
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, id] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto& sess = safe->session;
            const auto current = sess.getSection (id);

            if (! current.has_value())
                return;

            auto* edit = sess.getEdit();
            const auto start = edit != nullptr ? edit->tempoSequence.toTime (te::BeatPosition::fromBeats (current->startBeat)) : te::TimePosition();
            const auto end = edit != nullptr ? edit->tempoSequence.toTime (te::BeatPosition::fromBeats (current->endBeat)) : te::TimePosition();

            if (result >= 200)          safe->report (sess.repeatSection (id, result - 200), "Could not repeat the section");
            else if (result >= 100)     safe->report (sess.setSectionColour (id, juce::Colour (sectionColours[result - 100].second)), "Could not change the colour");
            else if (result == 1)       safe->startRenaming (*current);
            else if (result == 2)
            {
                sess.setLoopRange (start, end);
                safe->report (sess.setLooping (true), "Could not loop the section");
            }
            else if (result == 3)       safe->report (sess.selectSectionClips (id), "Could not select the clips");
            else if (result == 4)       sess.setPosition (start);
            else if (result == 5)       safe->report (sess.deleteSection (id), "Could not delete the section");
            else if (result == 6)       safe->report (sess.deleteSectionAndMusic (id), "Could not delete the section");

            safe->repaint();
        });
    }

    //==============================================================================
    void SectionsLane::startRenaming (const Session::Section& s)
    {
        renaming = s.id;
        selected = s.id;
        const auto block = blockFor (s);
        nameEditor.setBounds (juce::jlimit (0, juce::jmax (0, getWidth() - 140), block.getX()), 1, juce::jlimit (90, 220, block.getWidth()), getHeight() - 2);
        nameEditor.setText (s.name, false);
        nameEditor.setVisible (true);
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
    }

    void SectionsLane::commitRename()
    {
        if (! nameEditor.isVisible())
            return;

        nameEditor.setVisible (false);   // first: committing can run again from onFocusLost
        const auto id = std::exchange (renaming, {});
        const auto name = nameEditor.getText().trim();

        if (id.isValid() && name.isNotEmpty())
            if (const auto s = session.getSection (id); s.has_value() && s->name != name)
                report (session.renameSection (id, name), "Could not rename the section");

        returnFocus();
    }

    void SectionsLane::returnFocus()
    {
        // Give the keyboard back so Space and the shortcuts work again
        for (auto* p = getParentComponent(); p != nullptr; p = p->getParentComponent())
            if (p->getWantsKeyboardFocus())
            {
                p->grabKeyboardFocus();
                return;
            }
    }
}
