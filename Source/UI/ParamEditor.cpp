/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "ParamEditor.h"
#include "Theme.h"
#include "Widgets.h"
#include "../Engine/VocalChain.h"

namespace ez
{
    namespace
    {
        bool isSwitch (const te::AutomatableParameter& p)
        {
            const auto& r = p.valueRange;
            return r.interval >= 1.0f && r.end - r.start <= 11.0f;
        }

        /** Off/On: a toggle button, not a menu. */
        bool isToggle (te::AutomatableParameter& p)
        {
            return isSwitch (p) && p.valueRange.end - p.valueRange.start == 1.0f
                    && p.valueToString (p.valueRange.start).equalsIgnoreCase ("off");
        }

        /** "Clean-up on" reads as "Clean-up" on a button that lights up. */
        juce::String toggleName (te::AutomatableParameter& p)
        {
            auto name = p.getParameterName();
            return name.endsWithIgnoreCase (" on") ? name.dropLastCharacters (3) : name;
        }

        /** Moving a Vocal Chain macro or switch by hand: its preset name says it was changed (as on the card). */
        void markEdited (te::AutomatableParameter& p)
        {
            if (auto* chain = dynamic_cast<VocalChainPlugin*> (p.getPlugin()))
                if (p.paramID != "root" && p.paramID != "scale" && ! chain->getPresetName().endsWith (" (edited)"))
                    chain->setPresetName (chain->getPresetName() + " (edited)");
        }

        /** With Write on, a drag records only if the track is in touch mode (a new track is in read). */
        void beginGesture (te::AutomatableParameter& p)
        {
            auto& arm = p.getEdit().getAutomationRecordManager();

            if (auto* track = p.getTrack(); track != nullptr && arm.isWritingAutomation()
                 && track->state[te::IDs::automationMode].toString() != te::toString (te::AutomationMode::touch))
                track->state.setProperty (te::IDs::automationMode, te::toString (te::AutomationMode::touch), nullptr);

            p.parameterChangeGestureBegin();
        }

        bool isLocked (te::AutomatableParameter& p)
        {
            auto& edit = p.getEdit();
            auto& arm = edit.getAutomationRecordManager();
            return p.hasAutomationPoints() && arm.isReadingAutomation() && ! (arm.isWritingAutomation() && edit.getTransport().isPlaying());
        }

        //==============================================================================
        class Control : public juce::Component
        {
        public:
            explicit Control (te::AutomatableParameter& p) : param (&p)
            {
                name.setText (p.getParameterName(), juce::dontSendNotification);
                name.setJustificationType (juce::Justification::centred);
                name.setFont (Theme::font (Theme::captionFontSize + 0.5f, true));
                name.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (name);

                value.setJustificationType (juce::Justification::centred);
                value.setFont (Theme::font (Theme::captionFontSize + 0.5f));
                value.setColour (juce::Label::textColourId, Theme::textDim);
                value.setEditable (false, true);   // double-click the number to type one
                value.onTextChange = [this]
                {
                    if (param != nullptr && ! isLocked (*param))
                    {
                        param->getEdit().getUndoManager().beginNewTransaction();
                        markEdited (*param);
                        param->setParameter (param->valueRange.snapToLegalValue (param->stringToValue (value.getText())), juce::sendNotification);
                    }

                    refresh();
                };
                addAndMakeVisible (value);

                if (isToggle (p))
                {
                    toggle.setButtonText (toggleName (p));
                    toggle.setClickingTogglesState (false);
                    toggle.setColour (juce::TextButton::buttonOnColourId, Theme::accent.withAlpha (0.55f));
                    toggle.setTooltip (toggleName (p) + ": click to switch it on or off");
                    toggle.onClick = [this]
                    {
                        if (param == nullptr || isLocked (*param))
                            return refresh();

                        param->getEdit().getUndoManager().beginNewTransaction();
                        markEdited (*param);
                        param->setParameter (param->getCurrentValue() >= 0.5f ? param->valueRange.start : param->valueRange.end, juce::sendNotification);
                        refresh();
                    };
                    toggle.onMenu = [this] { showMenu(); };
                    addAndMakeVisible (toggle);
                    name.setVisible (false);
                    value.setVisible (false);
                }
                else if (isSwitch (p))
                {
                    name.setJustificationType (juce::Justification::centredLeft);
                    for (int i = 0; i <= juce::roundToInt (p.valueRange.end - p.valueRange.start); ++i)
                        choice.addItem (p.valueToString (p.valueRange.start + (float) i), i + 1);

                    choice.onChange = [this]
                    {
                        if (param == nullptr || isLocked (*param))
                            return refresh();

                        param->getEdit().getUndoManager().beginNewTransaction();
                        markEdited (*param);
                        param->setParameter (param->valueRange.start + (float) (choice.getSelectedItemIndex()), juce::sendNotification);
                    };
                    choice.setTooltip (p.getParameterName());
                    addAndMakeVisible (choice);
                    value.setVisible (false);
                }
                else
                {
                    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                    const auto& r = p.valueRange;
                    knob.setNormalisableRange ({ (double) r.start, (double) r.end, (double) r.interval, (double) r.skew });

                    if (const auto def = p.getDefaultValue())
                        knob.setDoubleClickReturnValue (true, (double) *def);

                    knob.setVelocityModeParameters (0.3, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Shift: fine
                    knob.setPopupDisplayEnabled (true, true, nullptr);
                    knob.textFromValueFunction = [this] (double v) { return param != nullptr ? param->valueToString ((float) v) : juce::String(); };
                    knob.onDragStart = [this]
                    {
                        if (param == nullptr)
                            return;

                        param->getEdit().getUndoManager().beginNewTransaction();   // one drag, one undo step
                        beginGesture (*param);                                      // Write records between begin and end
                        gesture = true;
                    };
                    knob.onDragEnd = [this] { endGesture(); refresh(); };
                    knob.onValueChange = [this]
                    {
                        if (param == nullptr)
                            return;

                        if (isLocked (*param))
                            return refresh();   // the lane wins

                        if (! gesture)
                            param->getEdit().getUndoManager().beginNewTransaction();   // a wheel step or double-click

                        if (std::abs (param->getCurrentValue() - (float) knob.getValue()) > 1.0e-6f)
                            markEdited (*param);   // only a real change renames a Vocal Chain preset

                        param->setParameter ((float) knob.getValue(), juce::sendNotification);
                        value.setText (param->valueToString ((float) knob.getValue()), juce::dontSendNotification);
                    };
                    knob.setTooltip (p.getParameterName() + ": drag (Shift: fine) or scroll; double-click: default; double-click the number to type; right-click for more");
                    knob.onMenu = [this] { showMenu(); };
                    addAndMakeVisible (knob);
                }

                refresh();
            }

            ~Control() override         { endGesture(); }

            void refresh()
            {
                if (param == nullptr)
                    return;

                const auto now = param->getCurrentValue();
                const bool locked = isLocked (*param);

                if (isToggle (*param))
                {
                    toggle.setToggleState (now >= 0.5f, juce::dontSendNotification);
                    toggle.setEnabled (! locked);
                    return;
                }

                if (isSwitch (*param))
                {
                    choice.setSelectedItemIndex (juce::roundToInt (now - param->valueRange.start), juce::dontSendNotification);
                    choice.setEnabled (! locked);
                }
                else
                {
                    if (! knob.isMouseButtonDown())
                        knob.setValue (now, juce::dontSendNotification);

                    knob.setColour (juce::Slider::rotarySliderFillColourId, locked ? Theme::textDim : Theme::accent);
                }

                if (! value.isBeingEdited())
                    value.setText (param->valueToString (now) + (locked ? " (lane)" : ""), juce::dontSendNotification);
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                if (e.mods.isPopupMenu())
                    showMenu();
            }

            void showMenu()
            {
                if (param == nullptr)
                    return;

                juce::PopupMenu menu;
                menu.addSectionHeader (param->getParameterName() + ": " + param->getCurrentValueAsString());
                menu.addItem (1, "Back to default (double-click)", param->getDefaultValue().has_value() && ! isLocked (*param));
                menu.addItem (2, "Type a value...", ! isLocked (*param) && ! isSwitch (*param));
                juce::Component::SafePointer<Control> safe (this);
                menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe] (int result)
                {
                    if (safe == nullptr || safe->param == nullptr)
                        return;

                    if (result == 1)
                    {
                        safe->param->getEdit().getUndoManager().beginNewTransaction();
                        markEdited (*safe->param);
                        safe->param->setParameter (*safe->param->getDefaultValue(), juce::sendNotification);
                        safe->refresh();
                    }
                    else if (result == 2)
                    {
                        safe->value.showEditor();
                    }
                });
            }

            enum class Kind { knob, toggle, choice };
            Kind getKind() const     { return isToggle (*param) ? Kind::toggle : isSwitch (*param) ? Kind::choice : Kind::knob; }

            /** The width this control wants in its section's flow. */
            int getIdealWidth() const
            {
                switch (getKind())
                {
                    case Kind::toggle:  return juce::GlyphArrangement::getStringWidthInt (Theme::font (Theme::baseFontSize), toggle.getButtonText()) + 28;
                    case Kind::choice:  return 196;
                    case Kind::knob:    break;
                }

                return 76;
            }

            void resized() override
            {
                auto r = getLocalBounds();

                switch (getKind())
                {
                    case Kind::toggle:
                        toggle.setBounds (r);
                        break;

                    case Kind::choice:
                        name.setBounds (r.removeFromLeft (70));
                        choice.setBounds (r.reduced (0, 1));
                        break;

                    case Kind::knob:
                    {
                        r.reduce (2, 0);
                        name.setBounds (r.removeFromTop (16));
                        value.setBounds (r.removeFromBottom (16));
                        const auto size = juce::jmin (r.getWidth(), r.getHeight(), 50);
                        knob.setBounds (r.withSizeKeepingCentre (size, size));
                        break;
                    }
                }
            }

        private:
            te::AutomatableParameter::Ptr param;   // ref-counted: the plugin may go while the window shows
            juce::Label name, value;
            MenuSlider knob;
            juce::ComboBox choice;
            MenuTextButton toggle;
            bool gesture = false;

            void endGesture()
            {
                if (std::exchange (gesture, false) && param != nullptr)
                    param->parameterChangeGestureEnd();
            }
        };

        //==============================================================================
        class Content : public juce::Component, private juce::Timer
        {
        public:
            explicit Content (te::Plugin& p) : plugin (&p)
            {
                onButton.setClickingTogglesState (false);
                onButton.setTooltip ("On: the effect is working. Off: bypassed, the sound passes untouched");
                onButton.onClick = [this]
                {
                    if (plugin == nullptr)
                        return;

                    plugin->edit.getUndoManager().beginNewTransaction();
                    plugin->setEnabled (! plugin->isEnabled());   // (the Devices panel notices within a second)
                    refresh();
                };
                addAndMakeVisible (onButton);

                for (auto* param : p.getAutomatableParameters())
                    addAndMakeVisible (controls.add (new Control (*param)));

                // Wide enough for the knobs (up to 6 a row) and an option menu side by side
                int knobs = 0;

                for (auto* c : controls)
                    knobs += c->getKind() == Control::Kind::knob ? 1 : 0;

                const auto width = juce::jmax (300, juce::jlimit (1, 6, juce::jmax (knobs, 3)) * knobWidth + 2 * margin);
                setSize (width, layout (width, false));
                refresh();
                startTimerHz (15);   // follows automation and undo
            }

            void paint (juce::Graphics& g) override
            {
                g.fillAll (Theme::panel);
                g.setColour (Theme::laneSeparator);
                g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

                g.setColour (plugin != nullptr && plugin->isEnabled() ? Theme::text : Theme::textDim);
                g.setFont (Theme::font (Theme::baseFontSize));
                g.drawText (plugin != nullptr && ! plugin->isEnabled() ? "Bypassed: the sound passes untouched" : "On",
                            onButton.getBounds().withX (onButton.getRight() + 8).withRight (getWidth() - margin), juce::Justification::centredLeft, true);

                g.setColour (Theme::textDim);
                g.setFont (Theme::font (Theme::captionFontSize - 0.5f, true));

                for (const auto& [area, text] : captions)
                    g.drawText (text, area, juce::Justification::centredLeft, false);

                if (controls.isEmpty())
                    g.drawText ("This effect has no controls", getLocalBounds().withTrimmedTop (headerHeight), juce::Justification::centred);
            }

            void resized() override
            {
                onButton.setBounds (margin, (headerHeight - 24) / 2, 24, 24);
                layout (getWidth(), true);
            }

        private:
            static constexpr int margin = 14, headerHeight = 40, knobWidth = 76, knobHeight = 88, rowHeight = 26, gap = 8;
            te::Plugin* plugin;   // Tracktion closes this window when the plugin goes (never a Ptr: that would keep it alive)
            IconButton onButton { "On", Icon::power, {}, IconButton::Style::ghost };
            juce::OwnedArray<Control> controls;
            std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;

            /** Knobs in a grid, then switches as buttons that light up, then option menus; returns the height. */
            int layout (int width, bool place)
            {
                captions.clear();
                int y = headerHeight + 10;
                const auto inner = width - 2 * margin;

                const auto section = [&] (Control::Kind kind, const juce::String& caption)
                {
                    juce::Array<Control*> items;

                    for (auto* c : controls)
                        if (c->getKind() == kind)
                            items.add (c);

                    if (items.isEmpty())
                        return;

                    if (caption.isNotEmpty())
                    {
                        captions.push_back ({ { margin, y, inner, 14 }, caption });
                        y += 18;
                    }

                    int x = 0;
                    const auto h = kind == Control::Kind::knob ? knobHeight : rowHeight;
                    const auto columns = juce::jmax (1, inner / knobWidth);

                    for (int i = 0; i < items.size(); ++i)
                    {
                        auto w = kind == Control::Kind::knob ? inner / juce::jmin (columns, items.size()) : items[i]->getIdealWidth();

                        if (x > 0 && x + w > inner)
                        {
                            x = 0;
                            y += h + (kind == Control::Kind::knob ? 4 : gap);
                        }

                        if (place)
                            items[i]->setBounds (margin + x, y, juce::jmin (w, inner), h);

                        x += w + (kind == Control::Kind::knob ? 0 : gap);
                    }

                    y += h + 14;
                };

                section (Control::Kind::knob, {});
                section (Control::Kind::toggle, "SWITCHES");
                section (Control::Kind::choice, "OPTIONS");
                return y;
            }

            void refresh()
            {
                if (plugin == nullptr)
                    return;

                const auto on = plugin->isEnabled();

                if (on != onButton.getToggleState())
                {
                    onButton.setToggleState (on, juce::dontSendNotification);
                    repaint();
                }

                for (auto* c : controls)
                    c->refresh();
            }

            void timerCallback() override   { refresh(); }
        };

        //==============================================================================
        class Window : public juce::DocumentWindow
        {
        public:
            explicit Window (te::Plugin& p)
                : juce::DocumentWindow (p.getName(), Theme::panel, juce::DocumentWindow::closeButton), plugin (&p)
            {
                setUsingNativeTitleBar (false);   // our dark title bar, not the system's white one
                setTitleBarHeight (28);
                auto content = ParamEditor::createContent (p);
                const auto contentSize = content->getBounds();

                // Instruments can have dozens of controls: scroll past what fits on screen
                const auto maxHeight = 640;

                if (contentSize.getHeight() > maxHeight)
                {
                    auto viewport = std::make_unique<juce::Viewport>();
                    viewport->setScrollBarsShown (true, false);
                    viewport->setViewedComponent (content.release(), true);
                    viewport->setSize (contentSize.getWidth() + viewport->getScrollBarThickness(), maxHeight);
                    setContentOwned (viewport.release(), true);
                    setResizable (true, false);
                    setResizeLimits (contentSize.getWidth(), 200, contentSize.getWidth() + 40, contentSize.getHeight() + 40);
                }
                else
                {
                    setContentOwned (content.release(), true);
                    setResizable (false, false);
                }

                setTopLeftPosition (p.windowState->choosePositionForPluginWindow());
                setVisible (true);
            }

            ~Window() override
            {
                if (plugin != nullptr)
                    plugin->edit.flushPluginStateIfNeeded (*plugin);
            }

            bool keyPressed (const juce::KeyPress& key) override
            {
                if (key == juce::KeyPress::escapeKey)
                {
                    closeButtonPressed();   // Esc closes, like the other windows
                    return true;
                }

                return juce::DocumentWindow::keyPressed (key);
            }

            void closeButtonPressed() override
            {
                if (plugin != nullptr)
                    plugin->windowState->closeWindowExplicitly();   // destroys this window
            }

        private:
            te::Plugin* plugin;   // Tracktion closes this window when the plugin goes (never a Ptr: that would keep it alive)
        };
    }

    std::unique_ptr<juce::Component> ParamEditor::createContent (te::Plugin& plugin)
    {
        return std::make_unique<Content> (plugin);
    }

    std::unique_ptr<juce::Component> ParamEditor::createWindow (te::Plugin& plugin)
    {
        return std::make_unique<Window> (plugin);
    }
}
