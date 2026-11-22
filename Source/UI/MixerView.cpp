/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "MixerView.h"
#include "UiMode.h"
#include "LevelMeter.h"
#include "TrackMenus.h"
#include "Theme.h"

namespace ez
{
    namespace
    {
        constexpr int headerHeight = 34, wideStrip = 96, narrowStrip = 74, stripGap = 4, insertSlots = 4;
        const float faderMarks[] = { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };

        juce::String panText (double pan)
        {
            const auto amount = juce::roundToInt (std::abs (pan) * 100.0);
            return amount == 0 ? juce::String ("C") : (pan < 0 ? "L" : "R") + juce::String (amount);
        }

        juce::String dbText (float db)
        {
            return db <= Session::minVolumeDb + 0.05f ? juce::String ("-inf") : (db > 0.05f ? "+" : "") + juce::String (db, 1);
        }

        /** A send knob: a rotary slider whose right-click opens the send's menu instead of dragging. */
        class SendKnob : public juce::Slider
        {
        public:
            std::function<void()> onMenu, onTouched;

            void mouseDown (const juce::MouseEvent& e) override
            {
                menuClick = e.mods.isPopupMenu();

                if (onTouched)
                    onTouched();

                if (menuClick)
                {
                    if (onMenu)
                        onMenu();

                    return;
                }

                juce::Slider::mouseDown (e);
            }

            void mouseDrag (const juce::MouseEvent& e) override     { if (! menuClick) juce::Slider::mouseDrag (e); }
            void mouseUp (const juce::MouseEvent& e) override       { if (! std::exchange (menuClick, false)) juce::Slider::mouseUp (e); }

        private:
            bool menuClick = false;
        };

        constexpr int sendRowHeight = 20, sendsCaption = 12, sectionGap = 8;
    }

    //==============================================================================
    /** One channel strip (trackIndex -1 = Master). */
    class MixerView::Strip : public juce::Component
    {
    public:
        Strip (MixerView& o, int index) : owner (o), session (o.session), trackIndex (index),
                                          meter (o.session, index, false)
        {
            const bool master = isMaster();

            name.setEditable (false, ! master);
            name.setWantsKeyboardFocus (false);   // the shell never keeps the keyboard (Space, arrows...)
            name.setFont (Theme::font (Theme::smallFontSize + 0.5f, true));
            name.setColour (juce::Label::textColourId, Theme::text);
            name.setTooltip (master ? "The whole mix" : "Double-click to rename, right-click for more");
            name.setInterceptsMouseClicks (true, false);
            name.onTextChange = [this]
            {
                if (! session.renameTrack (trackIndex, name.getText()))
                    refresh();
            };
            addAndMakeVisible (name);

            for (auto* box : { &input, &output })
            {
                box->setScrollWheelEnabled (false);
                box->setWantsKeyboardFocus (false);
                addAndMakeVisible (box);
            }

            input.setTooltip ("Where this track records from");
            output.setTooltip ("Where this track's sound goes: the main mix, a bus, or straight to an output");

            // Buses and returns have no input: this says what plays into them instead
            routeInfo.setFont (Theme::font (Theme::captionFontSize + 0.5f));
            routeInfo.setColour (juce::Label::textColourId, Theme::textDim);
            routeInfo.setJustificationType (juce::Justification::centredLeft);
            routeInfo.setInterceptsMouseClicks (false, false);
            addChildComponent (routeInfo);
            input.onChange = [this]
            {
                const auto i = input.getSelectedItemIndex();

                if (juce::isPositiveAndBelow (i, inputIDs.size()) && ! session.setTrackInput (trackIndex, inputIDs[i]))
                    owner.report (session.getLastError());

                refresh();
            };
            output.onChange = [this]
            {
                const auto i = output.getSelectedItemIndex();

                if (juce::isPositiveAndBelow (i, outputIDs.size()) && ! session.setTrackOutput (trackIndex, outputIDs[i]))
                    owner.report (session.getLastError());

                refresh();
            };

            if (master)
            {
                input.setVisible (false);
                output.setEnabled (false);
                output.setTooltip ("The mix plays on the output chosen at the top right of the window (and in Setup)");
            }

            pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.setPopupDisplayEnabled (true, true, nullptr);
            pan.textFromValueFunction = [] (double v) { return panText (v); };
            pan.setTooltip ("Pan: drag or scroll; double-click for centre");
            // Drags are gestures too: with Write on and the song playing, they record into the lanes
            pan.onDragStart = [this] { session.beginUndoStep(); panDragging = true; if (! isMaster()) session.beginParameterGesture (trackIndex, session.getPanParam (trackIndex)); };
            pan.onDragEnd = [this] { panDragging = false; if (! isMaster()) session.endParameterGesture (trackIndex, session.getPanParam (trackIndex)); refresh(); };
            pan.onValueChange = [this]
            {
                if (! panDragging)
                    session.beginUndoStep();

                if (isMaster())   session.setMasterPan ((float) pan.getValue());
                else if (! session.setTrackPan (trackIndex, (float) pan.getValue()) && ! panDragging)
                    refresh();   // automated: the lane wins (the status bar says why)

                panLabel.setText (panText (pan.getValue()), juce::dontSendNotification);
            };

            fader.setSliderStyle (juce::Slider::LinearVertical);
            fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            fader.setRange (Session::minVolumeDb, Session::maxVolumeDb, 0.1);
            fader.setSkewFactorFromMidPoint (-12.0);
            fader.setDoubleClickReturnValue (true, 0.0);
            fader.setVelocityModeParameters (0.3, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Shift: fine (slow velocity mode)
            fader.setTooltip ("Volume: drag (Shift for fine) or scroll; double-click for 0 dB; click the number below to type");
            fader.setColour (juce::Slider::trackColourId, Theme::accent);
            fader.setPopupDisplayEnabled (true, true, nullptr);   // the value beside the cap while dragging
            fader.textFromValueFunction = [] (double db) { return dbText ((float) db) + " dB"; };
            fader.setColour (juce::Slider::backgroundColourId, Theme::display);
            fader.setColour (juce::Slider::thumbColourId, juce::Colour (0xffe4e8ee));
            fader.onDragStart = [this] { session.beginUndoStep(); faderDragging = true; if (! isMaster()) session.beginParameterGesture (trackIndex, session.getVolumeParam (trackIndex)); };
            fader.onDragEnd = [this] { faderDragging = false; if (! isMaster()) session.endParameterGesture (trackIndex, session.getVolumeParam (trackIndex)); refresh(); };
            fader.onValueChange = [this]
            {
                if (! faderDragging)
                    session.beginUndoStep();

                applyVolume ((float) fader.getValue());
            };

            volumeLabel.setEditable (false, true);
            volumeLabel.setWantsKeyboardFocus (false);
            volumeLabel.setJustificationType (juce::Justification::centredLeft);
            volumeLabel.setFont (Theme::font (Theme::captionFontSize + 0.5f));
            volumeLabel.setColour (juce::Label::textColourId, Theme::text);
            volumeLabel.setTooltip ("Volume in dB: double-click to type");
            volumeLabel.onTextChange = [this]
            {
                const auto text = volumeLabel.getText().trim();
                const auto digits = text.retainCharacters ("0123456789");

                if (text.containsIgnoreCase ("inf") || text.containsIgnoreCase ("off"))
                {
                    session.beginUndoStep();
                    applyVolume (Session::minVolumeDb);
                }
                else if (digits.isNotEmpty())
                {
                    session.beginUndoStep();
                    applyVolume (text.retainCharacters ("-+.0123456789").getFloatValue());
                }

                refresh();   // anything else: put the real value back
            };

            peakLabel.setJustificationType (juce::Justification::centredRight);
            peakLabel.setFont (Theme::font (Theme::captionFontSize + 0.5f));
            peakLabel.setTooltip ("Highest level since reset: click to reset");


            panLabel.setJustificationType (juce::Justification::centred);
            panLabel.setFont (Theme::font (Theme::captionFontSize));
            panLabel.setColour (juce::Label::textColourId, Theme::textDim);
            panLabel.setInterceptsMouseClicks (false, false);

            for (auto* c : std::initializer_list<juce::Component*> { &pan, &fader, &volumeLabel, &peakLabel, &panLabel, &meter })
            {
                c->setWantsKeyboardFocus (false);
                addAndMakeVisible (c);
            }

            if (! master)
            {
                muteButton.setTooltip ("Mute (M)");
                soloButton.setTooltip ("Solo: hear only soloed tracks (S)");
                armButton.setTooltip ("Arm for recording");
                muteButton.setColour (juce::TextButton::buttonOnColourId, Theme::mute);
                soloButton.setColour (juce::TextButton::buttonOnColourId, Theme::solo);
                armButton.setColour (juce::TextButton::buttonOnColourId, Theme::record);
                // The design's on colours: amber mute and blue solo with dark text, red arm with white
                muteButton.setColour (juce::TextButton::textColourOnId, Theme::ink);
                soloButton.setColour (juce::TextButton::textColourOnId, Theme::ink);
                armButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
                muteButton.onClick = [this] { session.setTrackMuted (trackIndex, ! session.isTrackMuted (trackIndex)); refresh(); };
                soloButton.onClick = [this] { session.setTrackSoloed (trackIndex, ! session.isTrackSoloed (trackIndex)); refresh(); };
                armButton.onClick = [this]
                {
                    if (! session.setTrackArmed (trackIndex, ! session.isTrackArmed (trackIndex)))
                        owner.report (session.getLastError());

                    refresh();
                };

                for (auto* b : { &muteButton, &soloButton, &armButton })
                {
                    b->setWantsKeyboardFocus (false);
                    addAndMakeVisible (b);
                }
            }

            addMouseListener (this, true);   // any click in the strip (fader, pan, buttons...) highlights its track
            refresh();
        }

        bool isMaster() const noexcept      { return trackIndex < 0; }
        ~Strip() override
        {
            // A rebuild mid-drag never calls onDragEnd: end the gesture, or Write can't record that control again
            if (isMaster())
                return;

            if (faderDragging)  session.endParameterGesture (trackIndex, session.getVolumeParam (trackIndex));
            if (panDragging)    session.endParameterGesture (trackIndex, session.getPanParam (trackIndex));

            if (sendDragging)
                for (const auto& r : returns)
                    session.endParameterGesture (trackIndex, session.getSendParam (trackIndex, r.bus));
        }

        int getTrackIndex() const noexcept  { return trackIndex; }

        void nudgeVolume (float db)
        {
            session.beginUndoStep();
            applyVolume (currentVolume() + db);
            refresh();
        }

        /** [ / ]: the picked send (or the first). ] on a send that isn't there yet starts it at -24 dB; [ leaves it off. */
        bool nudgeSend (float db)
        {
            if (sendKnobs.isEmpty() || ! owner.showSends)
                return false;   // never change a control you can't see

            int i = 0;

            for (int k = 0; k < (int) returns.size(); ++k)
                if (returns[(size_t) k].bus == owner.pickedSendBus)
                    i = k;

            const auto& info = sendInfo[(size_t) i];

            if (! info.exists && db < 0)
                return true;   // already as low as it goes: don't make a silent send

            const auto target = info.exists && info.levelDb > Session::minVolumeDb ? info.levelDb + db : (db > 0 ? -24.0f : Session::minVolumeDb);
            owner.pickedSendBus = returns[(size_t) i].bus;

            if (! session.setSendLevel (trackIndex, returns[(size_t) i].bus, juce::jlimit (Session::minVolumeDb, Session::maxVolumeDb, target)))
                owner.report (session.getLastError());

            refresh();
            return true;
        }

        void refresh()
        {
            const bool master = isMaster();

            if (! name.isBeingEdited())
                name.setText (master ? juce::String ("Master") : session.getTrackName (trackIndex), juce::dontSendNotification);

            colour = master ? Theme::accent : session.getTrackColour (trackIndex);   // the master wears the accent: the strip everything ends in

            if (! fader.isMouseButtonDown())
                fader.setValue (currentVolume(), juce::dontSendNotification);

            if (! pan.isMouseButtonDown())
                pan.setValue (master ? session.getMasterPan() : session.getTrackPan (trackIndex), juce::dontSendNotification);

            panLabel.setText (panText (pan.getValue()), juce::dontSendNotification);

            if (! volumeLabel.isBeingEdited())
                volumeLabel.setText (dbText ((float) fader.getValue()), juce::dontSendNotification);

            if (! master)
            {
                const auto newKind = session.getTrackKind (trackIndex);

                if (newKind != kind)
                {
                    kind = newKind;
                    resized();
                }

                muteButton.setToggleState (session.isTrackMuted (trackIndex), juce::dontSendNotification);
                soloButton.setToggleState (session.isTrackSoloed (trackIndex), juce::dontSendNotification);
                armButton.setToggleState (session.isTrackArmed (trackIndex), juce::dontSendNotification);
                armButton.setEnabled (kind == Session::TrackKind::normal);
                armButton.setTooltip (kind == Session::TrackKind::normal ? "Arm for recording" : "Buses and FX returns don't record");
                soloButton.setTooltip (kind == Session::TrackKind::fxReturn ? "Solo: hear only this return. Returns are solo-safe: soloing a track keeps them (S)"
                                                                            : "Solo: hear only soloed tracks (S)");
                name.setTooltip (kind == Session::TrackKind::bus ? "Bus: tracks can play into it (their Output). Double-click to rename, right-click for more"
                               : kind == Session::TrackKind::fxReturn ? "FX return: plays what tracks send to it through its effects. Double-click to rename, right-click for more"
                               : "Double-click to rename, right-click for more");
                refreshRouting (input, inputIDs, session.getTrackInputChoices(), session.getTrackInput (trackIndex));
                refreshRouting (output, outputIDs, session.getTrackOutputChoices (trackIndex), session.getTrackOutput (trackIndex));
                refreshRouteInfo();
                refreshSends();
                // Slot 0 is the instrument (if any), then the effects in signal order
                instrument = session.getInstrumentInfo (trackIndex);
                effects = session.getEffectInfo (trackIndex);
                inserts.clear();

                if (instrument.has_value())
                    inserts.add (instrument->name);

                for (auto& fx : effects)
                    inserts.add (fx.name);
            }
            else
            {
                output.clear (juce::dontSendNotification);
                output.addItem ("Main out", 1);
                output.setSelectedId (1, juce::dontSendNotification);
            }

            repaint();
        }

        /** Meter numbers (a few times a second). */
        void refreshMeters()
        {
            const auto reading = isMaster() ? static_cast<MeterBank::Reading> (session.getMeters().getMain())
                                            : session.getMeters().getTrack (trackIndex);
            const auto text = reading.maxDb <= MeterBank::floorDb + 0.01f ? juce::String ("-inf") : juce::String (reading.maxDb, 1);

            if (peakLabel.getText() != text)
                peakLabel.setText (text, juce::dontSendNotification);

            peakLabel.setColour (juce::Label::textColourId, reading.clipped ? Theme::record : Theme::textDim);

            // Automated controls move as the song plays: follow them here, not twice a second
            if (! isMaster() && session.isPlaying() && session.isAutomationReadOn())
            {
                if (! fader.isMouseButtonDown())
                    fader.setValue (session.getTrackVolumeDb (trackIndex), juce::dontSendNotification);

                if (! pan.isMouseButtonDown())
                    pan.setValue (session.getTrackPan (trackIndex), juce::dontSendNotification);

                for (size_t i = 0; i < returns.size() && (int) i < sendKnobs.size(); ++i)
                    if (! sendKnobs[(int) i]->isMouseButtonDown() && session.isAutomated (trackIndex, session.getSendParam (trackIndex, returns[i].bus)))
                        sendKnobs[(int) i]->setValue (session.getSend (trackIndex, returns[i].bus).levelDb, juce::dontSendNotification);
            }

            if (isMaster())
            {
                const auto loud = session.getMeters().getMain().loudness;
                const auto lufs = loud.shortTermValid && loud.shortTermLufs > -99.0f ? juce::String (loud.shortTermLufs, 1) + " LUFS" : juce::String ("--.- LUFS");

                if (lufs != lufsText)
                {
                    lufsText = lufs;
                    repaint (lufsArea);
                }
            }
        }

        void paint (juce::Graphics& g) override
        {
            const bool highlighted = ! isMaster() && owner.getFocusedTrack && owner.getFocusedTrack() == trackIndex;
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            constexpr auto radius = (float) Theme::cardRadius;
            g.setColour (highlighted ? Theme::raised : Theme::panel);
            g.fillRoundedRectangle (r, radius);

            // The track's colour: a wash down from the top and a bar under the name, so a strip
            // matches its lane in the arrangement
            if (! colour.isTransparent())
            {
                g.setGradientFill (juce::ColourGradient::vertical (colour.withAlpha (highlighted ? 0.18f : 0.11f), r.getY(),
                                                                   colour.withAlpha (0.0f), r.getY() + 90.0f));
                g.fillRoundedRectangle (r, radius);
                g.setColour (colour);
                g.fillRoundedRectangle (juce::Rectangle<float> ((float) name.getX(), (float) name.getBottom(), (float) name.getWidth(), 3.0f), 1.5f);
            }

            g.setColour (highlighted ? Theme::accent : isMaster() ? Theme::accent.withAlpha (0.45f) : Theme::hairline.brighter (0.1f));
            g.drawRoundedRectangle (r, radius, highlighted ? 1.5f : 1.0f);

            if (! kindChip.isEmpty())
            {
                const auto chip = kindChip.toFloat().withSizeKeepingCentre ((float) kindChip.getWidth(), 14.0f);
                g.setColour (kind == Session::TrackKind::bus ? Theme::selection.withAlpha (0.35f) : Theme::accent.withAlpha (0.35f));
                g.fillRoundedRectangle (chip, 3.0f);
                g.setColour (Theme::text);
                g.setFont (Theme::font (Theme::captionFontSize - 1.0f, true));
                g.drawText (kind == Session::TrackKind::bus ? "BUS" : "FX", chip, juce::Justification::centred, false);
            }

            paintSends (g);

            // Inserts
            if (! insertsArea.isEmpty())
            {
                drawCaption (g, "Inserts", insertsArea.withHeight (12));

                if (! isMaster() && session.isTrackFrozen (trackIndex))
                {
                    // Frozen: its effects are rendered in, so say so where they're listed
                    g.setColour (Theme::frozen);
                    g.setFont (Theme::font (Theme::captionFontSize, true));
                    g.drawText ("FROZEN", insertsArea.withHeight (12).withTrimmedRight (inserts.size() > slotsShown ? 18 : 0),
                                juce::Justification::centredRight, false);   // left of the "+N more" count
                }

                bool firstEmpty = true;

                for (int slot = 0; slot < slotsShown; ++slot)
                {
                    const auto box = slotBounds (slot).toFloat();
                    const auto label = slotLabel (slot);

                    if (label.isEmpty())
                    {
                        // Empty: the first is a shallow well saying where a new effect goes; the rest stay quiet
                        // (a faint outline) until hovered, so a strip doesn't look like a column of broken boxes
                        const bool isAddSlot = firstEmpty;
                        const bool hovered = slot == hoveredSlot;

                        if (isAddSlot || hovered)
                        {
                            g.setColour (hovered ? Theme::hover.withAlpha (0.6f) : Theme::display.withAlpha (0.55f));
                            g.fillRoundedRectangle (box, (float) Theme::smallRadius);
                        }

                        g.setColour (hovered ? Theme::border : isAddSlot ? Theme::hairline : Theme::hairline.withAlpha (0.45f));
                        g.drawRoundedRectangle (box.reduced (0.5f), (float) Theme::smallRadius, 1.0f);

                        if ((std::exchange (firstEmpty, false) || hovered) && box.getHeight() >= 14.0f)
                        {
                            g.setColour (Theme::textFaint);
                            g.setFont (Theme::font (Theme::captionFontSize + 0.5f));
                            g.drawText ("+ Add", box.reduced (6.0f, 0.0f), juce::Justification::centredLeft, false);
                        }

                        continue;
                    }

                    const bool bypassed = slotIsBypassed (slot);
                    g.setColour (slot == hoveredSlot ? Theme::hover : Theme::raised);
                    g.fillRoundedRectangle (box, (float) Theme::smallRadius);
                    // A small light: on (green) or bypassed (dim)
                    g.setColour (bypassed ? Theme::textFaint : Theme::success);
                    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ box.getX() + 6.0f, box.getCentreY() }));
                    g.setColour (Theme::border);
                    g.drawRoundedRectangle (box.reduced (0.5f), (float) Theme::smallRadius, 1.0f);
                    g.setColour (bypassed ? Theme::textDim : Theme::text);
                    g.setFont (Theme::font (Theme::captionFontSize + 0.5f));
                    g.drawText (label, box.reduced (5.0f, 0.0f).withTrimmedLeft (7.0f), juce::Justification::centredLeft, true);
                }

                if (inserts.size() > slotsShown)
                {
                    g.setColour (Theme::textDim);
                    g.setFont (Theme::font (Theme::captionFontSize - 0.5f));
                    g.drawText ("+" + juce::String (inserts.size() - slotsShown), insertsArea.withHeight (12), juce::Justification::centredRight, false);
                }
            }

            // dB marks beside the fader
            g.setFont (Theme::font (Theme::captionFontSize - 1.0f));
            g.setColour (Theme::textDim);

            for (auto db : faderMarks)
            {
                if (fader.getHeight() < 120 && (db == -6.0f || db == -48.0f))
                    continue;   // a short fader keeps fewer marks, so they never overlap

                const auto y = fader.getY() + (int) fader.getPositionOfValue (db);
                g.drawText (db > 0 ? "+" + juce::String ((int) db) : juce::String ((int) db), juce::Rectangle<int> (scaleX, y - 6, 20, 12),
                            juce::Justification::centredRight, false);
            }

            if (isMaster() && ! lufsArea.isEmpty())
            {
                g.setColour (Theme::text);
                g.setFont (Theme::font (Theme::captionFontSize));
                g.drawText (lufsText, lufsArea, juce::Justification::centred, false);
            }
        }

        void resized() override
        {
            const bool master = isMaster();
            auto r = getLocalBounds().reduced (5, 0);
            auto nameRow = r.removeFromTop (26);
            kindChip = kind != Session::TrackKind::normal && ! master ? nameRow.removeFromRight (kind == Session::TrackKind::bus ? 28 : 20) : juce::Rectangle<int>();
            name.setBounds (nameRow);
            r.removeFromTop (8);

            // Short strips drop the less used rows first (MixerView::computeRows): the same for every strip,
            // the master included, so the faders line up
            const auto rows = owner.rows;
            const bool io = rows.io;
            slotsShown = rows.slots;
            sendRowsShown = rows.sends;
            const bool plainTrack = kind == Session::TrackKind::normal;

            input.setVisible (io && ! master && plainTrack);
            routeInfo.setVisible (io && ! master && ! plainTrack);
            output.setVisible (io);

            if (io)
            {
                if (! master)
                {
                    input.setBounds (r.removeFromTop (20));
                    routeInfo.setBounds (input.getBounds());
                }
                else
                {
                    r.removeFromTop (20);
                }

                r.removeFromTop (3);
                output.setBounds (r.removeFromTop (20));
                r.removeFromTop (8);
            }

            insertsArea = slotsShown > 0 ? r.removeFromTop (12 + slotsShown * 21) : juce::Rectangle<int>();

            if (slotsShown > 0)
                r.removeFromTop (8);

            sendsArea = sendRowsShown > 0 ? r.removeFromTop (sendsCaption + sendRowsShown * sendRowHeight) : juce::Rectangle<int>();

            if (sendRowsShown > 0)
                r.removeFromTop (sectionGap);

            for (int i = 0; i < sendKnobs.size(); ++i)
            {
                sendKnobs[i]->setVisible (i < sendRowsShown);

                if (i < sendRowsShown)
                    sendKnobs[i]->setBounds (sendRowBounds (i).removeFromLeft (18).withSizeKeepingCentre (18, 18));
            }

            auto panRow = r.removeFromTop (30);
            pan.setBounds (panRow.withSizeKeepingCentre (30, 30));
            panLabel.setBounds (r.removeFromTop (12));
            r.removeFromTop (6);

            auto bottom = r.removeFromBottom (22);   // M/S/R, or the master's loudness: the same height, so the dB marks line up
            r.removeFromBottom (4);
            auto values = r.removeFromBottom (16);
            r.removeFromBottom (4);

            if (master)
                lufsArea = bottom;
            else
            {
                const auto w = (bottom.getWidth() - 4) / 3;
                muteButton.setBounds (bottom.removeFromLeft (w));
                bottom.removeFromLeft (2);
                soloButton.setBounds (bottom.removeFromLeft (w));
                bottom.removeFromLeft (2);
                armButton.setBounds (bottom);
            }

            volumeLabel.setBounds (values.removeFromLeft (values.getWidth() / 2));
            peakLabel.setBounds (values);

            // Scale | fader | meter
            const auto meterWidth = getWidth() < wideStrip ? 8 : 10;
            meter.setBounds (r.removeFromRight (meterWidth).withTrimmedTop (4).withTrimmedBottom (4));
            r.removeFromRight (2);
            scaleX = r.getX() - 4;
            r.removeFromLeft (16);
            fader.setBounds (r);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            int over = -1;

            if (e.eventComponent == this)
                for (int slot = 0; slot < slotsShown; ++slot)
                    if (slotBounds (slot).contains (e.getPosition()))
                        over = slot;

            if (over != hoveredSlot)
            {
                hoveredSlot = over;
                repaint (insertsArea);
            }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (std::exchange (hoveredSlot, -1) >= 0)
                repaint (insertsArea);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! isMaster() && owner.setFocusedTrack)
            {
                owner.setFocusedTrack (trackIndex);
                owner.repaintStrips();
            }

            owner.focus.setFocus (true);

            if (e.eventComponent != this && e.eventComponent != &name && e.eventComponent != &peakLabel)
                return;   // a fader, knob or button: it does its own thing (the track is highlighted above)

            if (e.eventComponent == &peakLabel)
            {
                if (isMaster())   session.getMeters().resetMain();
                else              session.getMeters().resetTrack (trackIndex);

                refreshMeters();
                return;
            }

            if (e.eventComponent == &name)
            {
                if (e.mods.isPopupMenu() && ! isMaster())
                    showTrackMenu();

                return;
            }

            for (int slot = 0; slot < slotsShown; ++slot)
                if (slotBounds (slot).contains (e.getPosition()))
                {
                    clickSlot (slot, e.mods.isPopupMenu());
                    return;
                }

            if (sendsArea.contains (e.getPosition()) && ! sendKnobs.isEmpty())
            {
                if (e.getPosition().y < sendsArea.getY() + sendsCaption)
                {
                    showAllSendsMenu();   // the caption (and its "+N"): every send
                    return;
                }

                for (int i = 0; i < juce::jmin (sendRowsShown, sendKnobs.size()); ++i)
                    if (sendRowBounds (i).contains (e.getPosition()))
                    {
                        pickSend (i);

                        if (e.mods.isPopupMenu())
                            showSendMenu (i);

                        return;
                    }
            }

            if (e.mods.isPopupMenu() && ! isMaster())
                showTrackMenu();
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            // We listen to our children too: over a fader or knob the wheel changes it, and must not also scroll
            if (dynamic_cast<juce::Slider*> (e.eventComponent) != nullptr)
                return;

            owner.mouseWheelMove (e.getEventRelativeTo (&owner), w);   // the strip itself scrolls the mixer
        }

    private:
        MixerView& owner;
        Session& session;
        const int trackIndex;
        juce::Label name, volumeLabel, peakLabel, panLabel;
        juce::ComboBox input, output;
        juce::StringArray inputIDs, outputIDs, inserts;
        juce::Array<Session::DeviceInfo> effects;
        std::optional<Session::DeviceInfo> instrument;
        juce::Slider pan, fader;
        LevelMeter meter;
        juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton { "R" };
        juce::Colour colour;
        juce::Rectangle<int> insertsArea, lufsArea, sendsArea, kindChip;
        juce::String lufsText;
        int scaleX = 0, slotsShown = insertSlots, sendRowsShown = 0, hoveredSlot = -1;
        bool panDragging = false, faderDragging = false, sendDragging = false, sendGestureStarted = false;
        Session::TrackKind kind = Session::TrackKind::normal;
        juce::Label routeInfo;
        std::vector<Session::ReturnInfo> returns;     // one knob per return, in this order
        std::vector<Session::SendInfo> sendInfo;
        juce::OwnedArray<SendKnob> sendKnobs;

        juce::Rectangle<int> sendRowBounds (int i) const
        {
            return { sendsArea.getX(), sendsArea.getY() + sendsCaption + i * sendRowHeight, sendsArea.getWidth(), sendRowHeight };
        }

        void refreshRouteInfo()
        {
            juce::String text, tip;

            if (kind == Session::TrackKind::bus)
            {
                const auto into = session.getTracksIntoBus (trackIndex);
                text = into.isEmpty() ? juce::String ("Nothing in yet") : juce::String (into.size()) + (into.size() == 1 ? " track in" : " tracks in");
                tip = "Tracks playing into this bus";

                for (auto t : into)
                    tip << "\n" << session.getTrackName (t);

                if (into.isEmpty())
                    tip << ": none yet. Pick this bus as a track's Output (or right-click a track: Play into a new bus)";
            }
            else if (kind == Session::TrackKind::fxReturn)
            {
                int bus = -1, senders = 0;

                for (const auto& r : session.getReturns())
                    if (r.trackIndex == trackIndex)
                        bus = r.bus;

                for (int t = 0; t < session.getNumAudioTracks(); ++t)
                    if (t != trackIndex && session.getSend (t, bus).exists)
                        ++senders;

                text = senders == 0 ? juce::String ("No sends yet") : juce::String (senders) + (senders == 1 ? " send in" : " sends in");
                tip = "Tracks send to this return with their Sends knobs";
            }

            routeInfo.setText (text, juce::dontSendNotification);
            routeInfo.setTooltip (tip);
        }

        void refreshSends()
        {
            auto newReturns = session.getReturns();
            const bool sends = session.canSend (trackIndex);
            bool sameBuses = newReturns.size() == returns.size();

            for (size_t i = 0; sameBuses && i < newReturns.size(); ++i)
                sameBuses = newReturns[i].bus == returns[i].bus;

            const bool dragging = std::any_of (sendKnobs.begin(), sendKnobs.end(), [] (auto* k) { return k->isMouseButtonDown(); });

            if ((! sameBuses || sendKnobs.size() != (sends ? (int) newReturns.size() : 0)) && ! dragging)
            {
                sendKnobs.clear();

                if (sends)
                    for (size_t i = 0; i < newReturns.size(); ++i)
                        addAndMakeVisible (sendKnobs.add (makeSendKnob ((int) i)));

                returns = newReturns;
                resized();
            }
            else if (! dragging)
            {
                returns = newReturns;   // names may have changed; never mid-drag (knob i must keep its bus)
            }

            sendInfo.clear();

            for (const auto& r : returns)
                sendInfo.push_back (sends ? session.getSend (trackIndex, r.bus) : Session::SendInfo {});

            for (int i = 0; i < sendKnobs.size() && i < (int) returns.size(); ++i)
            {
                auto* k = sendKnobs[i];
                const auto& info = sendInfo[(size_t) i];

                if (! k->isMouseButtonDown())
                    k->setValue (info.exists ? info.levelDb : Session::minVolumeDb, juce::dontSendNotification);

                const bool live = info.exists && info.enabled && info.levelDb > Session::minVolumeDb;
                k->setColour (juce::Slider::rotarySliderFillColourId, ! live ? Theme::textDim : info.preFader ? Theme::accent : Theme::selection);
                k->setColour (juce::Slider::rotarySliderOutlineColourId, Theme::display);
                k->setTooltip ("Send to " + returns[(size_t) i].name + ": "
                               + (info.exists ? dbText (info.levelDb) + " dB" + (info.preFader ? ", pre-fader" : ", post-fader") + (info.enabled ? "" : ", muted") : juce::String ("off"))
                               + ". Drag (Shift: fine) or scroll; double-click or Alt+click: 0 dB; right-click: pre/post, mute, remove; [ / ]: -/+ 1 dB");
            }
        }

        SendKnob* makeSendKnob (int i)
        {
            auto* k = new SendKnob();
            k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k->setRange (Session::minVolumeDb, Session::maxVolumeDb, 0.1);
            k->setSkewFactorFromMidPoint (-12.0);
            k->setDoubleClickReturnValue (true, 0.0);   // and Alt+click (JUCE's single-click modifier)
            k->setVelocityModeParameters (0.3, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Shift: fine (slow velocity mode)
            k->setPopupDisplayEnabled (true, true, nullptr);
            k->textFromValueFunction = [] (double v) { return dbText ((float) v) + " dB"; };
            k->setWantsKeyboardFocus (false);
            k->onTouched = [this, i] { pickSend (i); };
            k->onMenu = [this, i] { showSendMenu (i); };
            k->onDragStart = [this, i]
            {
                sendDragging = true;
                sendGestureStarted = false;

                if (juce::isPositiveAndBelow (i, (int) returns.size()))
                    session.beginParameterGesture (trackIndex, session.getSendParam (trackIndex, returns[(size_t) i].bus));
            };
            k->onDragEnd = [this, i]
            {
                sendDragging = false;

                if (juce::isPositiveAndBelow (i, (int) returns.size()))
                    session.endParameterGesture (trackIndex, session.getSendParam (trackIndex, returns[(size_t) i].bus));

                refresh();
            };
            k->onValueChange = [this, i, k]
            {
                if (! juce::isPositiveAndBelow (i, (int) returns.size()))
                    return;

                if (session.isParamLockedByAutomation (trackIndex, session.getSendParam (trackIndex, returns[(size_t) i].bus)))
                {
                    session.showHint ("This send is automated: edit its lane, or turn on Write and move it while the song plays");

                    if (! sendDragging)
                        refresh();

                    return;
                }

                const bool continues = sendDragging && sendGestureStarted;
                sendGestureStarted = sendDragging;

                if (! session.setSendLevel (trackIndex, returns[(size_t) i].bus, (float) k->getValue(), continues))
                    owner.report (session.getLastError());

                sendInfo[(size_t) i] = session.getSend (trackIndex, returns[(size_t) i].bus);
                repaint (sendsArea);
            };
            return k;
        }

        void pickSend (int i)
        {
            if (juce::isPositiveAndBelow (i, (int) returns.size()))
            {
                owner.pickedSendBus = returns[(size_t) i].bus;
                owner.repaintStrips();
            }
        }

        TrackMenus::Callbacks menuCallbacks()
        {
            juce::Component::SafePointer<Strip> safe (this);
            juce::Component::SafePointer<MixerView> view (&owner);
            TrackMenus::Callbacks callbacks;
            callbacks.horizontal = true;
            callbacks.rename = [safe] { if (safe != nullptr) safe->name.showEditor(); };
            callbacks.moved = [view] (int newIndex) { if (view != nullptr && view->setFocusedTrack) view->setFocusedTrack (newIndex); };
            callbacks.showTrack = [view] (int t) { if (view != nullptr) view->revealTrack (t); };
            callbacks.problem = [view] (const juce::String& m) { if (view != nullptr) view->report (m); };
            return callbacks;
        }

        void showSendMenu (int i)
        {
            if (! juce::isPositiveAndBelow (i, (int) returns.size()))
                return;

            const auto bus = returns[(size_t) i].bus;
            auto menu = TrackMenus::makeSendMenu (session, trackIndex, bus, 0, "Send to " + returns[(size_t) i].name);
            auto& s = session;
            const auto t = trackIndex;
            auto callbacks = menuCallbacks();
            juce::Component::SafePointer<Strip> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [&s, t, bus, callbacks, safe] (int result)
            {
                if (result > 0 && ! TrackMenus::performSendAction (s, t, bus, result, callbacks) && callbacks.problem)
                    callbacks.problem (s.getLastError());

                if (safe != nullptr)
                    safe->refresh();
            });
        }

        void showAllSendsMenu()
        {
            constexpr int stride = 16;
            juce::PopupMenu menu;
            menu.addSectionHeader ("Sends from " + session.getTrackName (trackIndex));

            for (size_t i = 0; i < returns.size(); ++i)
                menu.addSubMenu (returns[i].name + ": " + (sendInfo[i].exists ? dbText (sendInfo[i].levelDb) + " dB" : juce::String ("off")),
                                 TrackMenus::makeSendMenu (session, trackIndex, returns[i].bus, (int) i * stride));

            auto& s = session;
            const auto t = trackIndex;
            const auto buses = returns;
            auto callbacks = menuCallbacks();
            juce::Component::SafePointer<Strip> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [&s, t, buses, callbacks, safe] (int result)
            {
                const auto r = result / stride;

                if (result > 0 && juce::isPositiveAndBelow (r, (int) buses.size())
                     && ! TrackMenus::performSendAction (s, t, buses[(size_t) r].bus, result % stride, callbacks) && callbacks.problem)
                    callbacks.problem (s.getLastError());

                if (safe != nullptr)
                    safe->refresh();
            });
        }

        void paintSends (juce::Graphics& g)
        {
            if (sendsArea.isEmpty())
                return;

            if (isMaster())
                return;   // the space is kept so every fader lines up

            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::captionFontSize - 0.5f, true));
            g.drawText ("SENDS", sendsArea.withHeight (sendsCaption), juce::Justification::centredLeft, false);

            if (kind == Session::TrackKind::fxReturn)
            {
                g.setFont (Theme::font (Theme::captionFontSize));
                g.drawText ("None: it's a return", sendRowBounds (0), juce::Justification::centredLeft, true);
                return;
            }

            const auto hidden = (int) returns.size() - sendRowsShown;

            if (hidden > 0)
                g.drawText ("+" + juce::String (hidden), sendsArea.withHeight (sendsCaption), juce::Justification::centredRight, false);

            const bool focused = owner.getFocusedTrack && owner.getFocusedTrack() == trackIndex;
            const bool wide = getWidth() >= wideStrip;

            for (int i = 0; i < juce::jmin (sendRowsShown, (int) returns.size()); ++i)
            {
                auto row = sendRowBounds (i);
                const auto& info = sendInfo[(size_t) i];

                if (focused && returns[(size_t) i].bus == owner.pickedSendBus)
                {
                    g.setColour (Theme::selection.withAlpha (0.18f));
                    g.fillRoundedRectangle (row.toFloat(), 3.0f);
                }

                if (info.exists && info.preFader)
                {
                    g.setColour (Theme::accent);   // pre-fader: a mark, and in the tooltip and menu
                    g.fillRect (row.getX(), row.getY() + 4, 2, row.getHeight() - 8);
                }

                row.removeFromLeft (22);
                const auto value = ! info.exists ? juce::String ("-") : ! info.enabled ? juce::String ("mute") : dbText (info.levelDb);
                g.setFont (Theme::font (Theme::captionFontSize));

                if (wide)
                {
                    g.setColour (info.exists && info.enabled ? Theme::text : Theme::textDim);
                    g.drawText (value, row.removeFromRight (30), juce::Justification::centredRight, false);
                }

                g.setColour (info.exists ? Theme::text : Theme::textDim);
                g.drawText (returns[(size_t) i].name, row, juce::Justification::centredLeft, true);
            }
        }

        float currentVolume() const     { return isMaster() ? session.getMasterVolumeDb() : session.getTrackVolumeDb (trackIndex); }

        void applyVolume (float db)
        {
            if (isMaster())   session.setMasterVolumeDb (db);
            else if (! session.setTrackVolumeDb (trackIndex, db) && ! faderDragging)
                refresh();   // automated: the lane wins

            if (! volumeLabel.isBeingEdited())
                volumeLabel.setText (dbText (currentVolume()), juce::dontSendNotification);
        }

        juce::Rectangle<int> slotBounds (int slot) const
        {
            return { insertsArea.getX(), insertsArea.getY() + 13 + slot * 21, insertsArea.getWidth(), 19 };
        }

        /** Slot 0 shows the instrument (if any), the rest the effects in order. */
        bool hasInstrument() const      { return instrument.has_value(); }

        juce::String slotLabel (int slot) const
        {
            if (isMaster())
                return slot == 0 ? juce::String ("Mastering chain") : juce::String();

            return slot < inserts.size() ? inserts[slot] : juce::String();
        }

        bool slotIsBypassed (int slot) const
        {
            if (isMaster())
                return ! session.isMasterChainOn();

            const auto effect = slot - (hasInstrument() ? 1 : 0);
            return juce::isPositiveAndBelow (effect, effects.size()) && ! effects.getReference (effect).isEnabled;
        }

        void clickSlot (int slot, bool menu)
        {
            if (isMaster())
            {
                if (slot != 0)
                    return;

                if (! menu)
                {
                    if (owner.showMasterView)
                        owner.showMasterView();

                    return;
                }

                juce::PopupMenu m;
                m.addSectionHeader ("Mastering chain: " + session.getMasterPresetName());
                m.addItem (1, "Open the Master view");
                m.addItem (2, session.isMasterChainOn() ? "Switch it off (Ctrl+Shift+A)" : "Switch it on (Ctrl+Shift+A)");
                juce::Component::SafePointer<Strip> safeStrip (this);

                m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (slotBounds (slot))), [safeStrip] (int result)
                {
                    if (safeStrip == nullptr)
                        return;

                    if (result == 1 && safeStrip->owner.showMasterView)
                        safeStrip->owner.showMasterView();
                    else if (result == 2)
                        safeStrip->session.setMasterChainOn (! safeStrip->session.isMasterChainOn());

                    safeStrip->refresh();
                });
                return;
            }

            const auto effect = slot - (hasInstrument() ? 1 : 0);

            if (slot == 0 && hasInstrument())
            {
                if (! session.openInstrumentWindow (trackIndex))
                    owner.report (session.getLastError());

                return;
            }

            juce::Component::SafePointer<Strip> safe (this);

            if (! menu && juce::isPositiveAndBelow (effect, effects.size()))
            {
                if (! session.openEffectWindow (trackIndex, effect))
                    owner.report (session.getLastError());

                return;
            }

            TrackMenus::showInsertMenu (session, trackIndex, juce::isPositiveAndBelow (effect, effects.size()) ? effect : -1, *this,
                                        [safe] (const juce::String& m) { if (safe != nullptr) safe->owner.report (m); },
                                        [safe] { if (safe != nullptr) safe->refresh(); });
        }

        void showTrackMenu()
        {
            TrackMenus::showTrackMenu (session, trackIndex, menuCallbacks());
        }

        static void refreshRouting (juce::ComboBox& box, juce::StringArray& ids, const juce::Array<Session::RoutingChoice>& choices, const juce::String& current)
        {
            juce::StringArray newIds;

            for (auto& c : choices)
                newIds.add (c.id);

            if (newIds != ids)
            {
                ids = newIds;
                box.clear (juce::dontSendNotification);

                for (int i = 0; i < choices.size(); ++i)
                    box.addItem (choices.getReference (i).name, i + 1);
            }

            box.setSelectedItemIndex (ids.indexOf (current), juce::dontSendNotification);
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Strip)
    };

    //==============================================================================
    MixerView::MixerView (Session& s) : session (s)
    {
        for (auto* b : { &peakButton, &rmsButton })
            b->setWantsKeyboardFocus (false);

        peakButton.setConnectedEdges (juce::Button::ConnectedOnRight);
        rmsButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
        peakButton.setTooltip ("Meters show the peak level (every meter, here and in the arrange)");
        rmsButton.setTooltip ("Meters show the average (RMS) level: closer to how loud it sounds. "
                              "Peaks and clip lights need Peak (a track can't be measured both ways at once)");
        peakButton.setToggleState (session.getMeters().getMode() != te::LevelMeasurer::RMSMode, juce::dontSendNotification);
        rmsButton.setToggleState (session.getMeters().getMode() == te::LevelMeasurer::RMSMode, juce::dontSendNotification);
        peakButton.onClick = [this] { setMeterMode (false); };
        rmsButton.onClick = [this] { setMeterMode (true); };

        ioButton.setTooltip ("Show the input and output choosers on each strip");
        ioButton.setToggleState (true, juce::dontSendNotification);
        ioButton.onClick = [this] { showIO = ! showIO; ioButton.setToggleState (showIO, juce::dontSendNotification); layoutStrips(); };
        narrowButton.setTooltip ("Narrow strips: fit more tracks on screen");
        narrowButton.onClick = [this] { narrow = ! narrow; narrowButton.setToggleState (narrow, juce::dontSendNotification); resized(); };

        sendsButton.setTooltip ("Show each strip's sends to the FX returns (Ctrl+Shift+E)");
        sendsButton.setToggleState (showSends, juce::dontSendNotification);
        sendsButton.onClick = [this] { setShowSends (! showSends); };
        addReturnButton.setTooltip ("Add an FX return: one reverb or delay every track can send to (Ctrl+Shift+R adds a reverb)");
        addReturnButton.onClick = [this] { showAddReturnMenu(); };
        addBusButton.setTooltip ("Add a bus: tracks play into it, so one fader turns a group up or down (Ctrl+Shift+B; Ctrl+Shift+G puts the highlighted track in a new one)");
        addBusButton.onClick = [this] { showAddBusMenu(); };

        for (auto* b : { &peakButton, &rmsButton, &ioButton, &narrowButton, &sendsButton, &addReturnButton, &addBusButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }

        viewport.setViewedComponent (&content, false);
        viewport.setWantsKeyboardFocus (false);   // or it takes the arrow keys for scrolling
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (Theme::scrollBarSize);
        addAndMakeVisible (viewport);

        session.addListener (this);
        rebuildIfNeeded();
        startTimerHz (10);
    }

    MixerView::~MixerView()
    {
        session.removeListener (this);
    }

    void MixerView::report (const juce::String& message)
    {
        if (onProblem && message.isNotEmpty())
            onProblem ("Mix", message);
    }

    void MixerView::repaintStrips()
    {
        for (auto* s : strips)
            s->repaint();
    }

    void MixerView::setMeterMode (bool rms)
    {
        peakButton.setToggleState (! rms, juce::dontSendNotification);
        rmsButton.setToggleState (rms, juce::dontSendNotification);
        session.getMeters().setMode (rms ? te::LevelMeasurer::RMSMode : te::LevelMeasurer::peakMode);
    }

    void MixerView::rebuildIfNeeded()
    {
        const auto ids = session.getTrackIDs();
        const auto returnsNow = (int) session.getReturns().size();

        if (returnsNow != numReturns)
        {
            numReturns = returnsNow;   // every strip makes room for the same number of sends
            builtFor.clear();
        }

        if (ids != builtFor || (masterStrip == nullptr) != (session.getEdit() == nullptr) || strips.size() != ids.size())
        {
            builtFor = ids;
            strips.clear();
            masterStrip.reset();

            if (session.getEdit() != nullptr)
            {
                for (int i = 0; i < ids.size(); ++i)
                    content.addAndMakeVisible (strips.add (new Strip (*this, i)));

                masterStrip = std::make_unique<Strip> (*this, -1);
                addAndMakeVisible (*masterStrip);   // stays on the right, outside the scrolling strips
            }

            layoutStrips();
            return;
        }

        for (auto* s : strips)
            s->refresh();

        if (masterStrip != nullptr)
            masterStrip->refresh();
    }

    void MixerView::timerCallback()
    {
        if (! isShowing())
            return;

        // Some changes (a volume set in the arrange, a rename) aren't announced: catch up twice a second
        if (++ticks % 5 == 0)
            rebuildIfNeeded();

        for (auto* s : strips)
            s->refreshMeters();

        if (masterStrip != nullptr)
            masterStrip->refreshMeters();
    }

    void MixerView::layoutStrips()
    {
        const auto w = narrow ? narrowStrip : wideStrip;
        const auto stripsWidth = 10 + strips.size() * (w + stripGap) + 6;
        const bool scrolls = stripsWidth > viewport.getWidth();   // decided before sizing, so the strips fit above the scrollbar
        const auto h = juce::jmax (210, viewport.getHeight() - (scrolls ? Theme::scrollBarSize : 0) - 20);
        rows = computeRows (h);
        int x = 10;

        for (auto* s : strips)
        {
            s->setBounds (x, 10, w, h);
            x += w + stripGap;
        }

        content.setSize (juce::jmax (x + 6, viewport.getMaximumVisibleWidth()), h + 20);

        if (masterStrip != nullptr)
            masterStrip->setBounds (getWidth() - w - 10, headerArea.getBottom() + 10, w, h);   // as tall as the strips: faders line up

        for (auto* s : strips)
            s->resized();

        if (masterStrip != nullptr)
            masterStrip->resized();
    }

    MixerView::Rows MixerView::computeRows (int stripHeight) const
    {
        // In order of importance: a send (the only place to turn one up, while inserts are also in the
        // arrange and the Devices panel), 2 insert slots, a second send, 4 slots, the other sends, inputs/outputs
        constexpr int fixedRows = 34 + 50 + 46 + 20, minFader = 80;
        auto room = stripHeight - fixedRows - minFader;
        const auto wanted = showSends ? numReturns : 0;
        const auto take = [&room] (int amount) { if (room < amount) return false; room -= amount; return true; };
        Rows fit;

        if (wanted > 0 && take (sendsCaption + sectionGap + sendRowHeight))
            fit.sends = 1;

        if (take (62))
            fit.slots = 2;

        if (fit.sends == 1 && wanted > 1 && take (sendRowHeight))
            fit.sends = 2;

        if (fit.slots == 2 && take (42))
            fit.slots = insertSlots;

        while (fit.sends > 0 && fit.sends < wanted && take (sendRowHeight))
            ++fit.sends;

        fit.io = showIO && UiMode::isPro() && take (51);

        if (! UiMode::isPro())
            fit.slots = 0;   // Easy view: the inserts are in the arrange and the Devices panel

        return fit;
    }

    void MixerView::setShowSends (bool shouldShow)
    {
        showSends = shouldShow;
        sendsButton.setToggleState (showSends, juce::dontSendNotification);
        layoutStrips();
    }

    void MixerView::revealTrack (int trackIndex)
    {
        rebuildIfNeeded();   // a track just added has its strip now

        if (setFocusedTrack)
            setFocusedTrack (trackIndex);

        if (auto* s = strips[trackIndex])
            viewport.setViewPosition (juce::jlimit (0, juce::jmax (0, content.getWidth() - viewport.getViewWidth()),
                                                    s->getX() + s->getWidth() / 2 - viewport.getViewWidth() / 2), 0);

        repaintStrips();
    }

    void MixerView::showAddReturnMenu()
    {
        juce::Component::SafePointer<MixerView> safe (this);
        TrackMenus::makeAddReturnMenu (0).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addReturnButton), [safe] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;

            const auto t = TrackMenus::performAddReturn (safe->session, result);

            if (t >= 0)
                safe->revealTrack (t);
            else
                safe->report (safe->session.getLastError());
        });
    }

    void MixerView::showAddBusMenu()
    {
        const auto focused = getFocusedTrack ? getFocusedTrack() : -1;
        const bool canGroup = juce::isPositiveAndBelow (focused, session.getNumAudioTracks()) && session.getTrackKind (focused) != Session::TrackKind::fxReturn;
        juce::PopupMenu menu;
        menu.addItem (1, "Empty bus (Ctrl+Shift+B)");
        menu.addItem (2, canGroup ? "Play \"" + session.getTrackName (focused) + "\" into a new bus (Ctrl+Shift+G)"
                                  : juce::String ("Play the highlighted track into a new bus (Ctrl+Shift+G)"), canGroup);

        juce::Component::SafePointer<MixerView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addBusButton), [safe, focused] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;

            const auto t = result == 1 ? safe->session.addBusTrack() : safe->session.routeTracksToNewBus ({ focused });

            if (t >= 0)
                safe->revealTrack (t);
            else
                safe->report (safe->session.getLastError());
        });
    }

    void MixerView::moveFocus (int direction)
    {
        if (strips.isEmpty() || ! getFocusedTrack || ! setFocusedTrack)
            return;

        setFocusedTrack (juce::jlimit (0, strips.size() - 1, getFocusedTrack() + direction));
        const auto focused = getFocusedTrack();

        if (auto* s = strips[focused])
            viewport.setViewPosition (juce::jlimit (0, juce::jmax (0, content.getWidth() - viewport.getViewWidth()),
                                                    s->getX() + s->getWidth() / 2 - viewport.getViewWidth() / 2), 0);

        repaintStrips();
    }

    bool MixerView::handleKey (const juce::KeyPress& key)
    {
        if (! hasEditFocus() || ! getFocusedTrack)
            return false;

        auto* strip = strips[getFocusedTrack()];

        if (strip == nullptr)
            return false;

        // [ and ] by the character typed: AltGr layouts (German, French) report Ctrl+Alt and another key code
        const auto typed = key.getTextCharacter();

        if (typed == '[' || typed == ']' || typed == '{' || typed == '}')
            return strip->nudgeSend ((typed == ']' || typed == '}' ? 1.0f : -1.0f) * (key.getModifiers().isShiftDown() ? 0.1f : 1.0f));

        if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown())
            return false;

        const auto code = key.getKeyCode();

        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            strip->nudgeVolume ((code == juce::KeyPress::upKey ? 1.0f : -1.0f) * (key.getModifiers().isShiftDown() ? 0.1f : 1.0f));
            return true;
        }

        const auto t = strip->getTrackIndex();

        if (code == '[' || code == ']')
            return strip->nudgeSend ((code == ']' ? 1.0f : -1.0f) * (key.getModifiers().isShiftDown() ? 0.1f : 1.0f));

        if (key == juce::KeyPress ('m'))    { session.setTrackMuted (t, ! session.isTrackMuted (t)); strip->refresh(); return true; }
        if (key == juce::KeyPress ('s'))    { session.setTrackSoloed (t, ! session.isTrackSoloed (t)); strip->refresh(); return true; }

        return false;
    }

    //==============================================================================
    void MixerView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::background);
        g.setColour (Theme::bar);
        g.fillRect (headerArea);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (headerArea.getBottom() - 1, 0.0f, (float) getWidth());
        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::baseFontSize + 0.5f, true));
        g.drawText ("Mix", headerArea.withTrimmedLeft (12).withWidth (40), juce::Justification::centredLeft, false);
        g.setFont (Theme::font (Theme::smallFontSize));
        g.drawText ("Meters", peakButton.getBounds().withX (peakButton.getX() - 50).withWidth (46), juce::Justification::centredRight, false);

        if (session.getEdit() == nullptr)
        {
            g.setColour (Theme::textDim);
            g.drawFittedText ("Open or make a song to mix it.", getLocalBounds().withTrimmedTop (headerHeight), juce::Justification::centred, 2);
        }
        else if (masterStrip != nullptr)
        {
            // The master sits apart, on the right
            g.setColour (Theme::laneSeparator);
            g.drawVerticalLine (masterStrip->getX() - 6, (float) masterStrip->getY(), (float) masterStrip->getBottom());
        }
    }

    void MixerView::paintOverChildren (juce::Graphics& g)
    {
        Planned::paintChips (g, *this);
    }

    void MixerView::resized()
    {
        auto r = getLocalBounds();
        headerArea = r.removeFromTop (headerHeight);

        auto bar = headerArea.reduced (10, 5).withTrimmedLeft (110);
        const auto place = [&] (juce::Component& c, int w, int gapAfter)
        {
            c.setBounds (bar.removeFromLeft (w));
            bar.removeFromLeft (gapAfter);
        };

        place (peakButton, peakButton.getIdealWidth (24), 0);
        place (rmsButton, rmsButton.getIdealWidth (24), 16);
        ioButton.setVisible (UiMode::isPro());   // Easy view: no inputs/outputs rows

        if (UiMode::isPro())
            place (ioButton, ioButton.getIdealWidth (24), 6);
        place (narrowButton, narrowButton.getIdealWidth (24), 16);
        place (sendsButton, sendsButton.getIdealWidth (24), 16);
        place (addReturnButton, addReturnButton.getIdealWidth (24), 6);
        place (addBusButton, addBusButton.getIdealWidth (24), 0);

        const auto w = narrow ? narrowStrip : wideStrip;
        viewport.setBounds (r.withTrimmedRight (w + 22));
        layoutStrips();
    }

    void MixerView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        // Strips scroll sideways (a vertical wheel too: there is nothing to scroll up and down)
        const auto amount = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        viewport.setViewPosition (juce::jlimit (0, juce::jmax (0, content.getWidth() - viewport.getViewWidth()),
                                                viewport.getViewPositionX() - juce::roundToInt (amount * 240.0f)), 0);
        juce::ignoreUnused (e);
    }
}
