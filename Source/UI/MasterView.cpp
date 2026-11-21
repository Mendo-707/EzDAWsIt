/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "MasterView.h"
#include "Theme.h"
#include "CommandIDs.h"
#include "../Engine/AudioEngine.h"
#include "../Engine/Mastering.h"
#include "../Engine/Dsp.h"

namespace ez
{
    namespace
    {
        using Chain = MasteringChainPlugin;
        constexpr int gap = 10;

        juce::String oneDecimal (float v)   { return juce::String (v, 1); }

        /** Cards draw themselves without a shadow (it would be clipped); their parent draws it. */
        void drawPanel (juce::Graphics& g, juce::Rectangle<int> r)
        {
            drawCard (g, r.toFloat(), false);
        }

        void drawTitle (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r)
        {
            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::titleFontSize, Theme::Weight::semibold));
            g.drawText (text, r, juce::Justification::centredLeft, false);
        }

        /** A horizontal reduction bar: how hard a part is working (dB below 0, right to left). */
        void drawReduction (juce::Graphics& g, juce::Rectangle<int> r, float db, float rangeDb = 12.0f)
        {
            const auto bar = r.toFloat();
            drawWell (g, bar, 2.0f);
            const auto amount = juce::jlimit (0.0f, 1.0f, -db / rangeDb);

            if (amount > 0.0f)
            {
                const auto fill = bar.withLeft (bar.getRight() - bar.getWidth() * amount).reduced (0.0f, 1.0f);
                g.setGradientFill (juce::ColourGradient::horizontal (Theme::meterMid.withAlpha (0.6f), fill.getX(), Theme::meterMid, fill.getRight()));
                g.fillRoundedRectangle (fill, 1.5f);
            }
        }

        /** The frequency axis of the EQ graph: 20 Hz to 20 kHz, log. */
        float xForHz (juce::Rectangle<float> area, float hz)
        {
            return area.getX() + area.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f);
        }

        float biquadDb (const dsp::Biquad& f, double sampleRate, float hz)
        {
            const auto w = 2.0 * juce::MathConstants<double>::pi * hz / sampleRate;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
            const auto num = (double) f.b0 + (double) f.b1 * z1 + (double) f.b2 * z2;
            const auto den = 1.0 + (double) f.a1 * z1 + (double) f.a2 * z2;
            return (float) (20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (num / den))));
        }
    }

    //==============================================================================
    /** A labelled knob for one control of the chain: drag = one undo step, double-click = default,
        right-click = menu. */
    class MasterView::Knob : public juce::Component
    {
    public:
        Knob (MasterView& o, int controlIndex, const juce::String& labelText) : owner (o), control (controlIndex), label (labelText)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setVelocityModeParameters (0.3, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Shift: fine
            slider.setPopupDisplayEnabled (true, true, nullptr);
            slider.setWantsKeyboardFocus (false);
            slider.onDragStart = [this] { owner.session.beginUndoStep(); dragging = true; };
            slider.onDragEnd = [this] { dragging = false; owner.refresh(); };
            slider.onValueChange = [this]
            {
                owner.report (owner.session.setMasterControl (control, (float) slider.getValue(), dragging), "Could not change it");
                repaint();
            };
            slider.onMenu = [this] { showMenu(); };
            addAndMakeVisible (slider);
        }

        void refresh (Chain* chain)
        {
            auto* p = chain != nullptr ? chain->getControl (control) : nullptr;

            if (p == nullptr)
                return;

            if (! rangeSet)
            {
                const auto range = p->getValueRange();
                slider.setRange (range.getStart(), range.getEnd(), 0.0);
                slider.setDoubleClickReturnValue (true, p->getDefaultValue().value_or (range.getStart()));
                // Through the session, never a parameter pointer: the view outlives songs (and their chains)
                slider.textFromValueFunction = [this] (double v)
                {
                    auto* current = owner.session.getMasteringChain();
                    auto* param = current != nullptr ? current->getControl (control) : nullptr;
                    return param != nullptr ? param->valueToString ((float) v) : juce::String (v, 1);
                };
                rangeSet = true;
            }

            slider.setTooltip (label + ": " + owner.session.formatMasterControl (control) + " (double-click: default, right-click: menu)");

            if (! dragging && std::abs (slider.getValue() - owner.session.getMasterControl (control)) > 1.0e-4)
            {
                slider.setValue (owner.session.getMasterControl (control), juce::dontSendNotification);
                repaint();
            }
        }

        void setActive (bool active)    { slider.setAlpha (active ? 1.0f : 0.45f); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            auto text = r.removeFromBottom (28);
            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::smallFontSize));
            g.drawText (label, text.removeFromTop (14), juce::Justification::centred, true);
            g.setColour (Theme::textDim);
            g.drawText (owner.session.formatMasterControl (control), text, juce::Justification::centred, true);
        }

        void resized() override         { slider.setBounds (getLocalBounds().withTrimmedBottom (28).reduced (2)); }

    private:
        MasterView& owner;
        const int control;
        const juce::String label;
        MenuSlider slider;
        bool dragging = false, rangeSet = false;

        void showMenu()
        {
            juce::PopupMenu menu;
            menu.addSectionHeader (label + ": " + owner.session.formatMasterControl (control));
            menu.addItem (1, "Back to the default");
            juce::Component::SafePointer<Knob> safe (this);

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider), [safe] (int result)
            {
                if (safe == nullptr || result != 1)
                    return;

                if (auto* chain = safe->owner.session.getMasteringChain())
                    if (auto* p = chain->getControl (safe->control))
                        safe->owner.report (safe->owner.session.setMasterControl (safe->control, p->getDefaultValue().value_or (0.0f)), "Could not reset it");

                safe->owner.refresh();
            });
        }
    };

    //==============================================================================
    /** One module of the chain: title with its on switch, then its knobs and readouts. */
    class MasterView::Card : public juce::Component
    {
    public:
        Card (MasterView& o, int moduleIndex, const juce::String& title, int width)
            : preferredWidth (width), owner (o), module (moduleIndex)
        {
            power.setTooltip (title + " on / off: " + (moduleIndex >= 0 ? Chain::moduleHint (moduleIndex) : juce::String()));
            power.onClick = [this]
            {
                if (module >= 0)
                    owner.report (owner.session.setMasterControl (Chain::firstModuleSwitch + module, isOn() ? 0.0f : 1.0f), "Could not switch it");
            };
            power.setWantsKeyboardFocus (false);

            if (module >= 0)
                addAndMakeVisible (power);

            name = title;
        }

        const int preferredWidth;
        juce::OwnedArray<Knob> knobs;
        std::function<void (juce::Graphics&, juce::Rectangle<int>)> drawExtra;   // a curve or meters above the knobs
        int extraHeight = 0;
        juce::Rectangle<int> extraArea;

        bool isOn() const   { return module < 0 || owner.session.getMasterControl (Chain::firstModuleSwitch + module) >= 0.5f; }

        Knob& addKnob (int control, const juce::String& label)
        {
            auto* k = knobs.add (new Knob (owner, control, label));
            addAndMakeVisible (k);
            return *k;
        }

        void refresh (Chain* chain)
        {
            power.setToggleState (isOn(), juce::dontSendNotification);
            power.setIconColour (isOn() ? Theme::displayText : Theme::textDim);

            for (auto* k : knobs)
            {
                k->refresh (chain);
                k->setActive (isOn() && owner.session.isMasterChainOn());
            }

            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const bool active = isOn() && owner.session.isMasterChainOn();
            drawCard (g, getLocalBounds().toFloat(), false, false);
            auto title = getLocalBounds().reduced (10, 0).removeFromTop (34);

            if (module >= 0)
                title.removeFromLeft (26);

            g.setColour (active ? Theme::text : Theme::textDim);
            g.setFont (Theme::font (Theme::baseFontSize, Theme::Weight::semibold));
            g.drawText (name, title, juce::Justification::centredLeft, true);

            // A light in the corner: this module is working on the sound
            if (module >= 0)
            {
                g.setColour (active ? Theme::success : Theme::textFaint.withAlpha (0.6f));
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) getWidth() - 14.0f, 17.0f }));
            }

            g.setColour (Theme::hairline);
            g.drawHorizontalLine (34, 10.0f, (float) getWidth() - 10.0f);

            if (drawExtra && ! extraArea.isEmpty())
                drawExtra (g, extraArea);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 6);
            auto title = r.removeFromTop (22);

            if (module >= 0)
                power.setBounds (title.removeFromLeft (22).withSizeKeepingCentre (22, 22));

            r.removeFromTop (6);
            extraArea = extraHeight > 0 ? r.removeFromTop (extraHeight) : juce::Rectangle<int>();

            if (extraHeight > 0)
                r.removeFromTop (6);

            layoutRest (r);
        }

        std::function<void (juce::Rectangle<int>)> layoutRest = [this] (juce::Rectangle<int> r)
        {
            if (knobs.isEmpty())
                return;

            const auto columns = juce::jmin (knobs.size(), 4);
            const auto rows = (knobs.size() + columns - 1) / columns;
            const auto w = r.getWidth() / columns, h = juce::jmin (84, r.getHeight() / juce::jmax (1, rows));

            for (int i = 0; i < knobs.size(); ++i)
                knobs[i]->setBounds (r.getX() + (i % columns) * w, r.getY() + (i / columns) * h, w, h);
        };

    private:
        MasterView& owner;
        const int module;
        juce::String name;
        IconButton power { "On", Icon::power, {}, IconButton::Style::ghost };
    };

    //==============================================================================
    /** EQ → Multiband → Stereo → Limiter → Dither, scrolling sideways when narrow. */
    class MasterView::ChainRow : public juce::Component
    {
    public:
        explicit ChainRow (MasterView& o) : owner (o)
        {
            // EQ: the curve of the four bands over 20 Hz .. 20 kHz
            eq = cards.add (new Card (owner, Chain::eqModule, "EQ", 300));
            eq->addKnob (Chain::lowShelf, "Low");
            eq->addKnob (Chain::lowMids, "Low mids");
            eq->addKnob (Chain::presence, "Presence");
            eq->addKnob (Chain::air, "Air");
            eq->extraHeight = 64;
            eq->drawExtra = [this] (juce::Graphics& g, juce::Rectangle<int> r) { drawEqCurve (g, r); };

            glue = cards.add (new Card (owner, Chain::glueModule, "Multiband", 230));
            glue->addKnob (Chain::glueLow, "Low");
            glue->addKnob (Chain::glueMid, "Mid");
            glue->addKnob (Chain::glueHigh, "High");
            glue->extraHeight = 64;
            glue->drawExtra = [this] (juce::Graphics& g, juce::Rectangle<int> r) { drawBands (g, r); };

            stereo = cards.add (new Card (owner, Chain::stereoModule, "Stereo", 170));
            stereo->addKnob (Chain::width, "Width");
            monoBass.setTooltip ("Bass in the middle: below 120 Hz both speakers play the same (tighter low end, safe for clubs and vinyl)");
            monoBass.onClick = [this] { owner.report (owner.session.setMasterControl (Chain::monoBass, monoBass.getToggleState() ? 1.0f : 0.0f), "Could not switch it"); };
            monoBass.setWantsKeyboardFocus (false);
            stereo->addAndMakeVisible (monoBass);
            stereo->layoutRest = [this] (juce::Rectangle<int> r)
            {
                monoBass.setBounds (r.removeFromBottom (24));
                stereo->knobs[0]->setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 90), juce::jmin (r.getHeight(), 92)));
            };

            limiter = cards.add (new Card (owner, Chain::limiterModule, "Limiter", 220));
            limiter->addKnob (Chain::gain, "Gain");
            limiter->addKnob (Chain::ceiling, "Ceiling");
            limiter->extraHeight = 0;
            limiter->layoutRest = [this] (juce::Rectangle<int> r)
            {
                limiterMeter = r.removeFromBottom (18);
                const auto w = r.getWidth() / 2;
                limiter->knobs[0]->setBounds (r.removeFromLeft (w));
                limiter->knobs[1]->setBounds (r);
            };
            limiter->drawExtra = nullptr;

            dither = cards.add (new Card (owner, -1, "Dither", 170));

            for (auto* b : { &ditherAuto, &ditherOff })
            {
                b->setRadioGroupId (77);
                b->setClickingTogglesState (true);
                b->setWantsKeyboardFocus (false);
                dither->addAndMakeVisible (b);
            }

            ditherAuto.setTooltip ("Adds a little noise to 16-bit exports (CD): hides the steps in quiet parts. 24-bit and MP3 don't need it");
            ditherOff.setTooltip ("Never dither (Export can still switch it on for one file)");
            ditherAuto.onClick = [this] { if (ditherAuto.getToggleState()) owner.session.setMasterDither (true); };
            ditherOff.onClick = [this] { if (ditherOff.getToggleState()) owner.session.setMasterDither (false); };
            dither->layoutRest = [this] (juce::Rectangle<int> r)
            {
                ditherAuto.setBounds (r.removeFromTop (24));
                r.removeFromTop (4);
                ditherOff.setBounds (r.removeFromTop (24));
            };

            for (auto* c : cards)
                addAndMakeVisible (c);
        }

        int getPreferredWidth() const
        {
            int w = 0;

            for (auto* c : cards)
                w += c->preferredWidth + arrowWidth;

            return w - arrowWidth;
        }

        void refresh (Chain* chain)
        {
            for (auto* c : cards)
                c->refresh (chain);

            monoBass.setToggleState (owner.session.getMasterControl (Chain::monoBass) >= 0.5f, juce::dontSendNotification);
            const bool ditherOn = owner.session.getMasterDither();
            ditherAuto.setToggleState (ditherOn, juce::dontSendNotification);
            ditherOff.setToggleState (! ditherOn, juce::dontSendNotification);
        }

        void refreshMeters()
        {
            glue->repaint();
            limiter->repaint (limiterMeter);
        }

        void paint (juce::Graphics& g) override
        {
            for (auto* c : cards)
                Theme::drawShadow (g, c->getBounds().toFloat(), (float) Theme::cardRadius, 1);

            // Connectors between the cards: the order the sound goes through
            for (int i = 0; i + 1 < cards.size(); ++i)
            {
                const auto x1 = (float) cards[i]->getRight() + 2.0f, x2 = (float) cards[i + 1]->getX() - 2.0f, y = (float) getHeight() * 0.5f;
                g.setColour (Theme::border);
                g.drawLine (x1, y, x2 - 3.0f, y, 1.5f);
                juce::Path arrow;
                arrow.addTriangle (x2 - 6.0f, y - 4.0f, x2 - 6.0f, y + 4.0f, x2, y);
                g.setColour (Theme::textDim);
                g.fillPath (arrow);
            }
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (auto* chain = owner.session.getMasteringChain(); chain != nullptr && ! limiterMeter.isEmpty())
            {
                auto r = limiterMeter.translated (limiter->getX(), limiter->getY());
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (Theme::captionFontSize + 0.5f));
                g.drawText ("Reduction", r.removeFromLeft (62), juce::Justification::centredLeft, false);
                // Switched off, Tracktion stops calling the chain: its last reading would stay up
                const auto db = owner.session.isMasterChainOn() && limiter->isOn() ? chain->getLimiterReductionDb() : 0.0f;
                g.drawText (oneDecimal (db), r.removeFromRight (34), juce::Justification::centredRight, false);
                drawReduction (g, r.reduced (0, 6), db);
            }
        }

        /** The narrowest the row gets before it scrolls: cards shrink to 80 % of their preferred widths. */
        int getMinimumWidth() const     { return juce::roundToInt ((float) (getPreferredWidth() - arrowWidth * (cards.size() - 1)) * 0.8f) + arrowWidth * (cards.size() - 1); }

        void resized() override
        {
            // the cards share the width in proportion to their preferred widths: all five always show
            auto r = getLocalBounds();
            const auto preferredCards = (float) (getPreferredWidth() - arrowWidth * (cards.size() - 1));
            const auto scale = (float) (getWidth() - arrowWidth * (cards.size() - 1)) / juce::jmax (1.0f, preferredCards);

            for (int i = 0; i < cards.size(); ++i)
            {
                auto* c = cards[i];
                c->setBounds (i + 1 == cards.size() ? r : r.removeFromLeft (juce::roundToInt ((float) c->preferredWidth * scale)));
                r.removeFromLeft (arrowWidth);
            }
        }

    private:
        static constexpr int arrowWidth = 18;
        MasterView& owner;
        juce::OwnedArray<Card> cards;
        Card* eq = nullptr;
        Card* glue = nullptr;
        Card* stereo = nullptr;
        Card* limiter = nullptr;
        Card* dither = nullptr;
        juce::ToggleButton monoBass { "Mono below 120 Hz" };
        juce::ToggleButton ditherAuto { "Auto (16-bit exports)" }, ditherOff { "Off" };
        juce::Rectangle<int> limiterMeter;

        void drawEqCurve (juce::Graphics& g, juce::Rectangle<int> r)
        {
            drawWell (g, r.toFloat(), 4.0f);

            const double sr = 48000.0;
            dsp::Biquad low, lowMids, presence, air;
            low.set (dsp::Biquad::Type::lowShelf, sr, 100.0f, 0.7f, owner.session.getMasterControl (Chain::lowShelf));
            lowMids.set (dsp::Biquad::Type::peak, sr, 300.0f, 1.0f, owner.session.getMasterControl (Chain::lowMids));
            presence.set (dsp::Biquad::Type::peak, sr, 3000.0f, 0.8f, owner.session.getMasterControl (Chain::presence));
            air.set (dsp::Biquad::Type::highShelf, sr, 12000.0f, 0.7f, owner.session.getMasterControl (Chain::air));

            const auto area = r.toFloat().reduced (3.0f);
            const auto yFor = [&area] (float db) { return area.getCentreY() - db / 8.0f * area.getHeight() * 0.5f; };   // +-8 dB
            const auto totalDb = [&] (float hz)
            {
                return biquadDb (low, sr, hz) + biquadDb (lowMids, sr, hz) + biquadDb (presence, sr, hz) + biquadDb (air, sr, hz);
            };

            // Grid: decades and the 6 dB lines, faint; 0 dB a step stronger
            g.setFont (Theme::font (Theme::captionFontSize));

            for (auto hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            {
                const auto x = xForHz (area, hz);
                const bool decade = hz == 100.0f || hz == 1000.0f || hz == 10000.0f;
                g.setColour (Theme::hairline.withAlpha (decade ? 1.0f : 0.5f));
                g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

                if (decade && area.getHeight() > 40.0f)
                {
                    g.setColour (Theme::textFaint);
                    g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                                juce::Rectangle<float> (x + 3.0f, area.getBottom() - 12.0f, 30.0f, 11.0f), juce::Justification::centredLeft, false);
                }
            }

            for (auto db : { -6.0f, 6.0f })
            {
                g.setColour (Theme::hairline.withAlpha (0.5f));
                g.drawHorizontalLine (juce::roundToInt (yFor (db)), area.getX(), area.getRight());
            }

            g.setColour (Theme::border);
            g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());

            juce::Path curve;

            for (int x = 0; x <= (int) area.getWidth(); x += 2)
            {
                const auto hz = 20.0f * std::pow (1000.0f, (float) x / area.getWidth());   // 20 Hz .. 20 kHz
                const juce::Point<float> p (area.getX() + (float) x, juce::jlimit (area.getY(), area.getBottom(), yFor (totalDb (hz))));

                if (x == 0)   curve.startNewSubPath (p);
                else          curve.lineTo (p);
            }

            const bool active = owner.session.isMasterChainOn() && eq->isOn();
            const auto colour = active ? Theme::accent : Theme::textDim;

            // The area between the curve and 0 dB, tinted: boosts and cuts read at a glance
            juce::Path fill (curve);
            fill.lineTo (area.getRight(), area.getCentreY());
            fill.lineTo (area.getX(), area.getCentreY());
            fill.closeSubPath();
            g.setColour (colour.withAlpha (active ? 0.18f : 0.08f));
            g.fillPath (fill);

            g.setColour (colour.withAlpha (active ? 1.0f : 0.6f));
            g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // A dot on each band, where its knob acts
            for (auto hz : { 100.0f, 300.0f, 3000.0f, 12000.0f })
            {
                const juce::Point<float> p (xForHz (area, hz), juce::jlimit (area.getY(), area.getBottom(), yFor (totalDb (hz))));
                g.setColour (Theme::display);
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));
                g.setColour (colour);
                g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p), 1.5f);
            }
        }

        void drawBands (juce::Graphics& g, juce::Rectangle<int> r)
        {
            auto* chain = owner.session.getMasteringChain();
            const char* names[] = { "Low", "Mid", "High" };
            const auto w = (r.getWidth() - 12) / 3;

            for (int b = 0; b < 3; ++b)
            {
                auto column = juce::Rectangle<int> (r.getX() + b * (w + 6), r.getY(), w, r.getHeight());
                const auto db = chain != nullptr && owner.session.isMasterChainOn() && glue->isOn() ? chain->getBandReductionDb (b) : 0.0f;
                auto label = column.removeFromBottom (14);
                drawWell (g, column.toFloat(), 4.0f);
                const auto amount = juce::jlimit (0.0f, 1.0f, -db / 12.0f);

                // a scale, so an idle band reads as a meter at rest rather than an empty box
                g.setFont (Theme::font (Theme::captionFontSize - 1.0f));

                for (int tick = 1; tick <= 3; ++tick)
                {
                    const auto y = (float) column.getY() + 2.0f + ((float) column.getHeight() - 4.0f) * (float) tick / 4.0f;
                    g.setColour (Theme::hairline);
                    g.drawHorizontalLine (juce::roundToInt (y), (float) column.getX() + 4.0f, (float) column.getRight() - 4.0f);

                    if (column.getHeight() > 72 && tick < 3)
                    {
                        g.setColour (Theme::textFaint);
                        g.drawText ("-" + juce::String (tick * 3), juce::Rectangle<float> ((float) column.getX() + 4.0f, y - 10.0f, 20.0f, 9.0f),
                                    juce::Justification::centredLeft, false);
                    }
                }

                if (amount > 0.0f)
                {
                    // Gain reduction hangs from the top, as on a hardware compressor
                    const auto fill = column.toFloat().reduced (2.0f).withHeight (juce::jmax (2.0f, ((float) column.getHeight() - 4.0f) * amount));
                    g.setGradientFill (juce::ColourGradient::vertical (Theme::meterMid, fill.getY(), Theme::meterMid.withAlpha (0.55f), fill.getBottom()));
                    g.fillRoundedRectangle (fill, 2.0f);
                }
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (Theme::captionFontSize + 0.5f));
                g.drawText (juce::String (names[b]) + " " + oneDecimal (db), label, juce::Justification::centred, false);
            }
        }
    };

    //==============================================================================
    class MasterView::LoudnessPanel : public juce::Component
    {
    public:
        explicit LoudnessPanel (MasterView& o) : owner (o)
        {
            const auto& targets = Session::getLoudnessTargets();

            for (int i = 0; i < targets.size(); ++i)
            {
                const auto& t = targets.getReference (i);
                auto* b = targetButtons.add (new juce::TextButton (t.name));
                b->setClickingTogglesState (true);
                b->setRadioGroupId (88);
                b->setWantsKeyboardFocus (false);
                b->setColour (juce::TextButton::buttonOnColourId, Theme::activeFill);
                b->setTooltip (t.name + ": " + oneDecimal (t.lufs) + " LUFS, true peak under " + oneDecimal (t.ceilingDb)
                               + " dB (the export's loudness target too)");
                b->onClick = [this, b, i] { if (b->getToggleState()) owner.session.setMasterTarget (i); };
                addAndMakeVisible (b);
            }

            matchButton.setTooltip ("Sets the limiter so the song lands on the target (play the loudest part first, so it's measured)");
            matchButton.onClick = [this] { owner.report (owner.session.matchMasterTarget(), "Can't match the target yet"); };
            resetButton.setTooltip ("Start measuring again (Ctrl+Shift+M)");
            resetButton.onClick = [this] { owner.session.getMeters().resetMain(); history.clear(); repaint(); };

            for (auto* b : { &matchButton, &resetButton })
            {
                b->setWantsKeyboardFocus (false);
                addAndMakeVisible (b);
            }
        }

        void refresh()
        {
            const auto target = owner.session.getMasterTarget();

            for (int i = 0; i < targetButtons.size(); ++i)
                targetButtons[i]->setToggleState (i == target, juce::dontSendNotification);

            repaint();
        }

        /** Every 100 ms while playing: the short-term loudness at the song position (the graph). */
        void sample()
        {
            if (! owner.session.isPlaying())
                return;

            const auto l = owner.session.getMasterLoudness();

            if (! l.shortTermValid || owner.session.getEdit() == nullptr)
                return;

            const auto time = owner.session.getEdit()->getTransport().getPosition().inSeconds();

            // Played again (or jumped back): the newer reading replaces the old ones there
            while (! history.empty() && history.back().x >= (float) time)
                history.pop_back();

            history.push_back ({ (float) time, l.shortTermLufs });
        }

        void clearHistory()     { history.clear(); repaint(); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            drawPanel (g, r);
            r = r.reduced (12);

            auto title = r.removeFromTop (22);
            drawTitle (g, "Loudness", title.removeFromLeft (90));
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize));
            g.drawText ("Where will this be played?", title, juce::Justification::centredLeft, true);

            const auto readings = owner.session.getMasterLoudness();
            const auto& target = Session::getLoudnessTargets().getReference (owner.session.getMasterTarget());
            const auto peakTooHot = readings.truePeakDb > target.ceilingDb + 0.05f && readings.truePeakDb > -99.0f;

            // The four big numbers
            auto boxes = readoutArea;
            const auto w = (boxes.getWidth() - 3 * 8) / 4;
            const auto lufsText = [] (float v, bool valid) { return valid && v > -99.0f ? oneDecimal (v) : juce::String ("-"); };
            drawReadout (g, boxes.removeFromLeft (w), "INTEGRATED", lufsText (readings.integratedLufs, readings.integratedValid), "LUFS", true);
            boxes.removeFromLeft (8);
            drawReadout (g, boxes.removeFromLeft (w), "SHORT-TERM", lufsText (readings.shortTermLufs, readings.shortTermValid), "LUFS", true);
            boxes.removeFromLeft (8);
            drawReadout (g, boxes.removeFromLeft (w), "TRUE PEAK", lufsText (readings.truePeakDb, readings.truePeakDb > -99.0f), "dBTP", ! peakTooHot);
            boxes.removeFromLeft (8);
            drawReadout (g, boxes, "RANGE", readings.loudnessRangeValid ? oneDecimal (readings.loudnessRangeLu) : juce::String ("-"), "LU", true);

            // What it means
            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.drawFittedText (statusText (readings, target), statusArea, juce::Justification::centredLeft, 2);

            drawHistory (g, historyArea, target.lufs);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            r.removeFromTop (22 + 8);
            auto targets = r.removeFromTop (28);
            const auto w = juce::jmin (110, (targets.getWidth() - 4 * 6) / juce::jmax (1, targetButtons.size()));

            for (auto* b : targetButtons)
            {
                b->setBounds (targets.removeFromLeft (w));
                targets.removeFromLeft (6);
            }

            r.removeFromTop (10);
            readoutArea = r.removeFromTop (58);
            r.removeFromTop (10);
            auto status = r.removeFromTop (30);
            resetButton.setBounds (status.removeFromRight (resetButton.getIdealWidth (28)).withSizeKeepingCentre (resetButton.getIdealWidth (28), 28));
            status.removeFromRight (6);
            matchButton.setBounds (status.removeFromRight (matchButton.getIdealWidth (28)).withSizeKeepingCentre (matchButton.getIdealWidth (28), 28));
            status.removeFromRight (10);
            statusArea = status;
            r.removeFromTop (10);
            historyArea = r;
        }

    private:
        MasterView& owner;
        juce::OwnedArray<juce::TextButton> targetButtons;
        IconButton matchButton { "Match target", Icon::wand, "Match target", IconButton::Style::primary };
        IconButton resetButton { "Reset", Icon::none, "Reset", IconButton::Style::ghost };
        juce::Rectangle<int> readoutArea, statusArea, historyArea;
        std::vector<juce::Point<float>> history;   // (song seconds, short-term LUFS)

        static void drawReadout (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, const juce::String& value,
                                 const juce::String& unit, bool ok)
        {
            drawWell (g, r.toFloat(), (float) Theme::cornerRadius);

            // A thin bar along the bottom in the reading's state: green on target, amber when not
            if (value != "-")
            {
                g.setColour ((ok ? Theme::success : Theme::warning).withAlpha (0.8f));
                g.fillRoundedRectangle (r.toFloat().reduced (8.0f, 0.0f).removeFromBottom (2.0f), 1.0f);
            }

            r = r.reduced (12, 7);
            drawCaption (g, label, r.removeFromTop (13));
            const auto valueFont = Theme::font (Theme::readoutFontSize, Theme::Weight::semibold);
            g.setColour (value == "-" ? Theme::textFaint : ok ? Theme::displayText : Theme::warning);
            g.setFont (valueFont);
            const auto valueWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (valueFont, value)) + 2;
            g.drawText (value, r.removeFromLeft (juce::jmin (valueWidth, r.getWidth())), juce::Justification::centredLeft, false);
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize, Theme::Weight::medium));
            g.drawText (" " + unit, r.withTrimmedTop (6), juce::Justification::centredLeft, false);
        }

        juce::String statusText (const te::LoudnessMeter::Readings& l, const Session::LoudnessTarget& target) const
        {
            if (! l.integratedValid)
                return "Play the song (the loudest part at least) to measure it.";

            const auto off = l.integratedLufs - target.lufs;
            juce::String text;

            if (std::abs (off) <= 1.0f)        text = "Loudness is on target for " + target.name + ".";
            else if (off < 0)                  text = oneDecimal (-off) + " dB quieter than " + target.name + " wants: Match target turns it up.";
            else                               text = oneDecimal (off) + " dB louder than " + target.name + " plays it: they'll turn it down.";

            if (l.truePeakDb > target.ceilingDb + 0.05f)
                text << " True peak is " << oneDecimal (l.truePeakDb - target.ceilingDb) << " dB too hot.";

            return text;
        }

        void drawHistory (juce::Graphics& g, juce::Rectangle<int> r, float targetLufs) const
        {
            if (r.getHeight() < 30)
                return;

            drawWell (g, r.toFloat(), (float) Theme::cornerRadius);

            auto* edit = owner.session.getEdit();
            const auto length = juce::jmax (1.0, owner.session.getSongLength().inSeconds());
            const auto area = r.toFloat().reduced (4.0f, 16.0f).withTrimmedLeft (30.0f);
            const auto yForAxis = [&] (float lufs) { return juce::jmap (juce::jlimit (-36.0f, -4.0f, lufs), -36.0f, -4.0f, area.getBottom(), area.getY()); };

            // A LUFS scale on the left, with faint lines across
            g.setFont (Theme::font (Theme::captionFontSize));

            for (auto lufs : { -30.0f, -24.0f, -18.0f, -12.0f, -6.0f })
            {
                const auto y = yForAxis (lufs);
                g.setColour (Theme::hairline.withAlpha (0.6f));
                g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
                g.setColour (Theme::textFaint);
                g.drawText (juce::String ((int) lufs), juce::Rectangle<float> ((float) r.getX() + 4.0f, y - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight, false);
            }
            const auto xFor = [&] (double seconds) { return area.getX() + (float) (seconds / length) * area.getWidth(); };
            const auto yFor = [&] (float lufs) { return juce::jmap (juce::jlimit (-36.0f, -4.0f, lufs), -36.0f, -4.0f, area.getBottom(), area.getY()); };

            // Sections across the top
            if (edit != nullptr)
            {
                g.setFont (Theme::font (Theme::captionFontSize + 0.5f));

                for (const auto& s : owner.session.getSections())
                {
                    const auto x = xFor (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (s.startBeat)).inSeconds());
                    g.setColour (Theme::border.withAlpha (0.8f));
                    g.drawVerticalLine (juce::roundToInt (x), (float) r.getY() + 2.0f, (float) r.getBottom() - 2.0f);
                    g.setColour (Theme::textDim);
                    g.drawText (s.name, juce::Rectangle<float> (x + 4.0f, (float) r.getY() + 2.0f, 120.0f, 12.0f), juce::Justification::centredLeft, true);
                }
            }

            // The target, with a +-1 LU band: inside it is "on target"
            const auto ty = yFor (targetLufs);
            g.setColour (Theme::success.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (area.getX(), yFor (targetLufs + 1.0f), area.getRight(), yFor (targetLufs - 1.0f)));
            g.setColour (Theme::accent);
            const float dashes[] = { 4.0f, 4.0f };
            g.drawDashedLine ({ area.getX(), ty, area.getRight(), ty }, dashes, 2, 1.0f);
            g.setFont (Theme::font (Theme::captionFontSize + 0.5f));
            g.drawText ("target " + juce::String (juce::roundToInt (targetLufs)), juce::Rectangle<float> (area.getRight() - 80.0f, ty - 14.0f, 78.0f, 12.0f),
                        juce::Justification::centredRight, false);

            if (history.size() < 2)
            {
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (Theme::smallFontSize));
                g.drawText ("Play the song: its loudness is drawn here", r, juce::Justification::centred, false);
                return;
            }

            juce::Path line;

            for (size_t i = 0; i < history.size(); ++i)
            {
                const juce::Point<float> p (xFor (history[i].x), yFor (history[i].y));

                if (i == 0 || history[i].x - history[i - 1].x > 0.5f)   line.startNewSubPath (p);   // a jump: don't join
                else                                                      line.lineTo (p);
            }

            // The loudness line over a soft fill down to the floor
            {
                juce::Path under;

                for (size_t i = 0; i < history.size(); ++i)
                {
                    const juce::Point<float> p (xFor (history[i].x), yFor (history[i].y));

                    if (i == 0 || history[i].x - history[i - 1].x > 0.5f)
                    {
                        if (i > 0)
                        {
                            under.lineTo (xFor (history[i - 1].x), area.getBottom());
                            under.closeSubPath();
                        }

                        under.startNewSubPath (p.x, area.getBottom());
                    }

                    under.lineTo (p);
                }

                under.lineTo (xFor (history.back().x), area.getBottom());
                under.closeSubPath();
                g.setGradientFill (juce::ColourGradient::vertical (Theme::accent.withAlpha (0.28f), area.getY(), Theme::accent.withAlpha (0.02f), area.getBottom()));
                g.fillPath (under);
            }

            g.setColour (Theme::accent.brighter (0.2f));
            g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    };

    //==============================================================================
    class MasterView::ReferencePanel : public juce::Component
    {
    public:
        explicit ReferencePanel (MasterView& o) : owner (o)
        {
            loadButton.setTooltip ("Load a finished song to compare with (WAV, FLAC, MP3...)");
            loadButton.onClick = [this] { chooseFile(); };
            clearButton.setTooltip ("Remove the reference");
            clearButton.onClick = [this] { owner.session.clearReference(); };

            for (auto* b : { &mixButton, &refButton })
            {
                b->setClickingTogglesState (true);
                b->setRadioGroupId (99);
                b->setWantsKeyboardFocus (false);
                b->setColour (juce::TextButton::buttonOnColourId, Theme::selectedRow);
                addAndMakeVisible (b);
            }

            mixButton.setConnectedEdges (juce::Button::ConnectedOnRight);
            refButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
            mixButton.setTooltip ("A: hear your mix (Ctrl+Shift+F switches)");
            refButton.setTooltip ("B: hear the reference instead, at the same place in the song (Ctrl+Shift+F switches)");
            mixButton.onClick = [this] { if (mixButton.getToggleState()) owner.session.setReferenceListening (false); };
            refButton.onClick = [this] { if (refButton.getToggleState()) owner.session.setReferenceListening (true); };

            match.setTooltip ("Play the reference as loud as your mix: louder always sounds better, so this keeps it fair");
            match.onClick = [this] { owner.session.setReferenceMatchLoudness (match.getToggleState()); };

            for (juce::Component* c : { (juce::Component*) &loadButton, (juce::Component*) &clearButton, (juce::Component*) &match })
            {
                c->setWantsKeyboardFocus (false);
                addAndMakeVisible (c);
            }
        }

        void refresh()
        {
            const auto file = owner.session.getReferenceFile();
            const bool loaded = file.existsAsFile();
            mixButton.setToggleState (! owner.session.isReferenceListening(), juce::dontSendNotification);
            refButton.setToggleState (owner.session.isReferenceListening(), juce::dontSendNotification);
            refButton.setEnabled (loaded);
            clearButton.setVisible (file != juce::File());
            match.setToggleState (owner.session.getReferenceMatchLoudness(), juce::dontSendNotification);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds());
            drawTitle (g, "Reference A/B", getLocalBounds().reduced (12, 0).removeFromTop (40));

            drawWell (g, fileArea.toFloat(), (float) Theme::cornerRadius);
            auto text = fileArea.reduced (10, 0);
            Icons::draw (g, Icon::file, text.removeFromLeft (14).toFloat().withSizeKeepingCentre (14.0f, 14.0f), Theme::textDim, 1.5f);
            text.removeFromLeft (8);
            const auto file = owner.session.getReferenceFile();
            g.setFont (Theme::font (Theme::smallFontSize));
            g.setColour (Theme::textDim);
            g.drawText (owner.session.isReferenceMeasured() ? oneDecimal (owner.session.getReferenceLufs()) + " LUFS"
                        : file.existsAsFile() ? juce::String ("measuring...") : juce::String(),
                        text.removeFromRight (78), juce::Justification::centredRight, false);
            g.setColour (file.existsAsFile() ? Theme::text : Theme::textDim);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.drawText (file == juce::File() ? juce::String ("No reference loaded")
                        : file.existsAsFile() ? file.getFileName() : file.getFileName() + " (missing)",
                        text, juce::Justification::centredLeft, true);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            auto title = r.removeFromTop (28);
            loadButton.setBounds (title.removeFromRight (loadButton.getIdealWidth (26)).withSizeKeepingCentre (loadButton.getIdealWidth (26), 26));
            title.removeFromRight (4);
            clearButton.setBounds (title.removeFromRight (26).withSizeKeepingCentre (26, 26));
            r.removeFromTop (8);
            fileArea = r.removeFromTop (32);
            r.removeFromTop (8);
            auto ab = r.removeFromTop (28);
            mixButton.setBounds (ab.removeFromLeft (ab.getWidth() / 2));
            refButton.setBounds (ab);
            r.removeFromTop (8);
            match.setBounds (r.removeFromTop (24));
        }

    private:
        MasterView& owner;
        IconButton loadButton { "Load reference", Icon::open, "Load..." };
        IconButton clearButton { "Remove reference", Icon::close, {}, IconButton::Style::ghost };
        juce::TextButton mixButton { "A: My mix" }, refButton { "B: Reference" };
        juce::ToggleButton match { "Match loudness (fair comparison)" };
        juce::Rectangle<int> fileArea;
        std::unique_ptr<juce::FileChooser> chooser;

        void chooseFile()
        {
            chooser = std::make_unique<juce::FileChooser> ("Pick a reference song", owner.session.getReferenceFile().getParentDirectory(),
                                                           "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            juce::Component::SafePointer<ReferencePanel> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe] (const juce::FileChooser& fc)
                                  {
                                      if (safe != nullptr && fc.getResult().existsAsFile())
                                          safe->owner.report (safe->owner.session.loadReference (fc.getResult()), "Can't use that file");
                                  });
        }
    };

    //==============================================================================
    MasterView::MasterView (AudioEngine& e, Session& s, juce::ApplicationCommandManager& c)
        : audioEngine (e), session (s), commands (c)
    {
        chainButton.setTooltip ("The mastering chain on or off: compare the song with and without it (Ctrl+Shift+A)");
        chainButton.onClick = [this] { report (session.setMasterChainOn (! session.isMasterChainOn()), "Could not switch the chain"); };
        chainButton.setWantsKeyboardFocus (false);
        addAndMakeVisible (chainButton);

        presetBox.setTooltip ("Mastering presets: a starting point for the whole chain (switches it on)");
        presetBox.setWantsKeyboardFocus (false);
        presetBox.onChange = [this]
        {
            const auto index = presetBox.getSelectedId() - 1;

            if (index >= 0)
                report (session.applyMasterPreset (index), "Could not use that preset");

            refresh();
        };
        addAndMakeVisible (presetBox);

        chainRow = std::make_unique<ChainRow> (*this);
        spectrum = std::make_unique<SpectrumView> (session);
        addAndMakeVisible (*spectrum);
        chainViewport.setViewedComponent (chainRow.get(), false);
        chainViewport.setScrollBarsShown (false, true);
        addAndMakeVisible (chainViewport);

        loudness = std::make_unique<LoudnessPanel> (*this);
        reference = std::make_unique<ReferencePanel> (*this);
        addAndMakeVisible (*loudness);
        addAndMakeVisible (*reference);

        outputBox.setTooltip ("Where you hear EzDAWsIt (the same as the top bar and Setup)");
        outputBox.setWantsKeyboardFocus (false);
        outputBox.onChange = [this]
        {
            const auto index = outputBox.getSelectedItemIndex();

            if (juce::isPositiveAndBelow (index, outputIDs.size()) && outputIDs[index] != audioEngine.getMainOutputID()
                 && ! session.setMainOutput (outputIDs[index]))
            {
                lastOutputSignature = {};
                refreshOutputs();
            }
        };
        addAndMakeVisible (outputBox);

        setupButton.setCommandToTrigger (&commands, CommandIDs::audioSetup, true);
        exportButton.setCommandToTrigger (&commands, CommandIDs::exportSong, true);

        adviceButton.setTooltip ("Do what the assistant suggests (one undo step), then play the song again for the next tip");
        adviceButton.onClick = [this] { report (session.applyMasterAdvice (advice), "Master assistant"); };
        addChildComponent (adviceButton);

        for (auto* b : { &setupButton, &exportButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }

        session.addListener (this);
        refresh();
        refreshOutputs();
        startTimerHz (15);
    }

    MasterView::~MasterView()
    {
        session.removeListener (this);
    }

    void MasterView::sessionSongChanged()
    {
        loudness->clearHistory();   // the last song's curve
        refresh();
    }

    void MasterView::report (bool ok, const juce::String& title)
    {
        if (! ok && onProblem)
            onProblem (title, session.getLastError());

        refresh();
    }

    void MasterView::refresh()
    {
        auto* chain = session.getMasteringChain();
        const bool hasChain = chain != nullptr;

        chainButton.setEnabled (hasChain);
        chainButton.setToggleState (session.isMasterChainOn(), juce::dontSendNotification);
        chainButton.setButtonText (session.isMasterChainOn() ? "Chain on" : "Chain off");
        chainButton.setIconColour (session.isMasterChainOn() ? Theme::displayText : Theme::textDim);

        // Presets: the list, with the current name (and "(edited)") shown when it isn't one of them as it is
        const auto current = session.getMasterPresetName();
        const auto presets = Session::getMasterPresets();

        if (presetBox.getNumItems() != (int) presets.size())
        {
            presetBox.clear (juce::dontSendNotification);

            for (size_t i = 0; i < presets.size(); ++i)
                presetBox.addItem (presets[i].name + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + presets[i].description, (int) i + 1);
        }

        int selected = 0;

        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == current)
                selected = (int) i + 1;

        presetBox.setSelectedId (selected, juce::dontSendNotification);
        presetBox.setTextWhenNothingSelected (current.isNotEmpty() ? current : juce::String ("Pick a preset"));
        presetBox.setEnabled (hasChain);

        chainRow->refresh (chain);
        loudness->refresh();
        reference->refresh();
        repaint();
    }

    void MasterView::refreshOutputs()
    {
        outputBox.setEnabled (! session.isRecording());
        const auto outputs = audioEngine.getOutputs();
        const auto mainID = audioEngine.getMainOutputID();
        juce::String signature (mainID);

        for (const auto& o : outputs)
            signature << "|" << o.id << "=" << o.name;

        if (signature == lastOutputSignature || outputBox.isPopupActive())
            return;

        lastOutputSignature = signature;
        outputBox.clear (juce::dontSendNotification);
        outputIDs.clear();

        for (int i = 0; i < outputs.size(); ++i)
        {
            outputBox.addItem (outputs.getReference (i).name, i + 1);
            outputIDs.add (outputs.getReference (i).id);
        }

        outputBox.setSelectedItemIndex (outputIDs.indexOf (mainID), juce::dontSendNotification);
        outputBox.setTextWhenNothingSelected (outputs.isEmpty() ? "No audio output: open Setup" : "Pick an output");
        outputBox.setTextWhenNoChoicesAvailable ("No audio outputs found: open Setup");
    }

    void MasterView::timerCallback()
    {
        if (! isShowing())
            return;

        session.updateMaster();
        chainRow->refreshMeters();
        loudness->sample();
        loudness->repaint();
        reference->repaint();

        // The assistant: one tip at a time, from what the meter has heard
        if (const auto next = session.getMasterAdvice(); next.title != advice.title || next.text != advice.text || next.button != advice.button)
        {
            advice = next;
            adviceButton.setButtonText (advice.button);
            adviceButton.setVisible (advice.button.isNotEmpty() && ! assistantArea.isEmpty());
            resized();
            repaint (assistantArea);
        }

        if (++outputTicks % 15 == 0)
            refreshOutputs();
    }

    //==============================================================================
    void MasterView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::background);

        for (auto* panel : { (juce::Component*) loudness.get(), (juce::Component*) reference.get() })
            if (panel != nullptr && panel->isVisible())
                Theme::drawShadow (g, panel->getBounds().toFloat(), (float) Theme::cardRadius, 1);

        g.setColour (Theme::bar);
        g.fillRect (headerArea);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (headerArea.getBottom() - 1, 0.0f, (float) getWidth());
        auto header = headerArea.reduced (12, 0);
        drawTitle (g, "Master", header.removeFromLeft (70));
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::smallFontSize));
        g.drawText ("Final polish for the whole song: the chain, its loudness, and a reference to compare with.", header,
                    juce::Justification::centredLeft, true);

        drawTitle (g, "Mastering chain", chainTitleArea);

        // The assistant: what it heard, and the one fix it suggests
        if (! assistantArea.isEmpty())
        {
            Theme::drawShadow (g, assistantArea.toFloat(), (float) Theme::cardRadius, 1);
            drawPanel (g, assistantArea);
            auto r = assistantArea.reduced (12);
            drawCaption (g, "Master assistant", r.removeFromTop (16));
            g.setColour (advice.fix == Session::MasterAdvice::Fix::none && advice.title == "On target" ? Theme::displayText : Theme::text);
            g.setFont (Theme::font (Theme::titleFontSize, true));
            g.drawText (advice.title, r.removeFromTop (22), juce::Justification::centredLeft, true);

            if (adviceButton.isVisible())
                r.removeFromBottom (adviceButton.getHeight() + 6);

            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize));
            g.drawFittedText (advice.text, r, juce::Justification::topLeft, juce::jmax (1, r.getHeight() / 15));
        }

        // Main output row
        Theme::drawShadow (g, outputArea.toFloat(), (float) Theme::cardRadius, 1);
        drawPanel (g, outputArea);
        auto out = outputArea.reduced (12, 0);
        Icons::draw (g, Icon::headphones, out.removeFromLeft (18).toFloat().withSizeKeepingCentre (16, 16), Theme::text);
        out.removeFromLeft (8);
        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::baseFontSize, true));
        g.drawText ("Main output", out.removeFromLeft (90), juce::Justification::centredLeft, false);
    }

    void MasterView::paintOverChildren (juce::Graphics&) {}

    void MasterView::resized()
    {
        auto r = getLocalBounds();
        headerArea = r.removeFromTop (34);
        r = r.reduced (12, 10);

        // Main output along the bottom, with its spectrum (taller when the window has room to spare)
        outputArea = r.removeFromBottom (juce::jlimit (48, 120, r.getHeight() - 540));
        r.removeFromBottom (gap);
        {
            auto out = outputArea.reduced (12, 0);
            out.removeFromLeft (18 + 8 + 90);
            exportButton.setBounds (out.removeFromRight (exportButton.getIdealWidth (30)).withSizeKeepingCentre (exportButton.getIdealWidth (30), 30));
            out.removeFromRight (8);
            setupButton.setBounds (out.removeFromRight (setupButton.getIdealWidth (28)).withSizeKeepingCentre (setupButton.getIdealWidth (28), 28));
            out.removeFromRight (12);
            const auto boxWidth = juce::jmin (260, out.getWidth());
            outputBox.setBounds (out.removeFromLeft (boxWidth).withSizeKeepingCentre (boxWidth, 28));
            out.removeFromLeft (12);
            spectrum->setVisible (out.getWidth() >= 120);
            spectrum->setBounds (out.reduced (0, 6));
        }

        // Chain: title row, then the cards (scrolling sideways if they don't fit)
        auto titleRow = r.removeFromTop (30);
        chainTitleArea = titleRow.removeFromLeft (140);
        chainButton.setBounds (titleRow.removeFromLeft (chainButton.getIdealWidth (28)).withSizeKeepingCentre (chainButton.getIdealWidth (28), 28));
        titleRow.removeFromLeft (10);
        presetBox.setBounds (titleRow.removeFromLeft (juce::jmin (380, titleRow.getWidth())).withSizeKeepingCentre (juce::jmin (380, titleRow.getWidth()), 28));
        r.removeFromTop (8);

        const auto chainHeight = juce::jlimit (150, 220, r.getHeight() / 2);
        auto chainArea = r.removeFromTop (chainHeight);
        const auto needsScroll = chainRow->getMinimumWidth() > chainArea.getWidth();
        chainViewport.setBounds (chainArea);
        chainRow->setSize (juce::jmax (chainArea.getWidth(), chainRow->getMinimumWidth()),
                           chainArea.getHeight() - (needsScroll ? chainViewport.getScrollBarThickness() : 0));
        r.removeFromTop (gap);

        // Loudness | reference + assistant
        const auto rightWidth = juce::jlimit (240, 330, r.getWidth() / 3);
        auto right = r.removeFromRight (rightWidth);
        r.removeFromRight (gap);
        loudness->setBounds (r);
        reference->setBounds (right.removeFromTop (juce::jmin (right.getHeight(), 176)));
        right.removeFromTop (gap);
        assistantArea = right.getHeight() >= 70 ? right : juce::Rectangle<int>();

        adviceButton.setVisible (advice.button.isNotEmpty() && ! assistantArea.isEmpty());

        if (! assistantArea.isEmpty())
            adviceButton.setBounds (assistantArea.reduced (12).removeFromBottom (26).removeFromLeft (juce::jmin (assistantArea.getWidth() - 24, 180)));
    }
}
