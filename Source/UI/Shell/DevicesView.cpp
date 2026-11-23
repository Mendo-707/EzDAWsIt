/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "DevicesView.h"
#include "../TrackMenus.h"
#include "VocalChainCard.h"
#include "../../Engine/VocalChain.h"
#include "../PluginDisplays.h"
#include "BrowserPanel.h"

namespace ez
{
    namespace
    {
        constexpr int inspectorWidth = 200;
        constexpr int cardGap = 22;                            // the gap holds the flow arrow
        constexpr int addCardWidth = 64, faderCardWidth = 112;
        constexpr int headerHeight = 38, footerHeight = 24;    // the same on every card, so titles and pictures line up

        /** Wider cards in a taller panel, so the pictures keep their shape. */
        int cardWidthFor (int height)   { return juce::jlimit (164, 240, juce::roundToInt (height * 0.82f)); }
    }

    //==============================================================================
    /** One device in the chain. Instrument cards have no bypass, move or remove. */
    class DevicesView::DeviceCard : public juce::Component,
                                    public juce::SettableTooltipClient
    {
    public:
        enum class Kind { instrument, effect, add, fader };

        DeviceCard (Kind k, Session::DeviceInfo i) : kind (k), info (std::move (i))
        {
            if (kind == Kind::effect)
            {
                bypassButton.setClickingTogglesState (false);
                bypassButton.setToggleState (info.isEnabled, juce::dontSendNotification);
                bypassButton.setTooltip (info.isEnabled ? "On: click to bypass (the sound passes through untouched)"
                                                        : "Bypassed: click to switch back on");
                leftButton.setTooltip ("Move earlier in the chain");
                rightButton.setTooltip ("Move later in the chain");
                removeButton.setTooltip ("Remove this effect (Ctrl+Z brings it back)");

                for (auto* b : { &bypassButton, &leftButton, &rightButton, &removeButton })
                    addAndMakeVisible (b);
            }

            if (kind == Kind::effect || kind == Kind::instrument)
            {
                openButton.setTooltip ("Show its controls (or double-click the card)");
                addAndMakeVisible (openButton);
            }

            if (kind == Kind::add)
            {
                // The whole card is the button (drawn by the card: a slim dashed slot, not a filled button)
                setTooltip ("Add an effect to the end of the chain (or drag one here from the browser)");
                setMouseCursor (juce::MouseCursor::PointingHandCursor);
            }
        }

        const Kind kind;
        const Session::DeviceInfo info;

        IconButton bypassButton { "Bypass", Icon::power, {}, IconButton::Style::ghost };
        IconButton openButton   { "Open", Icon::none, "Open" };
        IconButton removeButton { "Remove", Icon::close, {}, IconButton::Style::ghost };
        IconButton leftButton   { "Move earlier", Icon::chevronLeft, {}, IconButton::Style::ghost };
        IconButton rightButton  { "Move later", Icon::chevronRight, {}, IconButton::Style::ghost };
        std::function<void()> onAdd;   // the Add card: a click anywhere on it

        /** A drag from the browser is over the chain: the Add card shows where it lands. */
        void setDropHighlight (bool on)
        {
            if (std::exchange (dropHighlight, on) != on)
                repaint();
        }

        /** A live picture of the effect (its curve), in place of the icon. */
        void setPreview (std::unique_ptr<juce::Component> p)
        {
            preview = std::move (p);

            if (preview != nullptr)
            {
                preview->setInterceptsMouseClicks (false, false);   // clicks reach the card: double-click opens, right-click menus
                addAndMakeVisible (*preview);
            }
        }

        /** The fader card shows the track's volume (dB) and pan (-1..1). */
        std::function<std::pair<float, float>()> faderValues;

        juce::Rectangle<int> pictureArea() const
        {
            auto content = getLocalBounds().reduced (10, 8);
            content.removeFromTop (headerHeight);
            content.removeFromBottom (footerHeight + 8);
            return content;
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);

            if (kind == Kind::add)
            {
                const bool lit = dropHighlight || isMouseOver();
                g.setColour (lit ? Theme::activeFill : Theme::panel.withAlpha (0.35f));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (lit ? Theme::accent : Theme::border);
                juce::Path outline, dashed;
                outline.addRoundedRectangle (r, 6.0f);
                const float dashes[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
                g.fillPath (dashed);

                const auto centre = getLocalBounds().getCentre().toFloat();
                Icons::draw (g, Icon::plus, juce::Rectangle<float> (18.0f, 18.0f).withCentre (centre), lit ? Theme::accent : Theme::text);
                return;
            }

            g.setColour (Theme::panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (Theme::border);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);

            // Header: the bypass button at the left of effects, remove at the right; title over subtitle
            auto header = getLocalBounds().reduced (10, 8).removeFromTop (headerHeight);

            if (kind == Kind::effect)
            {
                header.removeFromLeft (26);
                header.removeFromRight (24);
            }

            const bool dimmed = kind == Kind::effect && ! info.isEnabled;
            g.setColour (dimmed ? Theme::textDim : Theme::text);
            g.setFont (Theme::font (Theme::baseFontSize, true));
            g.drawText (kind == Kind::fader ? juce::String ("Volume & pan") : info.name, header.removeFromTop (20), juce::Justification::centredLeft, true);

            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize - 0.5f));
            const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
            const auto delay = info.latencyMs >= 1.0 ? dot + "adds " + juce::String (juce::roundToInt (info.latencyMs)) + " ms" : juce::String();   // what it adds to live playing
            const auto subtitle = kind == Kind::instrument  ? "Instrument" + dot + info.maker + delay
                                : kind == Kind::fader       ? juce::String ("Track fader")
                                : info.skippedForLatency    ? "Skipped for low latency" + delay
                                : dimmed                    ? "Bypassed" + dot + info.maker
                                                            : info.maker + delay;
            g.drawText (subtitle, header.removeFromTop (16), juce::Justification::centredLeft, true);

            // A small picture of what it is: its live curve where it has one, else an icon
            const auto picture = pictureArea();

            if (kind == Kind::fader && faderValues && picture.getHeight() > 30)
            {
                drawFader (g, picture);
            }
            else if (preview == nullptr && picture.getHeight() > 16)
            {
                g.setColour (Theme::display);
                g.fillRoundedRectangle (picture.toFloat(), (float) Theme::cornerRadius);
                const auto icon = kind == Kind::instrument ? Icon::piano : info.isBuiltIn ? Icon::sliders : Icon::plug;
                const auto size = juce::jlimit (22.0f, 40.0f, picture.getHeight() * 0.3f);
                Icons::draw (g, icon, picture.toFloat().withSizeKeepingCentre (size, size), dimmed ? Theme::border : Theme::accent);
            }
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (kind != Kind::add)
                return;

            // "Add effect" under the plus, in two short lines so the card can stay slim
            const auto centre = getLocalBounds().getCentre();
            g.setColour (dropHighlight ? Theme::accent : Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize - 0.5f));
            g.drawFittedText (dropHighlight ? "Drop to add" : "Add effect",
                              juce::Rectangle<int> (0, centre.y + 16, getWidth(), 30).reduced (4, 0), juce::Justification::centredTop, 2);
        }

        void resized() override
        {
            if (preview != nullptr)
                preview->setBounds (pictureArea());

            if (kind == Kind::add)
                return;

            auto r = getLocalBounds().reduced (8);

            if (kind == Kind::effect)
            {
                bypassButton.setBounds (r.getX(), r.getY() - 2, 24, 24);   // centred on the title line
                removeButton.setBounds (r.getRight() - 24, r.getY() - 2, 24, 24);
            }

            auto footer = r.removeFromBottom (footerHeight);

            if (kind == Kind::effect)
            {
                leftButton.setBounds (footer.removeFromLeft (24));
                rightButton.setBounds (footer.removeFromLeft (24));
                footer.removeFromLeft (8);
            }

            if (openButton.isVisible())
                openButton.setBounds (kind == Kind::effect ? footer.removeFromRight (juce::jmin (footer.getWidth(), 72)) : footer);
        }

        /** Right-click: the same actions as the buttons (each button already knows how to act safely). */
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu() || kind == Kind::fader)
                return;

            if (kind == Kind::add)
                return;   // mouseUp adds, whichever button

            juce::PopupMenu menu;
            menu.addItem (1, "Open " + info.name);

            if (kind == Kind::effect)
            {
                menu.addItem (2, info.isEnabled ? "Bypass" : "Switch on");
                menu.addSeparator();
                menu.addItem (3, "Move earlier", leftButton.isEnabled());
                menu.addItem (4, "Move later", rightButton.isEnabled());
                menu.addSeparator();
                menu.addItem (5, "Remove");
            }

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                [safeThis = juce::Component::SafePointer<DeviceCard> (this)] (int result)
                                {
                                    if (safeThis == nullptr)
                                        return;

                                    IconButton* chosen[] = { nullptr, &safeThis->openButton, &safeThis->bypassButton,
                                                             &safeThis->leftButton, &safeThis->rightButton, &safeThis->removeButton };

                                    if (juce::isPositiveAndBelow (result, (int) std::size (chosen)) && chosen[result] != nullptr && chosen[result]->onClick)
                                        chosen[result]->onClick();
                                });
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (kind == Kind::add && onAdd && getLocalBounds().contains (e.getPosition()))
                onAdd();
        }

        void mouseEnter (const juce::MouseEvent&) override   { if (kind == Kind::add) repaint(); }
        void mouseExit (const juce::MouseEvent&) override    { if (kind == Kind::add) repaint(); }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu() && openButton.isVisible() && openButton.onClick)
                openButton.onClick();
        }

    private:
        std::unique_ptr<juce::Component> preview;
        bool dropHighlight = false;

        /** The track's volume as a little fader and its pan as a line from the centre. */
        void drawFader (juce::Graphics& g, juce::Rectangle<int> area)
        {
            const auto [db, pan] = faderValues();
            auto r = area.toFloat();
            g.setColour (Theme::display);
            g.fillRoundedRectangle (r, (float) Theme::cornerRadius);
            r = r.reduced (10.0f, 8.0f);

            const auto text = r.removeFromBottom (14.0f);
            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::smallFontSize, Theme::Weight::semibold));
            g.drawText ((db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB", text, juce::Justification::centredLeft, false);
            g.setColour (Theme::textDim);
            g.drawText (std::abs (pan) < 0.01f ? juce::String ("C") : juce::String (juce::roundToInt (std::abs (pan) * 100.0f)) + (pan < 0 ? " L" : " R"),
                        text, juce::Justification::centredRight, false);

            // the fader: a track and a cap at the level (-60..+6 dB)
            const auto panRow = r.removeFromBottom (12.0f);
            const auto track = r.withSizeKeepingCentre (4.0f, r.getHeight() - 4.0f);
            g.setColour (Theme::raised);
            g.fillRoundedRectangle (track, 2.0f);
            const auto position = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
            const auto y = track.getBottom() - track.getHeight() * std::pow (position, 2.0f);
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (track.withTop (y), 2.0f);
            g.setColour (Theme::text);
            g.fillRoundedRectangle (juce::Rectangle<float> (22.0f, 6.0f).withCentre ({ track.getCentreX(), y }), 2.0f);

            // pan: a line from the centre
            const auto mid = panRow.getCentreX();
            const auto line = panRow.withSizeKeepingCentre (panRow.getWidth(), 3.0f);
            g.setColour (Theme::raised);
            g.fillRoundedRectangle (line, 1.5f);
            g.setColour (Theme::accent);
            const auto end = mid + pan * line.getWidth() * 0.5f;
            g.fillRect (juce::Rectangle<float> (juce::jmin (mid, end), line.getY(), juce::jmax (2.0f, std::abs (end - mid)), line.getHeight()));
        }
    };

    //==============================================================================
    /** The scrollable row of cards, with flow arrows between them. */
    class DevicesView::ChainContent : public juce::Component
    {
    public:
        juce::OwnedArray<DeviceCard> cards;
        juce::OwnedArray<VocalChainCard> racks;   // a rack card in the chain, before cards[position]
        std::vector<int> rackPositions;

        /** Every card in chain order. */
        std::vector<juce::Component*> items() const
        {
            std::vector<juce::Component*> list;
            size_t r = 0;

            for (int i = 0; i <= cards.size(); ++i)
            {
                while (r < rackPositions.size() && rackPositions[r] == i)
                    list.push_back (racks[(int) r++]);

                if (i < cards.size())
                    list.push_back (cards[i]);
            }

            return list;
        }

        DeviceCard* findAddCard() const
        {
            for (auto* c : cards)
                if (c->kind == DeviceCard::Kind::add)
                    return c;

            return nullptr;
        }

        void paint (juce::Graphics& g) override
        {
            const auto all = items();

            for (size_t i = 1; i < all.size(); ++i)
            {
                const auto x1 = (float) all[i - 1]->getRight() + 4.0f, x2 = (float) all[i]->getX() - 4.0f;
                const auto y = (float) all[i]->getBounds().getCentreY();
                g.setColour (Theme::textDim);
                g.drawArrow ({ x1, y, x2, y }, 1.2f, 6.0f, 6.0f);
            }
        }

        void layout (int height)
        {
            int x = 0;

            for (auto* item : items())
            {
                auto* card = dynamic_cast<DeviceCard*> (item);
                const auto width = card == nullptr ? VocalChainCard::preferredWidth
                                 : card->kind == DeviceCard::Kind::add ? addCardWidth
                                 : card->kind == DeviceCard::Kind::fader ? faderCardWidth : cardWidthFor (height);
                item->setBounds (x, 0, width, height);
                x += width + cardGap;
            }

            setSize (juce::jmax (0, x - cardGap), height);
        }
    };

    //==============================================================================
    DevicesView::DevicesView (Session& s) : session (s)
    {
        chain = std::make_unique<ChainContent>();
        chainViewport.setViewedComponent (chain.get(), false);
        chainViewport.setScrollBarsShown (false, true);
        chainViewport.setWantsKeyboardFocus (false);
        addAndMakeVisible (chainViewport);

        for (auto* box : { &inputBox, &outputBox })
        {
            box->setWantsKeyboardFocus (false);
            addAndMakeVisible (box);
        }

        inputBox.setTooltip ("What this track records or plays from");
        outputBox.setTooltip ("Where this track's sound goes");

        inputBox.onChange = [this]
        {
            const auto i = inputBox.getSelectedItemIndex();

            if (juce::isPositiveAndBelow (i, inputChoices.size()) && inputChoices[i].id != session.getTrackInput (track))
                report (session.setTrackInput (track, inputChoices[i].id), "Could not change the input");
        };

        outputBox.onChange = [this]
        {
            const auto i = outputBox.getSelectedItemIndex();

            if (juce::isPositiveAndBelow (i, outputChoices.size()) && outputChoices[i].id != session.getTrackOutput (track))
                report (session.setTrackOutput (track, outputChoices[i].id), "Could not change the output");
        };

        session.addListener (this);
        startTimerHz (5);
    }

    DevicesView::~DevicesView()
    {
        session.removeListener (this);
    }

    void DevicesView::report (bool ok, const juce::String& title)
    {
        if (! ok)
        {
            if (onProblem)
                onProblem (title, session.getLastError());

            checkSoon = true;   // show the real state again
            lastSignature = {};
        }
    }

    //==============================================================================
    juce::String DevicesView::makeSignature() const
    {
        if (! session.hasSong() || ! juce::isPositiveAndBelow (track, session.getNumAudioTracks()))
            return "none";

        juce::String sig;
        sig << track << "|" << session.getTrackName (track) << "|" << (int) session.getTrackColour (track).getARGB()
            << "|" << session.getTrackInput (track) << "|" << session.getTrackOutput (track);

        if (auto instrument = session.getInstrumentInfo (track))
            sig << "|I:" << instrument->name;

        for (const auto& fx : session.getEffectInfo (track))
            sig << "|" << fx.id << fx.name << (fx.isEnabled ? "+" : "-");   // the id: two Gates swapping places change nothing else

        // Inputs and outputs can appear (a device plugged in) without anything else changing
        for (const auto& c : session.getTrackInputChoices())   sig << "|i" << c.id;
        for (const auto& c : session.getTrackOutputChoices (track))  sig << "|o" << c.id << c.name;

        return sig;
    }

    void DevicesView::timerCallback()
    {
        if (! isShowing())
            return;

        // Once a second anyway: a knob window's On button changes a plugin without telling the Session
        if (++ticks % 5 == 0)
            checkSoon = true;

        // The highlighted track changes on click, without a notification
        const auto focused = getFocusedTrack ? getFocusedTrack() : -1;

        if (focused != track)
        {
            track = focused;
            checkSoon = true;
        }

        // A drag that ended without telling us (the track went away under it) doesn't leave the chain lit
        if (dropHover)
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container == nullptr || ! container->isDragAndDropActive())
                setDropHover (false);

        if (! std::exchange (checkSoon, false))
            return;

        if (inputBox.isPopupActive() || outputBox.isPopupActive())
        {
            checkSoon = true;   // don't rebuild a menu out from under the user
            return;
        }

        if (auto sig = makeSignature(); sig != lastSignature)
        {
            lastSignature = sig;
            rebuild();
        }
    }

    void DevicesView::refreshRouting()
    {
        inputChoices = session.getTrackInputChoices();
        outputChoices = session.getTrackOutputChoices (track);

        const auto fill = [] (juce::ComboBox& box, const juce::Array<Session::RoutingChoice>& choices, const juce::String& currentID)
        {
            box.clear (juce::dontSendNotification);

            for (int i = 0; i < choices.size(); ++i)
                box.addItem (choices.getReference (i).name, i + 1);

            for (int i = 0; i < choices.size(); ++i)
                if (choices.getReference (i).id == currentID)
                    box.setSelectedItemIndex (i, juce::dontSendNotification);
        };

        fill (inputBox, inputChoices, session.getTrackInput (track));
        fill (outputBox, outputChoices, session.getTrackOutput (track));
    }

    void DevicesView::rebuild()
    {
        chain->cards.clear();
        chain->racks.clear();
        chain->rackPositions.clear();
        const auto valid = session.hasSong() && juce::isPositiveAndBelow (track, session.getNumAudioTracks());

        inputBox.setVisible (valid);
        outputBox.setVisible (valid);

        if (valid)
        {
            refreshRouting();

            if (auto instrument = session.getInstrumentInfo (track))
            {
                auto* card = chain->cards.add (new DeviceCard (DeviceCard::Kind::instrument, *instrument));
                card->openButton.onClick = [this, t = track] { report (session.openInstrumentWindow (t), "Could not open the instrument"); };
            }

            const auto effects = session.getEffectInfo (track);

            for (int i = 0; i < effects.size(); ++i)
            {
                // Session calls only: the view rebuilds on the next tick, never inside this click.
                // Each button acts only if slot i of this track still holds this very effect: a quick
                // double-click on Remove, or a click just after another track was highlighted, does nothing.
                const auto t = track;
                const auto id = effects.getReference (i).id;
                const auto act = [this, t, i, id] (std::function<bool()> action, const juce::String& title)
                {
                    const auto now = session.getEffectInfo (t);

                    if (juce::isPositiveAndBelow (i, now.size()) && now.getReference (i).id == id)
                        report (action(), title);
                };

                if (effects.getReference (i).type == VocalChainPlugin::xmlTypeName)
                {
                    // The Vocal Chain gets its rack card: macros, presets, what's inside
                    auto* rack = chain->racks.add (new VocalChainCard (session, t, i, effects.getReference (i), act, effects.size()));
                    chain->rackPositions.push_back (chain->cards.size());
                    rack->bypassButton.onClick = [this, act, t, i, on = effects.getReference (i).isEnabled]
                        { act ([this, t, i, on] { return session.setEffectEnabled (t, i, ! on); }, "Could not bypass the chain"); };
                    rack->openButton.onClick = [this, act, t, i]
                        { act ([this, t, i] { return session.openEffectWindow (t, i); }, "Could not open the chain"); };
                    rack->removeButton.onClick = [this, act, t, i]
                        { act ([this, t, i] { return session.removeEffect (t, i); }, "Could not remove the chain"); };
                    rack->leftButton.onClick = [this, act, t, i]
                        { act ([this, t, i] { return session.moveEffect (t, i, i - 1); }, "Could not move the chain"); };
                    rack->rightButton.onClick = [this, act, t, i]
                        { act ([this, t, i] { return session.moveEffect (t, i, i + 1); }, "Could not move the chain"); };
                    continue;
                }

                auto* card = chain->cards.add (new DeviceCard (DeviceCard::Kind::effect, effects.getReference (i)));

                // a live picture of its curve: found again by id each time, so it never outlives the effect
                const auto resolve = [this, t, id]() -> te::Plugin*
                {
                    return session.hasSong() ? session.findEffect (t, id) : nullptr;
                };

                if (auto* plugin = resolve())
                    card->setPreview (PluginDisplays::createPreview (*plugin, resolve));

                card->leftButton.setEnabled (i > 0);
                card->rightButton.setEnabled (i < effects.size() - 1);

                card->bypassButton.onClick = [this, act, t, i, on = effects.getReference (i).isEnabled]
                    { act ([this, t, i, on] { return session.setEffectEnabled (t, i, ! on); }, "Could not bypass the effect"); };
                card->openButton.onClick = [this, act, t, i]
                    { act ([this, t, i] { return session.openEffectWindow (t, i); }, "Could not open the effect"); };
                card->removeButton.onClick = [this, act, t, i]
                    { act ([this, t, i] { return session.removeEffect (t, i); }, "Could not remove the effect"); };
                card->leftButton.onClick = [this, act, t, i]
                    { act ([this, t, i] { return session.moveEffect (t, i, i - 1); }, "Could not move the effect"); };
                card->rightButton.onClick = [this, act, t, i]
                    { act ([this, t, i] { return session.moveEffect (t, i, i + 1); }, "Could not move the effect"); };
            }

            auto* add = chain->cards.add (new DeviceCard (DeviceCard::Kind::add, {}));
            add->onAdd = [this, add] { showAddEffectMenu (*add); };
            auto* fader = chain->cards.add (new DeviceCard (DeviceCard::Kind::fader, {}));
            fader->faderValues = [this, t = track] { return std::make_pair (session.getTrackVolumeDb (t), session.getTrackPan (t)); };

            for (auto* item : chain->items())
                chain->addAndMakeVisible (item);

            add->setDropHighlight (dropHover);   // a rebuild in the middle of a drag keeps showing where it lands
        }

        resized();
        repaint();
    }

    //==============================================================================
    void DevicesView::showAddEffectMenu (juce::Component& anchor)
    {
        const auto choices = session.getAvailableEffects();
        juce::PopupMenu menu;
        menu.addSectionHeader ("Add an effect");
        TrackMenus::addEffectChoices (menu, choices, 0);   // IDs index + 1

        // The roadmap, so nobody wonders where the rest is (greyed, not clickable)
        menu.addSeparator();
        menu.addItem (-4, "Your own chains and presets (Phase 4)", false, false);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                            [safeThis = juce::Component::SafePointer<DevicesView> (this), choices, forTrack = track] (int result)
                            {
                                if (safeThis == nullptr || ! juce::isPositiveAndBelow (result - 1, choices.size()))
                                    return;

                                safeThis->report (safeThis->session.addEffect (forTrack, choices.getReference (result - 1), true),
                                                  "Could not add " + choices.getReference (result - 1).name);
                            });
    }

    //==============================================================================
    void DevicesView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::background);
        g.setColour (Theme::panel);
        g.fillRect (inspectorArea);
        g.setColour (Theme::laneSeparator);
        g.drawVerticalLine (inspectorArea.getRight(), 0.0f, (float) getHeight());

        const auto valid = session.hasSong() && juce::isPositiveAndBelow (track, session.getNumAudioTracks());

        if (! valid)
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.drawText ("Click a track to see its instrument and effects here.", getLocalBounds().withTrimmedLeft (inspectorWidth),
                        juce::Justification::centred, true);
            return;
        }

        // Inspector: name with its colour, what kind of track, then the routing labels
        auto name = nameArea;
        const auto colour = session.getTrackColour (track);
        g.setColour (colour.isTransparent() ? Theme::textDim : colour);
        g.fillRoundedRectangle (name.removeFromLeft (10).withSizeKeepingCentre (10, 10).toFloat(), 2.0f);
        name.removeFromLeft (8);
        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::titleFontSize - 0.5f, true));
        g.drawText (session.getTrackName (track), name, juce::Justification::centredLeft, true);

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::smallFontSize));
        const auto instrument = session.getInstrumentInfo (track);
        g.drawText (instrument ? juce::String (juce::CharPointer_UTF8 ("Instrument track \xc2\xb7 ")) + instrument->name : juce::String ("Audio track"),
                    typeArea, juce::Justification::centredLeft, true);
        g.drawText ("Input", inputLabelArea, juce::Justification::bottomLeft, false);
        g.drawText ("Output", outputLabelArea, juce::Justification::bottomLeft, false);

        // The free space after the chain says what it's for (and lights up while an effect is dragged over)
        auto free = chainViewport.getBounds().withTrimmedLeft (chain->getWidth() + cardGap);

        if (free.getWidth() >= 220)
        {
            free = free.withSizeKeepingCentre (juce::jmin (free.getWidth(), 300), 40).withX (free.getX() + 8);
            g.setColour (dropHover ? Theme::accent : Theme::textDim.withAlpha (0.7f));
            g.setFont (Theme::font (Theme::smallFontSize));
            g.drawFittedText (dropHover ? "Drop to add it to " + session.getTrackName (getFocusedTrack ? getFocusedTrack() : track)
                                        : "Drag effects here from the browser, or right-click a card for more",
                              free, juce::Justification::centredLeft, 2);
        }
    }

    //==============================================================================
    bool DevicesView::isInterestedInDragSource (const SourceDetails& d)
    {
        // Effects only: an instrument makes its own track (drop it on the timeline)
        const auto plugin = BrowserDrag::pluginIn (d.description, session);
        const auto target = getFocusedTrack ? getFocusedTrack() : track;   // the panel's track, even between timer ticks
        return plugin.has_value() && ! plugin->second && session.hasSong() && juce::isPositiveAndBelow (target, session.getNumAudioTracks());
    }

    void DevicesView::setDropHover (bool on)
    {
        dropHover = on;

        if (auto* add = chain->findAddCard())
            add->setDropHighlight (on);

        repaint();
    }

    void DevicesView::itemDragEnter (const SourceDetails&)   { setDropHover (true); }
    void DevicesView::itemDragExit (const SourceDetails&)    { setDropHover (false); }

    void DevicesView::itemDropped (const SourceDetails& d)
    {
        setDropHover (false);

        if (const auto plugin = BrowserDrag::pluginIn (d.description, session); plugin.has_value() && ! plugin->second)
        {
            const auto& choice = plugin->first;
            report (session.addEffect (getFocusedTrack ? getFocusedTrack() : track, choice, choice.preset.isEmpty()),
                    "Could not add " + (choice.preset.isNotEmpty() ? choice.preset : choice.name));
        }
    }

    void DevicesView::resized()
    {
        auto r = getLocalBounds();
        inspectorArea = r.removeFromLeft (inspectorWidth);

        auto column = inspectorArea.reduced (12, 10);
        nameArea = column.removeFromTop (20);
        typeArea = column.removeFromTop (18);
        column.removeFromTop (6);
        inputLabelArea = column.removeFromTop (16);
        inputBox.setBounds (column.removeFromTop (24));
        column.removeFromTop (6);
        outputLabelArea = column.removeFromTop (16);
        outputBox.setBounds (column.removeFromTop (24));

        chainViewport.setBounds (r.reduced (12, 10));
        const auto height = chainViewport.getHeight();
        chain->layout (height);

        if (chain->getWidth() > chainViewport.getWidth())
            chain->layout (juce::jmax (0, height - chainViewport.getScrollBarThickness()));   // leave room for the scrollbar
    }
}
