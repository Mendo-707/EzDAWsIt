/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "VocalChainCard.h"
#include "../../Engine/VocalChain.h"
#include "../Theme.h"

namespace ez
{
    namespace
    {
        using VC = VocalChainPlugin;

        /** The preset a name like "Warm R&B lead (edited)" came from, or -1. */
        int presetIndexFor (const juce::String& name)
        {
            const auto& presets = VC::getPresets();
            const auto base = name.upToLastOccurrenceOf (" (edited)", false, false);

            for (size_t i = 0; i < presets.size(); ++i)
                if (base == presets[i].name)
                    return (int) i;

            return -1;
        }
    }

    //==============================================================================
    class VocalChainCard::Macro : public juce::Component
    {
    public:
        Macro (VocalChainCard& o, int m) : owner (o), index (m)
        {
            knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            knob.setRange (0.0, 100.0, 1.0);
            knob.setVelocityModeParameters (0.3, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Shift: fine
            knob.setPopupDisplayEnabled (true, true, nullptr);
            knob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
            knob.setColour (juce::Slider::rotarySliderFillColourId, Theme::success);
            knob.onDragStart = [this]
            {
                dragging = true;
                started = false;
                gestureRef = owner.stillHere() ? owner.session.getEffectParam (owner.trackIndex, owner.effectIndex, index) : Session::ParamRef();
                owner.session.beginParameterGesture (owner.trackIndex, gestureRef);
            };
            knob.onDragEnd = [this]
            {
                dragging = false;
                owner.session.endParameterGesture (owner.trackIndex, std::exchange (gestureRef, {}));   // exactly the one begun
                owner.refresh();
            };
            knob.onValueChange = [this]
            {
                auto& s = owner.session;

                if (s.isParamLockedByAutomation (owner.trackIndex, s.getEffectParam (owner.trackIndex, owner.effectIndex, index)))
                {
                    s.showHint (VC::macroName (index) + " is automated: edit its lane, or turn on Write and move it while the song plays");

                    if (! dragging)
                        owner.refresh();

                    return;   // never an error box per drag step
                }

                const auto value = (float) knob.getValue();
                const auto continues = dragging && started;
                started = dragging;
                owner.act ([this, value, continues] { return owner.session.setEffectControl (owner.trackIndex, owner.effectIndex, index, value, continues); },
                           "Could not change " + VC::macroName (index));
                owner.refresh();
            };
            knob.onMenu = [this] { showMenu(); };   // right-click on the knob: our menu, never a drag
            addAndMakeVisible (knob);
        }

        ~Macro() override
        {
            if (dragging)
                owner.session.endParameterGesture (owner.trackIndex, gestureRef);
        }

        void refresh (int presetIndex, bool showHints)
        {
            if (std::exchange (hints, showHints) != showHints)
                resized();   // the knob gets the hint line's room, or gives it back
            const auto value = owner.session.getEffectControl (owner.trackIndex, owner.effectIndex, index);

            if (! knob.isMouseButtonDown())
                knob.setValue (value, juce::dontSendNotification);

            if (presetIndex >= 0)
                knob.setDoubleClickReturnValue (true, (double) VC::getPresets()[(size_t) presetIndex].macros[index]);

            auto text = juce::String (juce::roundToInt (value)) + "%";

            if (index == VC::punch)
                if (const auto reduction = owner.session.getVocalChainReductionDb (owner.trackIndex, owner.effectIndex); reduction < -0.5f)
                    text << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) << juce::String (reduction, 1) << " dB";   // how hard it's working now

            const bool locked = owner.session.isParamLockedByAutomation (owner.trackIndex, owner.session.getEffectParam (owner.trackIndex, owner.effectIndex, index));

            if (text != valueText || locked != wasLocked)
            {
                valueText = text;
                wasLocked = locked;
                knob.setTooltip (VC::macroName (index) + ": " + VC::macroHint (index).toLowerCase() + ". " + text
                                 + (locked ? " (follows its automation lane)" : "")
                                 + ". Drag (Shift: fine) or scroll; double-click: the preset's value; right-click for more");
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::baseFontSize, true));
            g.drawText (VC::macroName (index), r.removeFromTop (16), juce::Justification::centred, false);
            r.removeFromTop (knob.getHeight() + 2);
            g.setColour (wasLocked ? Theme::accent : Theme::text);
            g.setFont (Theme::font (Theme::captionFontSize + 0.5f));
            g.drawText (valueText, r.removeFromTop (14), juce::Justification::centred, false);

            if (hints)
            {
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (Theme::captionFontSize - 0.5f));
                g.drawFittedText (VC::macroHint (index), r.removeFromTop (26), juce::Justification::centredTop, 2, 1.0f);
            }
        }

        void resized() override
        {
            // The knob gives way first (down to 24 px) so the name and value always show
            const auto size = juce::jlimit (24, knobSize, getHeight() - 16 - 2 - 14 - (hints ? 26 : 0));
            knob.setBounds (getLocalBounds().withTrimmedTop (16).removeFromTop (size).withSizeKeepingCentre (size, size));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                showMenu();
        }

        void showMenu()
        {
            const auto presetIndex = presetIndexFor (owner.session.getVocalChainPresetName (owner.trackIndex, owner.effectIndex));
            juce::PopupMenu menu;
            menu.addSectionHeader (VC::macroName (index) + ": " + VC::macroHint (index));
            menu.addItem (1, "0% (off)");
            menu.addItem (2, "50%");
            menu.addItem (3, "100%");
            menu.addItem (4, "Back to the preset's value (double-click)", presetIndex >= 0);
            menu.addSeparator();
            menu.addItem (5, "Show its automation lane");

            juce::Component::SafePointer<Macro> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, presetIndex] (int result)
            {
                if (safe == nullptr || result <= 0)
                    return;

                auto& o = safe->owner;
                const auto i = safe->index;

                if (result == 5)
                {
                    o.act ([&o, i] { return o.session.setAutomationLaneShown (o.trackIndex, o.session.getEffectParam (o.trackIndex, o.effectIndex, i), true); },
                           "Could not show the lane");
                    return;
                }

                const auto value = result == 1 ? 0.0f : result == 2 ? 50.0f : result == 3 ? 100.0f
                                 : VC::getPresets()[(size_t) juce::jmax (0, presetIndex)].macros[i];
                o.act ([&o, i, value] { return o.session.setEffectControl (o.trackIndex, o.effectIndex, i, value); }, "Could not change " + VC::macroName (i));
                o.refresh();
            });
        }

        static constexpr int knobSize = 44;

    private:
        VocalChainCard& owner;
        const int index;
        MenuSlider knob;
        juce::String valueText;
        Session::ParamRef gestureRef;
        bool dragging = false, started = false, hints = true, wasLocked = false;
    };

    //==============================================================================
    class VocalChainCard::ModuleChip : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        ModuleChip (VocalChainCard& o, int m) : owner (o), index (m)
        {
            setTooltip (VC::moduleName (index) + ": " + VC::moduleHint (index) + ". Click to switch it off or on");
        }

        int getIdealWidth() const
        {
            return juce::GlyphArrangement::getStringWidthInt (Theme::font (Theme::captionFontSize + 0.5f), VC::moduleName (index)) + 22;
        }

        void refresh()
        {
            const auto nowOn = owner.session.getEffectControl (owner.trackIndex, owner.effectIndex, VC::firstModule + index) >= 0.5f;

            if (nowOn != isOn)
            {
                isOn = nowOn;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (isOn ? Theme::raised : Theme::panel);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Theme::border);
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
            g.setColour (isOn ? Theme::success : Theme::border);
            g.fillEllipse (r.getX() + 7.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
            g.setColour (isOn ? Theme::text : Theme::textDim);
            g.setFont (Theme::font (Theme::captionFontSize + 0.5f));
            g.drawText (VC::moduleName (index), r.withTrimmedLeft (17.0f), juce::Justification::centredLeft, true);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mouseWasClicked())
                return;

            const auto control = VC::firstModule + index;
            const auto value = isOn ? 0.0f : 1.0f;
            auto& o = owner;
            o.act ([&o, control, value] { return o.session.setEffectControl (o.trackIndex, o.effectIndex, control, value); },
                   "Could not switch " + VC::moduleName (index));
            o.refresh();
        }

    private:
        VocalChainCard& owner;
        const int index;
        bool isOn = true;
    };

    //==============================================================================
    VocalChainCard::VocalChainCard (Session& s, int t, int e, Session::DeviceInfo i,
                                    std::function<void (std::function<bool()>, const juce::String&)> a, int numEffects)
        : session (s), trackIndex (t), effectIndex (e), info (std::move (i)), act (std::move (a))
    {
        bypassButton.setClickingTogglesState (false);
        bypassButton.setToggleState (info.isEnabled, juce::dontSendNotification);
        bypassButton.setTooltip (info.isEnabled ? "On: click to bypass the whole chain" : "Bypassed: click to switch it back on");
        leftButton.setTooltip ("Move earlier in the chain");
        rightButton.setTooltip ("Move later in the chain");
        removeButton.setTooltip ("Remove the Vocal Chain (Ctrl+Z brings it back)");
        openButton.setTooltip ("Every control as knobs (macros, switches, key)");
        leftButton.setEnabled (effectIndex > 0);
        rightButton.setEnabled (effectIndex < numEffects - 1);

        for (auto* b : { &bypassButton, &leftButton, &rightButton, &removeButton, &openButton })
            addAndMakeVisible (b);

        const auto& presets = VC::getPresets();

        for (size_t p = 0; p < presets.size(); ++p)
            presetBox.addItem (presets[p].name, (int) p + 1);

        presetBox.setTooltip ("Preset: sets all six macros and the switches at once (the key stays)");
        presetBox.onChange = [this]
        {
            const auto p = presetBox.getSelectedItemIndex();

            if (p >= 0)
                act ([this, p] { return session.applyVocalChainPreset (trackIndex, effectIndex, p); }, "Could not apply the preset");

            refresh();
        };
        presetBox.setWantsKeyboardFocus (false);
        addAndMakeVisible (presetBox);

        chainButton.setTooltip ("Show what's inside the chain (in place of the macros): switch each part off or on, pick the key. Click again for the macros");
        chainButton.onClick = [this]
        {
            showInside = ! showInside;
            resized();
            repaint();
        };
        addAndMakeVisible (chainButton);

        for (int m = 0; m < VC::numMacros; ++m)
            addAndMakeVisible (macros.add (new Macro (*this, m)));

        for (int m = 0; m < VC::numModules; ++m)
            addChildComponent (modules.add (new ModuleChip (*this, m)));

        keyButton.setTooltip ("The notes Tune pulls to. It starts in the song's key");
        keyButton.onClick = [this] { showKeyMenu(); };
        phoneButton.setTooltip ("Phone tone: a narrow, lo-fi telephone sound");
        phoneButton.setClickingTogglesState (false);
        phoneButton.onClick = [this]
        {
            const auto value = session.getEffectControl (trackIndex, effectIndex, VC::phone) >= 0.5f ? 0.0f : 1.0f;
            act ([this, value] { return session.setEffectControl (trackIndex, effectIndex, VC::phone, value); }, "Could not switch the phone tone");
            refresh();
        };

        for (auto* b : { &keyButton, &phoneButton })
        {
            b->setWantsKeyboardFocus (false);
            addChildComponent (b);
        }

        refresh();
        startTimerHz (10);   // automation, undo, and the Punch reading
    }

    VocalChainCard::~VocalChainCard() = default;

    bool VocalChainCard::stillHere() const
    {
        const auto now = session.getEffectInfo (trackIndex);
        return juce::isPositiveAndBelow (effectIndex, now.size()) && now.getReference (effectIndex).id == info.id;
    }

    void VocalChainCard::refresh()
    {
        if (! stillHere())
            return;   // another effect moved into this slot: the panel rebuilds within a moment

        const auto name = session.getVocalChainPresetName (trackIndex, effectIndex);
        const auto presetIndex = presetIndexFor (name);

        if (presetBox.getText() != name)
        {
            presetBox.setSelectedItemIndex (presetIndex, juce::dontSendNotification);
            presetBox.setText (name, juce::dontSendNotification);   // keeps " (edited)" visible
        }

        const bool hints = getHeight() - 70 >= 16 + Macro::knobSize + 2 + 14 + 26;   // room for the knob and the hint line

        for (auto* m : macros)
            m->refresh (presetIndex, hints);

        for (auto* chip : modules)
            chip->refresh();

        const auto key = session.formatEffectControl (trackIndex, effectIndex, VC::root) + " "
                       + session.formatEffectControl (trackIndex, effectIndex, VC::scale).toLowerCase();

        if (keyButton.getButtonText() != "Key: " + key)
            keyButton.setButtonText ("Key: " + key);

        phoneButton.setToggleState (session.getEffectControl (trackIndex, effectIndex, VC::phone) >= 0.5f, juce::dontSendNotification);
        chainButton.setToggleState (showInside, juce::dontSendNotification);
    }

    void VocalChainCard::showKeyMenu()
    {
        static const char* roots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        juce::PopupMenu menu, major, minor;
        const auto song = session.getKeyAt (session.getPosition());
        const auto root = juce::roundToInt (session.getEffectControl (trackIndex, effectIndex, VC::root));
        const auto scale = juce::roundToInt (session.getEffectControl (trackIndex, effectIndex, VC::scale));

        menu.addItem (1, "The song's key" + (song.root >= 0 ? " (" + Session::describeKey (song.root, song.minor) + ")" : juce::String (": none set")), song.root >= 0);
        menu.addItem (2, "Every note (chromatic)", true, scale == 0);
        menu.addSeparator();

        for (int r = 0; r < 12; ++r)
        {
            major.addItem (100 + r, juce::String (roots[r]) + " major", true, scale == 1 && root == r);
            minor.addItem (200 + r, juce::String (roots[r]) + " minor", true, scale == 2 && root == r);
        }

        menu.addSubMenu ("Major", major);
        menu.addSubMenu ("Minor", minor);

        juce::Component::SafePointer<VocalChainCard> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyButton), [safe, song] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;

            int newRoot = -1, newScale = 0;

            if (result == 1)            { newRoot = song.root; newScale = song.minor ? 2 : 1; }
            else if (result == 2)       { newScale = 0; }
            else if (result >= 200)     { newRoot = result - 200; newScale = 2; }
            else if (result >= 100)     { newRoot = result - 100; newScale = 1; }

            auto& s = safe->session;
            const auto t = safe->trackIndex, e = safe->effectIndex;
            safe->act ([&s, t, e, newRoot, newScale]
            {
                // One undo step for both: the second call continues the first
                const bool ok = s.setEffectControl (t, e, VC::scale, (float) newScale);
                return ok && (newRoot < 0 || s.setEffectControl (t, e, VC::root, (float) newRoot, true));
            }, "Could not change the key");
            safe->refresh();
        });
    }

    //==============================================================================
    void VocalChainCard::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        auto title = getLocalBounds().reduced (10, 8).removeFromTop (24).withTrimmedLeft (24);
        g.setColour (info.isEnabled ? Theme::text : Theme::textDim);
        g.setFont (Theme::font (Theme::titleFontSize, true));
        g.drawText (info.isEnabled ? "Vocal Chain" : "Vocal Chain (off)", title.removeFromLeft (120), juce::Justification::centredLeft, true);

        if (! insideArea.isEmpty())
        {
            g.setColour (Theme::laneSeparator);
            g.drawHorizontalLine (insideArea.getY() - 4, (float) insideArea.getX(), (float) insideArea.getRight());
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::captionFontSize - 0.5f, true));
            g.drawText ("INSIDE THE CHAIN", insideArea.withHeight (12), juce::Justification::centredLeft, false);
        }
    }

    void VocalChainCard::paintOverChildren (juce::Graphics& g)
    {
        // Arrows between the modules: the order the sound goes through them
        if (showInside)
            for (int i = 1; i < modules.size(); ++i)
                if (modules[i]->isVisible() && modules[i - 1]->isVisible() && modules[i]->getY() == modules[i - 1]->getY())
                {
                    const auto x = (float) modules[i]->getX() - 5.0f, y = (float) modules[i]->getBounds().getCentreY();
                    g.setColour (Theme::textDim);
                    g.drawArrow ({ x - 4.0f, y, x + 3.0f, y }, 1.0f, 5.0f, 4.0f);
                }
    }

    void VocalChainCard::resized()
    {
        auto r = getLocalBounds().reduced (10, 8);

        // Header
        auto header = r.removeFromTop (24);
        bypassButton.setBounds (header.removeFromLeft (22).withSizeKeepingCentre (22, 22));
        header.removeFromLeft (110);
        removeButton.setBounds (header.removeFromRight (24));
        const auto chainWidth = chainButton.getIdealWidth (22);
        chainButton.setBounds (header.removeFromRight (chainWidth).withSizeKeepingCentre (chainWidth, 22));
        header.removeFromRight (6);
        presetBox.setBounds (header.reduced (0, 1));
        r.removeFromTop (6);

        // Bottom: move and Open
        auto bottom = r.removeFromBottom (24);   // the same footer as the other device cards
        leftButton.setBounds (bottom.removeFromLeft (24));
        rightButton.setBounds (bottom.removeFromLeft (24));
        openButton.setBounds (bottom.removeFromRight (72));
        r.removeFromBottom (6);

        // The body: the six macros, or (Chain on) what's inside, in the same space so nothing is squeezed out
        insideArea = showInside ? r : juce::Rectangle<int>();

        for (auto* m : macros)
            m->setVisible (! showInside);

        for (auto* chip : modules)
            chip->setVisible (showInside);

        keyButton.setVisible (showInside);
        phoneButton.setVisible (showInside);

        if (showInside)
        {
            auto area = r.withTrimmedTop (14);
            auto keyRow = area.removeFromBottom (juce::jmin (24, juce::jmax (18, area.getHeight() / 3)));
            keyButton.setBounds (keyRow.removeFromLeft (150));
            keyRow.removeFromLeft (6);
            phoneButton.setBounds (keyRow.removeFromLeft (100));
            area.removeFromBottom (4);

            // Chips wrap; if the rows don't all fit, they get narrower (names shorten) rather than vanish
            const auto rowHeight = juce::jlimit (18, 26, area.getHeight() / 2);
            const auto rows = juce::jmax (1, area.getHeight() / rowHeight);
            int total = 0;

            for (auto* chip : modules)
                total += chip->getIdealWidth() + 8;

            const auto scale = juce::jmin (1.0f, (float) (area.getWidth() * rows) / (float) juce::jmax (1, total));
            int x = area.getX(), y = area.getY();

            for (auto* chip : modules)
            {
                const auto w = juce::jmax (40, (int) ((float) chip->getIdealWidth() * scale));

                if (x + w > area.getRight() && x > area.getX())
                {
                    x = area.getX();
                    y += rowHeight;
                }

                chip->setBounds (x, y, juce::jmin (w, area.getWidth()), rowHeight - 4);
                x += w + 8;
            }
        }
        else
        {
            const auto w = r.getWidth() / VC::numMacros;

            for (int m = 0; m < macros.size(); ++m)
                macros[m]->setBounds (r.getX() + m * w, r.getY(), w, r.getHeight());
        }

        refresh();
    }
}
