/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "DevicesView.h"
#include "../TrackMenus.h"
#include "VocalChainCard.h"
#include "../../Engine/VocalChain.h"

namespace ez
{
    namespace
    {
        constexpr int inspectorWidth = 200;
        constexpr int cardWidth = 136, cardGap = 22;   // the gap holds the flow arrow
        constexpr int addCardWidth = 96, faderCardWidth = 108;
    }

    //==============================================================================
    /** One device in the chain. Instrument cards have no bypass, move or remove. */
    class DevicesView::DeviceCard : public juce::Component
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
                openButton.setTooltip ("Show its controls");
                addAndMakeVisible (openButton);
            }

            if (kind == Kind::add)
            {
                addButton.setTooltip ("Add an effect to the end of the chain");
                addAndMakeVisible (addButton);
            }
        }

        const Kind kind;
        const Session::DeviceInfo info;

        IconButton bypassButton { "Bypass", Icon::power, {}, IconButton::Style::ghost };
        IconButton openButton   { "Open", Icon::none, "Open" };
        IconButton removeButton { "Remove", Icon::close, {}, IconButton::Style::ghost };
        IconButton leftButton   { "Move earlier", Icon::chevronRight, {}, IconButton::Style::ghost };   // drawn mirrored below
        IconButton rightButton  { "Move later", Icon::chevronRight, {}, IconButton::Style::ghost };
        IconButton addButton    { "Add effect", Icon::plus, "Add effect", IconButton::Style::ghost };

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);

            if (kind == Kind::add)
            {
                g.setColour (Theme::activeFill.withAlpha (0.6f));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (Theme::chipBorder);
                juce::Path outline, dashed;
                outline.addRoundedRectangle (r, 6.0f);
                const float dashes[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
                g.fillPath (dashed);
                return;
            }

            g.setColour (Theme::panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (Theme::border);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);

            auto content = getLocalBounds().reduced (10, 8);

            if (kind == Kind::effect)
                content.removeFromTop (2);

            // Title row (the bypass button sits at its left for effects)
            auto title = content.removeFromTop (18);

            if (kind == Kind::effect)
                title.removeFromLeft (22);

            const bool dimmed = kind == Kind::effect && ! info.isEnabled;
            g.setColour (dimmed ? Theme::textDim : Theme::text);
            g.setFont (Theme::font (Theme::baseFontSize, true));
            g.drawText (kind == Kind::fader ? juce::String ("Volume & pan") : info.name, title, juce::Justification::centredLeft, true);

            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize - 0.5f));
            const auto subtitle = kind == Kind::instrument ? juce::String (juce::CharPointer_UTF8 ("Instrument \xc2\xb7 ")) + info.maker
                                : kind == Kind::fader      ? juce::String ("The track's fader and pan")
                                : dimmed                   ? juce::String (juce::CharPointer_UTF8 ("Bypassed \xc2\xb7 ")) + info.maker
                                                           : info.maker;
            g.drawText (subtitle, content.removeFromTop (16), juce::Justification::centredLeft, true);

            // A small picture of what it is
            content.removeFromTop (6);
            auto picture = content.removeFromTop (juce::jmax (0, content.getHeight() - 58));

            if (picture.getHeight() > 16)
            {
                g.setColour (Theme::display);
                g.fillRoundedRectangle (picture.toFloat(), (float) Theme::cornerRadius);
                const auto icon = kind == Kind::instrument ? Icon::piano : kind == Kind::fader ? Icon::sliders
                                                                         : info.isBuiltIn ? Icon::sliders : Icon::plug;
                Icons::draw (g, icon, picture.toFloat().withSizeKeepingCentre (22.0f, 22.0f),
                             dimmed ? Theme::border : juce::Colour (0xff7fbfff));
            }
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            // The "move earlier" arrow is the chevron flipped
            if (leftButton.isVisible())
            {
                auto area = leftButton.getBounds().toFloat();
                g.setColour (leftButton.isEnabled() ? Theme::text : Theme::border);
                juce::Path p;
                p.startNewSubPath (area.getCentreX() + 3.0f, area.getCentreY() - 6.0f);
                p.lineTo (area.getCentreX() - 3.0f, area.getCentreY());
                p.lineTo (area.getCentreX() + 3.0f, area.getCentreY() + 6.0f);
                g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);

            if (kind == Kind::add)
            {
                addButton.setBounds (getLocalBounds().reduced (4));
                return;
            }

            if (kind == Kind::effect)
            {
                bypassButton.setBounds (r.getX() - 2, r.getY(), 22, 22);
                auto bottom = r.removeFromBottom (22);
                leftButton.setBounds (bottom.removeFromLeft (24));
                rightButton.setBounds (bottom.removeFromLeft (24));
                removeButton.setBounds (bottom.removeFromRight (24));
                r.removeFromBottom (6);
            }

            if (openButton.isVisible())
                openButton.setBounds (r.removeFromBottom (24));
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
                                 : card->kind == DeviceCard::Kind::fader ? faderCardWidth : cardWidth;
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
            add->addButton.onClick = [this, add] { showAddEffectMenu (*add); };
            chain->cards.add (new DeviceCard (DeviceCard::Kind::fader, {}));

            for (auto* item : chain->items())
                chain->addAndMakeVisible (item);
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
