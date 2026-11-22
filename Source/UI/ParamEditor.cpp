/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "ParamEditor.h"
#include "PluginLayouts.h"
#include "Theme.h"
#include "Widgets.h"
#include "PluginLook.h"
#include "../Engine/Effects.h"
#include "../Engine/PluginPresets.h"
#include "../Engine/VocalChain.h"

namespace ez
{
    namespace
    {
        using namespace PluginLayouts;

        //==============================================================================
        // Parameter helpers (automation, Write, the Vocal Chain's "(edited)")

        bool isSwitch (const te::AutomatableParameter& p)
        {
            const auto& r = p.valueRange;
            return r.interval >= 1.0f && r.end - r.start <= 11.0f;
        }

        bool isToggleParam (te::AutomatableParameter& p)
        {
            if (isSwitch (p))
                return p.valueRange.end - p.valueRange.start == 1.0f && p.valueToString (p.valueRange.start).equalsIgnoreCase ("off");

            // Tracktion's reverb Freeze is a 0..1 knob that reads "Off"/"On"
            return p.valueToString (p.valueRange.start).equalsIgnoreCase ("off") && p.valueToString (p.valueRange.end).equalsIgnoreCase ("on");
        }

        void markEdited (te::AutomatableParameter& p)
        {
            if (auto* chain = dynamic_cast<VocalChainPlugin*> (p.getPlugin()))
                if (p.paramID != "root" && p.paramID != "scale" && ! chain->getPresetName().endsWith (" (edited)"))
                    chain->setPresetName (chain->getPresetName() + " (edited)");
        }

        void beginParamGesture (te::AutomatableParameter& p)
        {
            auto& arm = p.getEdit().getAutomationRecordManager();

            if (auto* track = p.getTrack(); track != nullptr && arm.isWritingAutomation()
                 && track->state[te::IDs::automationMode].toString() != te::toString (te::AutomationMode::touch))
                track->state.setProperty (te::IDs::automationMode, te::toString (te::AutomationMode::touch), nullptr);

            p.parameterChangeGestureBegin();
        }

        bool isParamLocked (te::AutomatableParameter& p)
        {
            auto& edit = p.getEdit();
            auto& arm = edit.getAutomationRecordManager();
            return p.hasAutomationPoints() && arm.isReadingAutomation() && ! (arm.isWritingAutomation() && edit.getTransport().isPlaying());
        }

        //==============================================================================
        /** One value a control or display shows and changes: a parameter or a setting. */
        class Source
        {
        public:
            virtual ~Source() = default;

            virtual juce::String getName() const = 0;
            virtual float get() const = 0;
            virtual juce::String toText (float) const = 0;
            virtual float fromText (const juce::String&) const = 0;
            virtual juce::NormalisableRange<float> getRange() const = 0;
            virtual std::optional<float> getDefault() const = 0;
            virtual juce::StringArray getChoices() const = 0;          // empty: a continuous value
            virtual float valueForChoice (int index) const = 0;
            virtual int choiceForValue (float) const = 0;
            virtual bool isToggle() const = 0;
            virtual bool isLocked() const                               { return false; }
            virtual bool isAutomatable() const                          { return false; }

            /** A change from the user: begin (a new undo step), set as often as needed, end. */
            virtual void begin() = 0;
            virtual void set (float) = 0;
            virtual void end() = 0;

            void setOnce (float value)  { begin(); set (value); end(); }

            std::function<void()> onUserChange;   // the editor marks its preset "edited"
        };

        class ParamSource : public Source
        {
        public:
            explicit ParamSource (te::AutomatableParameter& p) : param (&p) {}
            ~ParamSource() override     { end(); }

            juce::String getName() const override                   { return param->getParameterName(); }
            float get() const override                              { return param->getCurrentValue(); }
            juce::String toText (float v) const override            { return PluginLayouts::tidyValueText (param->valueToString (v)); }
            float fromText (const juce::String& typed) const override
            {
                // undo tidyValueText's unit changes so the plug-in parses what the user typed
                auto t = typed.trim();
                const auto rawUnit = param->valueToString (param->getCurrentValue()).trimEnd();

                if (t.equalsIgnoreCase ("C"))
                    t = "0";
                else if (t.endsWithIgnoreCase ("khz") || (t.endsWithIgnoreCase ("k") && rawUnit.endsWith ("Hz")))
                    t = juce::String (t.getDoubleValue() * 1000.0);
                else if (rawUnit.endsWith ("ms") && t.endsWithIgnoreCase ("s") && ! t.endsWithIgnoreCase ("ms"))
                    t = juce::String (t.getDoubleValue() * 1000.0);

                return param->valueRange.snapToLegalValue (param->stringToValue (t));
            }
            std::optional<float> getDefault() const override        { return param->getDefaultValue(); }
            bool isToggle() const override                          { return isToggleParam (*param); }
            bool isLocked() const override                          { return isParamLocked (*param); }
            bool isAutomatable() const override                     { return true; }

            juce::NormalisableRange<float> getRange() const override
            {
                const auto& r = param->valueRange;
                return { r.start, r.end, r.interval, r.skew };
            }

            juce::StringArray getChoices() const override
            {
                juce::StringArray names;

                if (isSwitch (*param))
                    for (int i = 0; i <= juce::roundToInt (param->valueRange.end - param->valueRange.start); ++i)
                        names.add (param->valueToString (param->valueRange.start + (float) i));
                else if (isToggle())
                    names = { "Off", "On" };

                return names;
            }

            float valueForChoice (int index) const override
            {
                if (isToggle() && ! isSwitch (*param))
                    return index > 0 ? param->valueRange.end : param->valueRange.start;

                return param->valueRange.start + (float) index;
            }

            int choiceForValue (float v) const override
            {
                if (isToggle() && ! isSwitch (*param))
                    return v >= 0.5f * (param->valueRange.start + param->valueRange.end) ? 1 : 0;

                return juce::roundToInt (v - param->valueRange.start);
            }

            void begin() override
            {
                param->getEdit().getUndoManager().beginNewTransaction();
                beginParamGesture (*param);
                gesture = true;
            }

            void set (float v) override
            {
                if (isLocked())
                    return;   // the lane wins

                if (std::abs (param->getCurrentValue() - v) > 1.0e-6f)
                {
                    markEdited (*param);

                    if (onUserChange)
                        onUserChange();
                }

                param->setParameter (param->valueRange.snapToLegalValue (v), juce::sendNotification);
            }

            void end() override
            {
                if (std::exchange (gesture, false))
                    param->parameterChangeGestureEnd();
            }

        private:
            te::AutomatableParameter::Ptr param;   // ref-counted: the plugin may go while the window shows
            bool gesture = false;
        };

        class SettingSource : public Source
        {
        public:
            SettingSource (te::Plugin& p, const Setting& s) : plugin (&p), setting (s), id (s.id) {}

            juce::String getName() const override                   { return setting.name; }
            std::optional<float> getDefault() const override        { return setting.defaultValue; }
            bool isToggle() const override                          { return setting.isToggle; }
            juce::StringArray getChoices() const override           { return setting.labels; }

            float get() const override
            {
                const auto v = plugin->state[id];

                if (v.isVoid())
                    return setting.defaultValue;

                if (! setting.textValues.isEmpty())
                    return (float) juce::jmax (0, setting.textValues.indexOf (v.toString()));

                return (float) v;
            }

            juce::NormalisableRange<float> getRange() const override
            {
                juce::NormalisableRange<float> r (setting.min, setting.max, setting.interval);

                if (setting.skewCentre > setting.min && setting.skewCentre < setting.max)
                    r.setSkewForCentre (setting.skewCentre);

                return r;
            }

            juce::String toText (float v) const override
            {
                if (! setting.labels.isEmpty())
                    return setting.labels[juce::jlimit (0, setting.labels.size() - 1, choiceForValue (v))];

                return juce::String (v * setting.displayScale, setting.decimals) + (setting.unit.isNotEmpty() ? " " + setting.unit : juce::String());
            }

            float fromText (const juce::String& t) const override
            {
                return juce::jlimit (setting.min, setting.max, t.retainCharacters ("0123456789.-").getFloatValue() / setting.displayScale);
            }

            float valueForChoice (int index) const override
            {
                if (! setting.choiceValues.empty())
                    return setting.choiceValues[(size_t) juce::jlimit (0, (int) setting.choiceValues.size() - 1, index)];

                return setting.min + (float) index;
            }

            int choiceForValue (float v) const override
            {
                if (! setting.choiceValues.empty())
                {
                    int best = 0;

                    for (int i = 1; i < (int) setting.choiceValues.size(); ++i)
                        if (std::abs (setting.choiceValues[(size_t) i] - v) < std::abs (setting.choiceValues[(size_t) best] - v))
                            best = i;

                    return best;
                }

                return juce::roundToInt (v - setting.min);
            }

            void begin() override       { plugin->edit.getUndoManager().beginNewTransaction(); }
            void end() override         {}

            void set (float v) override
            {
                auto* um = &plugin->edit.getUndoManager();
                juce::var value;

                if (! setting.textValues.isEmpty())
                    value = setting.textValues[juce::jlimit (0, setting.textValues.size() - 1, juce::roundToInt (v))];
                else if (! setting.choiceValues.empty())
                    value = std::abs (v - std::round (v)) < 1.0e-6f ? juce::var (juce::roundToInt (v)) : juce::var ((double) v);
                else if (setting.interval >= 1.0f || ! setting.labels.isEmpty())
                    value = juce::roundToInt (v);   // the plugin keeps these as whole numbers
                else
                    value = (double) juce::jlimit (setting.min, setting.max, v);

                if (plugin->state[id] != value)
                {
                    plugin->state.setProperty (id, value, um);

                    if (onUserChange)
                        onUserChange();
                }
            }

        private:
            te::Plugin* plugin;
            Setting setting;
            juce::Identifier id;
        };

        //==============================================================================
        namespace P = Theme::Plugin;

        juce::String utf8 (const char* text)    { return juce::String (juce::CharPointer_UTF8 (text)); }

        /** What controls and cards need from the editor. */
        struct Context
        {
            virtual ~Context() = default;
            virtual Source* findSource (const juce::String& id) const = 0;
            /** A modulated control's range, 0..1 of its knob, or nothing. */
            virtual std::optional<std::pair<float, float>> modRange (const juce::String& id) const = 0;
        };

        /** A custom card body (the routing matrix, note buttons, amp types, the cabinet). */
        struct Body : public juce::Component
        {
            virtual int getIdealHeight (int width) const = 0;
            virtual void refresh() {}
        };

        juce::Colour signalColour (Section::Signal s)
        {
            switch (s)
            {
                case Section::Signal::envelope:     return P::env;
                case Section::Signal::modulation:   return P::mod;
                case Section::Signal::dynamics:     return P::gainReduction;
                default:                            return P::signal;
            }
        }

        //==============================================================================
        /** The round power switch on cards and in the header. */
        class PowerButton : public juce::Button
        {
        public:
            PowerButton() : juce::Button ("Power")  { setClickingTogglesState (false); }

            void paintButton (juce::Graphics& g, bool over, bool) override
            {
                PluginLook::drawPower (g, getLocalBounds().toFloat(), getToggleState(), over);
            }
        };

        //==============================================================================
        /** A knob, a toggle, a menu, a row of buttons, a stepper or a fader for one Source. */
        class Control : public juce::Component
        {
        public:
            using Size = Item::Size;
            using Style = Item::Style;

            Control (Source& s, const Item& itemToUse, Context& c) : source (s), item (itemToUse), context (c)
            {
                const auto choices = s.getChoices();
                kind = item.style != Style::automatic ? item.style
                     : s.isToggle() ? Style::toggle
                     : ! choices.isEmpty() ? (choices.size() <= 4 ? Style::buttons : Style::menu)
                     : Style::knob;

                if ((kind == Style::menu || kind == Style::buttons) && choices.isEmpty())
                    kind = Style::knob;

                name.setText (item.label.isNotEmpty() ? item.label : s.getName(), juce::dontSendNotification);
                name.setJustificationType (juce::Justification::centred);
                name.setFont (Theme::font (Theme::smallFontSize, Theme::Weight::medium));
                name.setColour (juce::Label::textColourId, Theme::textDim);
                name.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (name);

                tip = s.getName() + (s.isAutomatable() ? "" : " (a setting: not automatable)");

                if (kind == Style::toggle)          buildToggle();
                else if (kind == Style::menu)       buildMenu (choices);
                else if (kind == Style::buttons)    buildSegments (choices);
                else if (kind == Style::stepper)    buildStepper();
                else if (kind == Style::fader)      buildFader();
                else                                buildKnob();

                refresh();
            }

            void refresh()
            {
                const auto now = source.get();
                const bool locked = source.isLocked();

                if (kind == Style::toggle)
                {
                    toggle.setToggleState (isOn (now), juce::dontSendNotification);
                    toggle.setEnabled (! locked);
                }
                else if (kind == Style::menu)
                {
                    combo.setSelectedItemIndex (source.choiceForValue (now), juce::dontSendNotification);
                    combo.setEnabled (! locked);
                }
                else if (kind == Style::buttons)
                {
                    for (int i = 0; i < segments.size(); ++i)
                    {
                        segments[i]->setToggleState (i == source.choiceForValue (now), juce::dontSendNotification);
                        segments[i]->setEnabled (! locked);
                    }
                }
                else if (kind == Style::stepper)
                {
                    value.setText (valueText (now), juce::dontSendNotification);
                    minus.setEnabled (! locked);
                    plus.setEnabled (! locked);
                }
                else
                {
                    auto& slider = kind == Style::fader ? fader : knob;

                    if (! slider.isMouseButtonDown())
                        slider.setValue (now, juce::dontSendNotification);

                    auto& props = slider.getProperties();
                    const auto changed = (bool) props["ezAutomated"] != locked || (bool) props["ezOff"] != ! moduleActive;
                    props.set ("ezAutomated", locked);
                    props.set ("ezOff", ! moduleActive);

                    if (const auto mod = context.modRange (item.id))
                    {
                        if ((double) props["ezModLo"] != (double) mod->first || (double) props["ezModHi"] != (double) mod->second || ! props.contains ("ezModLo"))
                        {
                            props.set ("ezModLo", (double) mod->first);
                            props.set ("ezModHi", (double) mod->second);
                            slider.repaint();
                        }
                    }
                    else if (props.contains ("ezModLo"))
                    {
                        props.remove ("ezModLo");
                        props.remove ("ezModHi");
                        slider.repaint();
                    }

                    if (changed)
                        slider.repaint();

                    if (! value.isBeingEdited())
                        value.setText (valueText (now), juce::dontSendNotification);

                    value.setColour (juce::Label::textColourId, locked ? P::gainReduction : moduleActive ? Theme::text : Theme::textFaint);
                    slider.setTooltip (locked ? name.getText() + ": automation is playing it (an amber dot). Stop the lane to change it by hand" : sliderTip);
                }

                // Controls that only matter in some settings dim when they don't (and say why)
                const auto enabledNow = isEnabledBySetting();

                if (enabledNow != wasEnabledBySetting)
                {
                    wasEnabledBySetting = enabledNow;
                    setAlpha (enabledNow ? 1.0f : 0.38f);
                    applyTooltip (enabledNow ? juce::String() : item.disabledTip);
                }
            }

            /** The card is on or off: arcs and values go grey, everything stays usable. */
            void setModuleActive (bool shouldBeActive)
            {
                if (moduleActive == shouldBeActive)
                    return;

                moduleActive = shouldBeActive;
                name.setColour (juce::Label::textColourId, moduleActive ? Theme::textDim : Theme::textFaint);
                refresh();
                repaint();
            }

            /** Grid columns this control spans. */
            int getColumnSpan (int columns) const
            {
                if (item.span > 0)
                    return juce::jmin (columns, item.span);

                if (kind == Style::buttons)
                    return juce::jmin (columns, juce::jmax (2, segments.size()));

                if (kind == Style::menu || kind == Style::fader || kind == Style::stepper)
                    return juce::jmin (columns, 2);

                if (kind == Style::toggle)
                    return juce::jmin (columns, toggle.getButtonText().length() > 7 ? 2 : 1);

                return 1;
            }

            int getIdealHeight() const
            {
                if (kind == Style::knob)
                    return knobSize() + 38;

                return kind == Style::toggle ? 34 : 50;   // the rest carry their name above
            }

            bool isKnob() const     { return kind == Style::knob; }

            void resized() override
            {
                auto r = getLocalBounds();

                if (kind == Style::toggle)
                {
                    toggle.setBounds (r.withSizeKeepingCentre (r.getWidth() - 6, 26));
                }
                else if (kind == Style::menu)
                {
                    name.setBounds (r.removeFromTop (18).reduced (3, 0));
                    combo.setBounds (r.removeFromTop (28).reduced (3, 0));
                }
                else if (kind == Style::buttons)
                {
                    name.setBounds (r.removeFromTop (18).reduced (3, 0));
                    auto row = r.removeFromTop (28).reduced (3, 0);
                    const auto w = row.getWidth() / juce::jmax (1, segments.size());

                    for (int i = 0; i < segments.size(); ++i)
                        segments[i]->setBounds (i + 1 == segments.size() ? row : row.removeFromLeft (w));
                }
                else if (kind == Style::stepper)
                {
                    name.setBounds (r.removeFromTop (18).reduced (3, 0));
                    auto row = r.removeFromTop (28).reduced (3, 0);
                    minus.setBounds (row.removeFromLeft (28));
                    plus.setBounds (row.removeFromRight (28));
                    value.setBounds (row);
                }
                else if (kind == Style::fader)
                {
                    auto top = r.removeFromTop (18).reduced (3, 0);
                    value.setBounds (top.removeFromRight (top.getWidth() / 2));
                    name.setBounds (top);
                    fader.setBounds (r.removeFromTop (28).reduced (3, 0));
                }
                else
                {
                    r.reduce (2, 0);
                    name.setBounds (r.removeFromTop (17));
                    value.setBounds (r.removeFromBottom (17));
                    const auto size = juce::jmin (r.getWidth(), r.getHeight(), knobSize());
                    knob.setBounds (r.withSizeKeepingCentre (size, size));
                }
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                if (e.mods.isPopupMenu())
                    showMenu();
            }

            void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
            {
                if (kind == Style::stepper && wheel.deltaY != 0.0f)
                    step (wheel.deltaY > 0 ? 1 : -1);
                else
                    juce::Component::mouseWheelMove (e, wheel);
            }

        private:
            Source& source;
            Item item;
            Context& context;
            Style kind = Style::knob;
            juce::Label name, value;
            MenuSlider knob, fader;
            juce::ComboBox combo;
            MenuTextButton toggle;
            juce::TextButton minus { "-" }, plus { "+" };
            juce::OwnedArray<juce::TextButton> segments;
            juce::String tip, sliderTip;
            bool dragging = false, moduleActive = true, wasEnabledBySetting = true;

            int knobSize() const
            {
                switch (item.size)
                {
                    case Size::small:   return P::knobSmall;
                    case Size::large:   return P::knobLarge;
                    case Size::hero:    return P::knobHero;
                    default:            return P::knobNormal;
                }
            }

            juce::String valueText (float v) const
            {
                auto text = source.toText (v);

                if (item.unit.isNotEmpty() && ! text.containsAnyOf ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ%"))
                    text << " " << item.unit;

                return text;
            }

            bool isEnabledBySetting() const
            {
                if (item.enabledBy.isEmpty())
                    return true;

                if (auto* s = context.findSource (item.enabledBy))
                {
                    const auto v = s->get();
                    const bool atLeast = v >= item.enabledFrom - 1.0e-4f;
                    return item.enabledBelow ? ! atLeast : (atLeast && v <= item.enabledUpTo + 1.0e-4f);
                }

                return true;
            }

            void applyTooltip (const juce::String& reason)
            {
                const auto add = reason.isNotEmpty() ? " (" + reason + ")" : juce::String();
                toggle.setTooltip (tip + ": click to switch it on or off" + add);
                combo.setTooltip (tip + add);
                minus.setTooltip (tip + ": one less (or scroll)" + add);
                plus.setTooltip (tip + ": one more (or scroll)" + add);
                sliderTip = tip + ": drag (Shift: fine) or scroll; double-click: default; double-click the number to type; right-click for more" + add;
                knob.setTooltip (sliderTip);
                fader.setTooltip (sliderTip);

                for (auto* b : segments)
                    b->setTooltip (tip + ": " + b->getButtonText() + add);
            }

            /** On for a toggle; a continuous control used as a switch is on above its bottom (ping-pong). */
            bool isOn (float v) const
            {
                if (! source.getChoices().isEmpty())
                    return source.choiceForValue (v) > 0;

                const auto r = source.getRange();
                return v > r.start + (r.end - r.start) * 0.01f;
            }

            float onValue() const
            {
                if (! source.getChoices().isEmpty())
                    return source.valueForChoice (1);

                const auto r = source.getRange();
                const auto def = source.getDefault();
                return def.has_value() && *def > r.start + (r.end - r.start) * 0.01f ? *def : r.end;
            }

            float offValue() const
            {
                return source.getChoices().isEmpty() ? source.getRange().start : source.valueForChoice (0);
            }

            void buildToggle()
            {
                toggle.setButtonText (name.getText());
                toggle.setClickingTogglesState (false);
                toggle.getProperties().set ("ezToggle", true);
                toggle.onClick = [this]
                {
                    if (! source.isLocked())
                        source.setOnce (isOn (source.get()) ? offValue() : onValue());

                    refresh();
                };
                toggle.onMenu = [this] { showMenu(); };
                addAndMakeVisible (toggle);
                name.setVisible (false);
                applyTooltip ({});
            }

            void buildMenu (const juce::StringArray& choices)
            {
                for (int i = 0; i < choices.size(); ++i)
                    combo.addItem (choices[i], i + 1);

                combo.onChange = [this]
                {
                    if (! source.isLocked() && combo.getSelectedItemIndex() >= 0)
                        source.setOnce (source.valueForChoice (combo.getSelectedItemIndex()));

                    refresh();
                };
                addAndMakeVisible (combo);
                name.setJustificationType (juce::Justification::centredLeft);
                applyTooltip ({});
            }

            void buildSegments (const juce::StringArray& choices)
            {
                for (int i = 0; i < choices.size(); ++i)
                {
                    auto* b = segments.add (new juce::TextButton (choices[i]));
                    b->setClickingTogglesState (false);
                    b->getProperties().set ("ezSegment", true);

                    if (i < item.icons.size())
                        b->getProperties().set ("ezIcon", item.icons[i]);

                    b->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i + 1 < choices.size() ? juce::Button::ConnectedOnRight : 0));
                    b->onClick = [this, i]
                    {
                        if (! source.isLocked())
                            source.setOnce (source.valueForChoice (i));

                        refresh();
                    };
                    addAndMakeVisible (b);
                }

                name.setJustificationType (juce::Justification::centredLeft);
                applyTooltip ({});
            }

            void step (int delta)
            {
                if (source.isLocked())
                    return;

                const auto choices = source.getChoices();

                if (! choices.isEmpty())
                {
                    const auto index = juce::jlimit (0, choices.size() - 1, source.choiceForValue (source.get()) + delta);
                    source.setOnce (source.valueForChoice (index));
                }
                else
                {
                    const auto r = source.getRange();
                    source.setOnce (juce::jlimit (r.start, r.end, source.get() + (float) delta * juce::jmax (1.0f, r.interval)));
                }

                refresh();
            }

            void buildStepper()
            {
                for (auto* b : { &minus, &plus })
                {
                    b->getProperties().set ("ezSegment", true);
                    addAndMakeVisible (b);
                }

                minus.setConnectedEdges (juce::Button::ConnectedOnRight);
                plus.setConnectedEdges (juce::Button::ConnectedOnLeft);
                minus.onClick = [this] { step (-1); };
                plus.onClick = [this] { step (1); };

                value.setJustificationType (juce::Justification::centred);
                value.setFont (Theme::font (Theme::baseFontSize, Theme::Weight::semibold));
                value.setColour (juce::Label::textColourId, Theme::text);
                value.setColour (juce::Label::backgroundColourId, P::recess);
                value.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (value);
                name.setJustificationType (juce::Justification::centredLeft);
                applyTooltip ({});
            }

            void setUpSlider (MenuSlider& slider)
            {
                const auto range = source.getRange();
                slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                slider.setNormalisableRange ({ (double) range.start, (double) range.end, (double) range.interval, (double) range.skew });

                if (const auto def = source.getDefault())
                {
                    slider.setDoubleClickReturnValue (true, (double) *def);

                    // gain, pan, tune and the like grow both ways from a default in the middle of the range
                    if (std::abs (range.convertTo0to1 (*def) - 0.5f) < 0.05f && range.start < 0.0f && range.end > 0.0f)
                        slider.getProperties().set ("ezDefault", (double) *def);
                }

                slider.setVelocityModeParameters (0.3, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Shift: fine
                slider.setPopupDisplayEnabled (true, true, nullptr);   // the value bubble while dragging
                slider.textFromValueFunction = [this] (double v) { return valueText ((float) v); };
                slider.onDragStart = [this] { source.begin(); dragging = true; value.setAlpha (0.35f); };
                slider.onDragEnd = [this] { source.end(); dragging = false; value.setAlpha (1.0f); refresh(); };
                slider.onValueChange = [this, &slider]
                {
                    if (source.isLocked())
                        return refresh();

                    if (dragging)
                        source.set ((float) slider.getValue());
                    else
                        source.setOnce ((float) slider.getValue());   // a wheel step or double-click: its own undo step

                    value.setText (valueText ((float) slider.getValue()), juce::dontSendNotification);
                };
                slider.onMenu = [this] { showMenu(); };
                addAndMakeVisible (slider);

                value.setFont (Theme::font (Theme::smallFontSize + 0.5f, Theme::Weight::medium));
                value.setColour (juce::Label::textColourId, Theme::text);
                value.setEditable (false, true);   // double-click the number to type one
                value.onTextChange = [this]
                {
                    if (! source.isLocked())
                        source.setOnce (source.fromText (value.getText()));

                    refresh();
                };
                addAndMakeVisible (value);
                applyTooltip ({});
            }

            void buildKnob()
            {
                kind = Style::knob;
                knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                knob.getProperties().set ("ezHero", item.size == Size::hero);
                setUpSlider (knob);
                value.setJustificationType (juce::Justification::centred);
            }

            void buildFader()
            {
                fader.setSliderStyle (juce::Slider::LinearHorizontal);
                setUpSlider (fader);
                value.setJustificationType (juce::Justification::centredRight);
                name.setJustificationType (juce::Justification::centredLeft);
            }

            void showMenu()
            {
                juce::PopupMenu menu;
                menu.addSectionHeader (source.getName() + ": " + valueText (source.get()));
                menu.addItem (1, "Back to default\tDouble-click", source.getDefault().has_value() && ! source.isLocked());
                menu.addItem (2, "Type a value...", ! source.isLocked() && (kind == Style::knob || kind == Style::fader));
                menu.addSeparator();
                menu.addItem (3, "Show its automation lane (coming next)", false);
                menu.addItem (4, "MIDI learn (coming next)", false);
                juce::Component::SafePointer<Control> safe (this);
                menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe] (int result)
                {
                    if (safe == nullptr)
                        return;

                    if (result == 1)
                    {
                        safe->source.setOnce (*safe->source.getDefault());
                        safe->refresh();
                    }
                    else if (result == 2)
                    {
                        safe->value.showEditor();
                    }
                });
            }
        };

        //==============================================================================
        /** How much a routing moves its control: a bar that fills from the centre, both ways. */
        class AmountBar : public juce::Component, public juce::SettableTooltipClient
        {
        public:
            std::function<void (float)> onChange;

            AmountBar()
            {
                setTooltip ("How much: drag left or right (left moves it the other way). Shift: fine. Double-click: +50 %");
                setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            }

            void setAmount (float a)    { amount = juce::jlimit (-1.0f, 1.0f, a); repaint(); }

            void paint (juce::Graphics& g) override
            {
                const auto area = getLocalBounds().toFloat().reduced (0.5f);
                g.setColour (P::recess);
                g.fillRoundedRectangle (area, 5.0f);
                g.setColour (P::moduleEdgeLo);
                g.drawRoundedRectangle (area, 5.0f, 1.0f);

                const auto centre = area.getCentreX();
                const auto end = centre + amount * area.getWidth() * 0.5f;
                const auto fill = juce::Rectangle<float> (juce::jmin (centre, end), area.getY() + 2.0f, std::abs (end - centre), area.getHeight() - 4.0f);
                g.setGradientFill (juce::ColourGradient::horizontal (P::mod.withAlpha (amount >= 0.0f ? 0.25f : 0.75f), fill.getX(),
                                                                     P::mod.withAlpha (amount >= 0.0f ? 0.75f : 0.25f), fill.getRight()));
                g.fillRoundedRectangle (fill, 3.0f);
                g.setColour (Theme::textFaint.withAlpha (0.6f));
                g.drawVerticalLine (juce::roundToInt (centre), area.getY() + 3.0f, area.getBottom() - 3.0f);

                g.setColour (Theme::text);
                g.setFont (Theme::font (Theme::smallFontSize, Theme::Weight::semibold));
                g.drawText ((amount > 0.0f ? "+" : "") + juce::String (juce::roundToInt (amount * 100.0f)) + " %", area, juce::Justification::centred, false);
            }

            void mouseDown (const juce::MouseEvent&) override       { start = amount; }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                const auto fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
                set (start + (float) e.getDistanceFromDragStartX() * fine * 2.0f / (float) juce::jmax (1, getWidth()));
            }
            void mouseDoubleClick (const juce::MouseEvent&) override    { set (0.5f); }
            void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override    { set (amount + (w.deltaY > 0 ? 0.02f : -0.02f)); }

        private:
            float amount = 0.0f, start = 0.0f;

            void set (float a)
            {
                setAmount (std::round (juce::jlimit (-1.0f, 1.0f, a) * 100.0f) / 100.0f);

                if (onChange)
                    onChange (amount);
            }
        };

        //==============================================================================
        /** The synth's modulation routings: source -> destination, amount, one row each. */
        class ModMatrix : public Body
        {
        public:
            ModMatrix (te::FourOscPlugin& p, std::function<void()> changedCallback) : synth (&p), onChange (std::move (changedCallback))
            {
                addButton.setButtonText ("+ Add routing");
                addButton.getProperties().set ("ezToggle", true);
                addButton.getProperties().set ("ezLamp", (int) P::mod.getARGB());
                addButton.setTooltip ("Adds a routing (LFO 1 to the filter cut-off to start with); then pick the source, the destination and the amount");
                addButton.onClick = [this] { addRouting (sources()[0], synth->filterFreq.get()); };
                addAndMakeVisible (addButton);

                // one-click starting points for the empty state
                const std::tuple<const char*, te::FourOscPlugin::ModSource, te::AutomatableParameter*, float> starters[]
                {
                    { "LFO 1 to the filter cut-off (wobble)", te::FourOscPlugin::lfo1, synth->filterFreq.get(), 0.35f },
                    { "Mod env 1 to Osc 1 pitch (pluck)", te::FourOscPlugin::env1, synth->oscParams[0]->tune.get(), 0.2f },
                };

                for (const auto& [text, src, target, depth] : starters)
                {
                    auto* b = starterButtons.add (new juce::TextButton (text));
                    b->getProperties().set ("ezSegment", true);
                    b->setTooltip ("Adds this routing");
                    b->onClick = [this, s = src, t = target, d = depth] { addRouting (s, t, d); };
                    addChildComponent (b);
                }

                rebuild();
            }

            int getCount() const    { return rows.size(); }

            int getIdealHeight (int) const override
            {
                return rows.isEmpty() ? 150 : 22 + juce::jmax (3, rows.size()) * rowHeight + 40;
            }

            void rebuild()
            {
                rows.clear();

                for (auto* param : synth->getAutomatableParameters())
                    for (auto src : synth->getModulationSources (param))
                        addAndMakeVisible (rows.add (new Row (*this, src, param)));

                addButton.setEnabled (rows.size() < maxRows);

                for (auto* b : starterButtons)
                    b->setVisible (rows.isEmpty());

                resized();
                repaint();

                if (onCountChange)
                    onCountChange();
            }

            std::function<void()> onCountChange;

            void paint (juce::Graphics& g) override
            {
                auto area = getLocalBounds().toFloat();

                if (rows.isEmpty())
                {
                    // three ghost rows: what a routing will look like
                    auto ghosts = area.removeFromTop (3.0f * rowHeight);
                    const float dashes[] { 4.0f, 4.0f };

                    for (int i = 0; i < 3; ++i)
                    {
                        const auto row = ghosts.removeFromTop ((float) rowHeight).reduced (0.0f, 4.0f);
                        juce::Path outline;
                        outline.addRoundedRectangle (row, 6.0f);
                        juce::Path dashed;
                        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
                        g.setColour (Theme::textFaint.withAlpha (0.18f));
                        g.fillPath (dashed);
                    }

                    g.setColour (Theme::textDim);
                    g.setFont (Theme::font (Theme::baseFontSize, Theme::Weight::medium));
                    g.drawText ("Nothing moves yet. Route an LFO or an envelope to any control:", getLocalBounds().removeFromTop (3 * rowHeight).withTrimmedBottom (rowHeight + 6),
                                juce::Justification::centred, false);
                    return;
                }

                // column captions
                g.setColour (Theme::textFaint);
                g.setFont (Theme::font (Theme::captionFontSize, Theme::Weight::semibold));
                auto captions = getLocalBounds().removeFromTop (18);
                const auto cols = columns (captions.getWidth());
                g.drawText ("SOURCE", captions.withX (cols[0]).withWidth (cols[1] - cols[0]), juce::Justification::centredLeft, false);
                g.drawText ("DESTINATION", captions.withX (cols[2]).withWidth (cols[3] - cols[2]), juce::Justification::centredLeft, false);
                g.drawText ("AMOUNT", captions.withX (cols[4]).withWidth (cols[5] - cols[4]), juce::Justification::centredLeft, false);
            }

            void resized() override
            {
                auto r = getLocalBounds();
                auto bottom = r.removeFromBottom (32);
                addButton.setBounds (bottom.removeFromLeft (150).withSizeKeepingCentre (150, 28));

                if (rows.isEmpty())
                {
                    auto starters = getLocalBounds().removeFromTop (3 * rowHeight).withTrimmedTop (rowHeight + 10).removeFromTop (30);
                    const auto w = juce::jmin (300, (starters.getWidth() - 10) / 2);
                    auto centred = starters.withSizeKeepingCentre (2 * w + 10, 28);

                    for (auto* b : starterButtons)
                    {
                        b->setBounds (centred.removeFromLeft (w));
                        centred.removeFromLeft (10);
                    }

                    addButton.setBounds (addButton.getBounds().withX ((getWidth() - 150) / 2));
                    return;
                }

                r.removeFromTop (20);

                for (auto* row : rows)
                    row->setBounds (r.removeFromTop (rowHeight));
            }

            static juce::Array<te::FourOscPlugin::ModSource> sources()
            {
                using S = te::FourOscPlugin::ModSource;
                return { S::lfo1, S::lfo2, S::env1, S::env2, S::midiVelocity, S::midiNoteNum, (S) ((int) S::ccBankSelect + 1), S::mpePressure };
            }

            static juce::String sourceName (te::FourOscPlugin::ModSource src)
            {
                using S = te::FourOscPlugin::ModSource;

                switch (src)
                {
                    case S::lfo1:           return "LFO 1";
                    case S::lfo2:           return "LFO 2";
                    case S::env1:           return "Mod env 1";
                    case S::env2:           return "Mod env 2";
                    case S::midiVelocity:   return "Velocity";
                    case S::midiNoteNum:    return "Note";
                    case S::mpePressure:    return "Pressure";
                    default:                return (int) src == (int) S::ccBankSelect + 1 ? "Mod wheel" : "Source";
                }
            }

            /** Where a control lives in the window (destinations are grouped by page). */
            static juce::String pageOf (const juce::String& paramID)
            {
                if (paramID.startsWith ("lfo") || paramID.startsWith ("mod"))       return "Modulation";
                if (paramID.startsWith ("filter") || paramID.startsWith ("amp"))    return "Filter & Amp";
                if (paramID.startsWith ("distortion") || paramID.startsWith ("chorus") || paramID.startsWith ("delay")
                     || paramID.startsWith ("reverb") || paramID == "masterLevel" || paramID == "legato")
                    return "Effects";
                return "Oscillators";
            }

        private:
            static constexpr int rowHeight = 34, maxRows = 8;

            static std::array<int, 6> columns (int width)
            {
                // source | arrow | destination | amount | remove
                const auto source = 4, arrow = source + width * 22 / 100, dest = arrow + 22, amount = dest + width * 36 / 100;
                return { source, arrow, dest, amount + 8, amount + 8, width - 30 };
            }

            struct Row : public juce::Component
            {
                Row (ModMatrix& m, te::FourOscPlugin::ModSource s, te::AutomatableParameter* p) : owner (m), src (s), param (p)
                {
                    const auto all = sources();

                    for (int i = 0; i < all.size(); ++i)
                        source.addItem (sourceName (all[i]), i + 1);

                    source.setSelectedItemIndex (all.indexOf (src), juce::dontSendNotification);
                    source.setTooltip ("What moves the control");
                    source.onChange = [this] { change (sources()[source.getSelectedItemIndex()], param.get()); };
                    addAndMakeVisible (source);

                    // destinations grouped by the page they're on
                    const auto targets = owner.synth->getAutomatableParameters();

                    for (auto page : { "Oscillators", "Filter & Amp", "Modulation", "Effects" })
                    {
                        destination.addSectionHeading (page);

                        for (int i = 0; i < targets.size(); ++i)
                            if (pageOf (targets[i]->paramID) == page)
                                destination.addItem (targets[i]->getParameterName(), i + 1);
                    }

                    destination.setSelectedId (targets.indexOf (p) + 1, juce::dontSendNotification);
                    destination.setTooltip ("The control it moves (its knob shows a purple ring)");
                    destination.onChange = [this] { change (src, owner.synth->getAutomatableParameters()[destination.getSelectedId() - 1]); };
                    addAndMakeVisible (destination);

                    depth.setAmount (owner.synth->getModulationDepth (src, param));
                    depth.onChange = [this] (float amount)
                    {
                        owner.synth->setModulationDepth (src, param, amount);
                        owner.changed();
                    };
                    addAndMakeVisible (depth);

                    remove.setButtonText (utf8 ("\xc3\x97"));
                    remove.setTooltip ("Remove this routing");
                    remove.getProperties().set ("ezSegment", true);
                    remove.onClick = [this]
                    {
                        owner.synth->clearModulation (src, param);
                        owner.changed();
                        owner.rebuildSoon();
                    };
                    addChildComponent (remove);
                    setRepaintsOnMouseActivity (true);
                }

                void change (te::FourOscPlugin::ModSource newSource, te::AutomatableParameter* newParam)
                {
                    if (newParam == nullptr)
                        return;

                    const auto amount = owner.synth->getModulationDepth (src, param);
                    owner.synth->clearModulation (src, param);
                    owner.synth->setModulationDepth (newSource, newParam, amount);
                    owner.changed();
                    owner.rebuildSoon();
                }

                void mouseEnter (const juce::MouseEvent&) override  { remove.setVisible (true); }
                void mouseExit (const juce::MouseEvent& e) override
                {
                    if (! getLocalBounds().contains (e.getEventRelativeTo (this).getPosition()))
                        remove.setVisible (false);
                }

                void paint (juce::Graphics& g) override
                {
                    const auto area = getLocalBounds().toFloat().reduced (0.0f, 3.0f);

                    if (isMouseOver (true))
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.03f));
                        g.fillRoundedRectangle (area, 6.0f);
                    }

                    // the source's colour chip and the arrow
                    const auto cols = columns (getWidth());
                    g.setColour (src == te::FourOscPlugin::lfo2 ? P::mod.brighter (0.35f) : (src == te::FourOscPlugin::env1 || src == te::FourOscPlugin::env2) ? P::mod.interpolatedWith (P::env, 0.4f) : P::mod);
                    g.fillRoundedRectangle (juce::Rectangle<float> ((float) cols[0], area.getY() + 7.0f, 4.0f, area.getHeight() - 14.0f), 2.0f);
                    g.setColour (Theme::textFaint);
                    g.setFont (Theme::font (Theme::baseFontSize));
                    g.drawText (utf8 ("\xe2\x86\x92"), juce::Rectangle<int> (cols[1], 0, cols[2] - cols[1], getHeight()), juce::Justification::centred, false);
                }

                void resized() override
                {
                    const auto cols = columns (getWidth());
                    const auto h = getHeight() - 8, y = 4;
                    source.setBounds (cols[0] + 10, y, cols[1] - cols[0] - 10, h);
                    destination.setBounds (cols[2], y, cols[3] - cols[2] - 8, h);
                    depth.setBounds (cols[4], y, cols[5] - cols[4] - 6, h);
                    remove.setBounds (cols[5], y, 26, h);
                }

                ModMatrix& owner;
                te::FourOscPlugin::ModSource src;
                te::AutomatableParameter::Ptr param;
                juce::ComboBox source, destination;
                AmountBar depth;
                juce::TextButton remove;
            };

            te::FourOscPlugin* synth;
            std::function<void()> onChange;
            juce::OwnedArray<Row> rows;
            juce::OwnedArray<juce::TextButton> starterButtons;
            MenuTextButton addButton;

            void changed()
            {
                static_cast<te::Plugin*> (synth)->flushPluginStateToValueTree();   // saved with the song (routings aren't undo steps)

                if (onChange)
                    onChange();
            }

            /** Rows are rebuilt after the click that changed them has finished (they're deleted). */
            void rebuildSoon()
            {
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ModMatrix> (this)] { if (safe != nullptr) safe->rebuild(); });
            }

            void addRouting (te::FourOscPlugin::ModSource preferred, te::AutomatableParameter* target, float depth = 0.5f)
            {
                if (target == nullptr || rows.size() >= maxRows)
                    return;

                // the preferred source, else the next one not already on this control
                auto chosen = preferred;

                if (synth->getModulationSources (target).contains (chosen))
                    for (auto src : sources())
                        if (! synth->getModulationSources (target).contains (src))
                        {
                            chosen = src;
                            break;
                        }

                synth->setModulationDepth (chosen, target, depth);
                changed();
                rebuildSoon();
            }
        };

        //==============================================================================
        /** Note lengths that set the delay time from the song's tempo: straight, dotted and triplet
            rows; the one that matches the time now is lit. */
        class DelaySync : public Body
        {
        public:
            DelaySync (te::Plugin& p, Source& lengthSource) : plugin (&p), length (lengthSource)
            {
                const std::pair<const char*, double> straight[] { { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/16", 0.25 } };
                const std::pair<const char*, double> kinds[] { { "", 1.0 }, { " dotted", 1.5 }, { " triplet", 2.0 / 3.0 } };

                for (int row = 0; row < 3; ++row)
                    for (int col = 0; col < 4; ++col)
                    {
                        const auto beats = straight[col].second * kinds[row].second;
                        auto* b = buttons.add (new juce::TextButton (straight[col].first));
                        b->getProperties().set ("ezSegment", true);
                        b->setClickingTogglesState (false);
                        b->setConnectedEdges ((col > 0 ? juce::Button::ConnectedOnLeft : 0) | (col < 3 ? juce::Button::ConnectedOnRight : 0));
                        b->setTooltip (juce::String ("Delay time: a ") + straight[col].first + kinds[row].first + " note at the song's tempo");
                        b->onClick = [this, beats] { length.setOnce ((float) juce::jlimit (1.0, 2000.0, std::round (beats * beatMs()))); refresh(); };
                        noteBeats.push_back (beats);
                        addAndMakeVisible (b);
                    }
            }

            int getIdealHeight (int) const override     { return 3 * 34 + 2 * 6 + 24; }

            void refresh() override
            {
                const auto ms = length.get();

                for (int i = 0; i < buttons.size(); ++i)
                {
                    const auto target = noteBeats[(size_t) i] * beatMs();
                    buttons[i]->setToggleState (std::abs (ms - target) <= juce::jmax (1.0, target * 0.01), juce::dontSendNotification);
                }
            }

            void paint (juce::Graphics& g) override
            {
                // row captions: straight, dotted, triplet
                g.setFont (Theme::font (Theme::captionFontSize, Theme::Weight::semibold));
                g.setColour (Theme::textFaint);
                const char* captions[] { "NOTE", "DOT", "TRIP" };

                for (int row = 0; row < 3; ++row)
                    g.drawText (captions[row], juce::Rectangle<int> (0, row * 40, labelWidth, 34), juce::Justification::centredLeft, false);

                g.setFont (Theme::font (Theme::smallFontSize));
                g.drawText ("At the song's tempo: " + juce::String (juce::roundToInt (60000.0 / beatMs())) + " BPM",
                            juce::Rectangle<int> (labelWidth, 3 * 40, getWidth() - labelWidth, 20), juce::Justification::centredLeft, false);
            }

            void resized() override
            {
                const auto w = (getWidth() - labelWidth) / 4;

                for (int i = 0; i < buttons.size(); ++i)
                    buttons[i]->setBounds (labelWidth + (i % 4) * w, (i / 4) * 40, w, 34);
            }

        private:
            static constexpr int labelWidth = 38;
            te::Plugin* plugin;
            Source& length;
            juce::OwnedArray<juce::TextButton> buttons;
            std::vector<double> noteBeats;

            double beatMs() const   { return 60000.0 / juce::jmax (20.0, plugin->edit.tempoSequence.getBpmAt (te::TimePosition())); }
        };

        //==============================================================================
        /** The amp types as six cards, each with a little picture of its sound. Arrows and the wheel step. */
        class AmpTypes : public Body
        {
        public:
            explicit AmpTypes (Source& modeSource) : mode (modeSource)
            {
                setWantsKeyboardFocus (false);
                setRepaintsOnMouseActivity (true);
            }

            int getIdealHeight (int width) const override   { return width < 6 * 46 ? 2 * 54 + 6 : 58; }

            void refresh() override
            {
                if (const auto now = mode.choiceForValue (mode.get()); now != shown)
                {
                    shown = now;
                    repaint();
                }
            }

            void paint (juce::Graphics& g) override
            {
                const auto names = mode.getChoices();
                g.setColour (P::recess);
                g.fillRoundedRectangle (getLocalBounds().toFloat(), (float) P::controlRadius);
                const auto mouse = getMouseXYRelative();

                for (int i = 0; i < names.size(); ++i)
                {
                    auto card = cardBounds (i).toFloat().reduced (2.5f);
                    const bool selected = i == shown;
                    const bool over = isMouseOver() && card.contains (mouse.toFloat());

                    if (selected)
                    {
                        g.setGradientFill (juce::ColourGradient::vertical (P::capTop, card.getY(), P::moduleBottom, card.getBottom()));
                        g.fillRoundedRectangle (card, 5.0f);
                        g.setColour (P::signal);
                        g.fillRoundedRectangle (card.withTop (card.getBottom() - 2.0f).reduced (card.getWidth() * 0.25f, 0.0f), 1.0f);
                    }
                    else if (over)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.04f));
                        g.fillRoundedRectangle (card, 5.0f);
                    }

                    const auto colour = selected ? P::signal : over ? Theme::text : Theme::textDim;
                    drawWave (g, card.withTrimmedBottom (18.0f).reduced (10.0f, 7.0f), i, colour);
                    g.setColour (selected ? Theme::text : Theme::textDim);
                    g.setFont (Theme::font (Theme::smallFontSize, selected ? Theme::Weight::semibold : Theme::Weight::medium));
                    g.drawText (names[i], card.removeFromBottom (18.0f), juce::Justification::centred, false);
                }
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                for (int i = 0; i < mode.getChoices().size(); ++i)
                    if (cardBounds (i).contains (e.getPosition()))
                        choose (i);
            }

            void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
            {
                if (w.deltaY != 0.0f)
                    choose (juce::jlimit (0, mode.getChoices().size() - 1, shown + (w.deltaY > 0 ? -1 : 1)));
            }

        private:
            Source& mode;
            int shown = -1;

            juce::Rectangle<int> cardBounds (int i) const
            {
                const auto count = juce::jmax (1, mode.getChoices().size());
                const auto perRow = getWidth() < count * 46 ? (count + 1) / 2 : count;
                const auto rowsNeeded = (count + perRow - 1) / perRow;
                const auto w = getWidth() / perRow, h = getHeight() / rowsNeeded;
                return { (i % perRow) * w, (i / perRow) * h, w, h };
            }

            void choose (int i)
            {
                if (! mode.isLocked())
                    mode.setOnce (mode.valueForChoice (i));

                refresh();
            }

            /** Clean, crunch, punk, lead, metal, bass: how hard the wave is clipped. */
            static void drawWave (juce::Graphics& g, juce::Rectangle<float> area, int type, juce::Colour colour)
            {
                const float drive[] { 1.0f, 2.5f, 5.0f, 8.0f, 30.0f, 1.6f };
                const auto d = drive[juce::jlimit (0, 5, type)];
                const auto cycles = type == 5 ? 1.0f : 2.0f;   // bass: slower and fatter
                juce::Path p;

                for (float x = 0.0f; x <= area.getWidth(); x += 1.0f)
                {
                    auto y = std::tanh (std::sin (x / area.getWidth() * cycles * juce::MathConstants<float>::twoPi) * d) / std::tanh (d);

                    if (type == 4)   // metal: a little fizz on top
                        y += 0.08f * std::sin (x * 1.7f);

                    const juce::Point<float> pt (area.getX() + x, area.getCentreY() - y * area.getHeight() * 0.5f);

                    if (x == 0.0f) p.startNewSubPath (pt);
                    else           p.lineTo (pt);
                }

                g.setColour (colour);
                g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        };

        //==============================================================================
        /** The cabinet: a front view of its speakers, ◀ ▶ to change it, and the gate's lamp. */
        class Cabinet : public Body
        {
        public:
            Cabinet (Source& cabSource, AmpPlugin* ampToWatch) : cab (cabSource), amp (ampToWatch)
            {
                for (auto* b : { &previous, &next })
                {
                    b->getProperties().set ("ezSegment", true);
                    addAndMakeVisible (b);
                }

                previous.setTooltip ("The previous cabinet");
                next.setTooltip ("The next cabinet");
                previous.onClick = [this] { step (-1); };
                next.onClick = [this] { step (1); };
            }

            int getIdealHeight (int) const override     { return 128; }

            void refresh() override
            {
                const auto now = cab.choiceForValue (cab.get());
                const auto open = amp != nullptr && amp->getGateGain() > 0.5f;

                if (now != shown || open != gateOpen)
                {
                    shown = now;
                    gateOpen = open;
                    repaint();
                }
            }

            void paint (juce::Graphics& g) override
            {
                auto area = getLocalBounds().toFloat();
                auto lampRow = area.removeFromBottom (18.0f);
                const auto nameRow = area.removeFromBottom (20.0f);
                const auto picture = area.reduced (34.0f, 2.0f);
                drawCabinet (g, picture, cab.getChoices()[juce::jmax (0, shown)]);

                g.setColour (Theme::text);
                g.setFont (Theme::font (Theme::baseFontSize, Theme::Weight::semibold));
                g.drawText (cab.getChoices()[juce::jmax (0, shown)], nameRow, juce::Justification::centred, false);

                // the gate: green while it lets the sound through
                const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ lampRow.getCentreX() - 34.0f, lampRow.getCentreY() });

                if (gateOpen)
                {
                    g.setColour (P::env.withAlpha (0.3f));
                    g.fillEllipse (dot.expanded (3.0f));
                }

                g.setColour (gateOpen ? P::env : Theme::textFaint.withAlpha (0.5f));
                g.fillEllipse (dot);
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (Theme::smallFontSize));
                g.drawText (gateOpen ? "Gate open" : "Gate holding", lampRow.withTrimmedLeft (lampRow.getCentreX() - 24.0f), juce::Justification::centredLeft, false);
            }

            void resized() override
            {
                const auto y = (getHeight() - 38) / 2 - 12;
                previous.setBounds (0, y, 28, 28);
                next.setBounds (getWidth() - 28, y, 28, 28);
            }

            void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
            {
                if (w.deltaY != 0.0f)
                    step (w.deltaY > 0 ? -1 : 1);
            }

        private:
            Source& cab;
            AmpPlugin* amp;
            juce::TextButton previous { utf8 ("\xe2\x97\x80") }, next { utf8 ("\xe2\x96\xb6") };
            int shown = -1;
            bool gateOpen = false;

            void step (int delta)
            {
                const auto count = cab.getChoices().size();

                if (count > 0 && ! cab.isLocked())
                    cab.setOnce (cab.valueForChoice ((cab.choiceForValue (cab.get()) + delta + count) % count));

                refresh();
            }

            /** A speaker cabinet seen from the front: 1, 2 or 4 cones (from the name: "1x12", "2x12", "4x12"...). */
            static void drawCabinet (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name)
            {
                auto speakers = name.containsIgnoreCase ("4x") ? 4 : name.containsIgnoreCase ("2x") ? 2 : 1;

                if (name.containsIgnoreCase ("off") || name.containsIgnoreCase ("none") || name.containsIgnoreCase ("direct"))
                    speakers = 0;

                const auto box = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight() * (speakers == 4 ? 1.1f : 1.4f)), area.getHeight());
                g.setColour (juce::Colours::white.withAlpha (0.04f));
                g.fillRoundedRectangle (box, 6.0f);
                g.setColour (Theme::textFaint.withAlpha (0.55f));
                g.drawRoundedRectangle (box, 6.0f, 1.2f);

                if (speakers == 0)
                {
                    g.setFont (Theme::font (Theme::smallFontSize));
                    g.drawText ("Straight to the desk", box, juce::Justification::centred, false);
                    return;
                }

                const auto grille = box.reduced (8.0f);
                const auto columns = speakers == 1 ? 1 : 2, rows = speakers == 4 ? 2 : 1;
                const auto cw = grille.getWidth() / (float) columns, ch = grille.getHeight() / (float) rows;

                for (int r = 0; r < rows; ++r)
                    for (int c = 0; c < columns; ++c)
                    {
                        const auto cell = juce::Rectangle<float> (grille.getX() + (float) c * cw, grille.getY() + (float) r * ch, cw, ch);
                        const auto d = juce::jmin (cell.getWidth(), cell.getHeight()) - 6.0f;
                        const auto cone = cell.withSizeKeepingCentre (d, d);
                        g.setColour (Theme::textFaint.withAlpha (0.5f));
                        g.drawEllipse (cone, 1.2f);
                        g.drawEllipse (cone.reduced (d * 0.2f), 1.0f);
                        g.setColour (Theme::textFaint.withAlpha (0.35f));
                        g.fillEllipse (cone.withSizeKeepingCentre (d * 0.22f, d * 0.22f));
                    }
            }
        };

        //==============================================================================
        /** A card (a module): title, power, a display, a body, then the controls in a grid. */
        class SectionView : public juce::Component, public juce::SettableTooltipClient
        {
        public:
            SectionView (const Section& s, Source* powerSource, Source* titleToggleSource) : section (s), power (powerSource), titleSource (titleToggleSource)
            {
                setTooltip (s.tip);

                if (power != nullptr)
                {
                    powerButton.setTooltip (s.title + " on or off");
                    powerButton.onClick = [this] { togglePower(); };
                    addAndMakeVisible (powerButton);

                    if (section.rememberPower)
                        lastOnValue = power->choiceForValue (power->get()) > 0 ? power->get() : power->valueForChoice (1);
                }

                if (titleSource != nullptr)
                {
                    titleToggle.setButtonText (shortName (titleSource->getName()));
                    titleToggle.getProperties().set ("ezToggle", true);
                    titleToggle.getProperties().set ("ezLamp", (int) signalColour (section.signal).getARGB());
                    titleToggle.setTooltip (titleSource->getName() + ": click to switch it on or off");
                    titleToggle.onClick = [this]
                    {
                        titleSource->setOnce (titleSource->valueForChoice (titleSource->choiceForValue (titleSource->get()) > 0 ? 0 : 1));
                        refresh();
                    };
                    addAndMakeVisible (titleToggle);
                }
            }

            const Section& getSection() const   { return section; }
            float getWeight() const             { return section.weight; }

            void addControl (std::unique_ptr<Control> c)                    { addAndMakeVisible (*c); controls.push_back (std::move (c)); }
            void setDisplay (std::unique_ptr<PluginDisplays::Display> d)    { display = std::move (d); addAndMakeVisible (*display); }
            void setBody (std::unique_ptr<Body> b)                          { body = std::move (b); addAndMakeVisible (*body); }
            Body* getBody() const                                           { return body.get(); }

            /** The whole plugin is bypassed: every card looks off. */
            void setPluginActive (bool shouldBeActive)
            {
                if (pluginActive != shouldBeActive)
                {
                    pluginActive = shouldBeActive;
                    firstRefresh = true;
                    refresh();
                }
            }

            void refresh()
            {
                for (auto& c : controls)
                    c->refresh();

                if (display != nullptr)
                    display->refresh();

                if (body != nullptr)
                    body->refresh();

                if (titleSource != nullptr)
                    titleToggle.setToggleState (titleSource->choiceForValue (titleSource->get()) > 0, juce::dontSendNotification);

                const auto ownOn = power == nullptr || power->choiceForValue (power->get()) > 0;

                if (power != nullptr && ownOn && section.rememberPower)
                    lastOnValue = power->get();

                const auto on = ownOn && pluginActive;

                if (on != wasOn || firstRefresh)
                {
                    wasOn = on;
                    powerButton.setToggleState (ownOn, juce::dontSendNotification);

                    for (auto& c : controls)
                        c->setModuleActive (on);

                    if (display != nullptr)
                        display->setActive (on);

                    repaint();
                }

                firstRefresh = false;
            }

            /** The height this section needs at a width (and places its children when asked). */
            int layout (int width, bool place)
            {
                const auto top = section.bare ? 0 : P::cardTitleHeight + 8;
                auto y = top;
                const auto pad = section.bare ? 0 : padding;
                const auto inner = width - 2 * pad;

                if (display != nullptr)
                {
                    if (place)
                        display->setBounds (pad, y, inner, section.displayHeight);

                    y += section.displayHeight + (section.bare ? 0 : 10);
                }

                if (body != nullptr)
                {
                    const auto h = body->getIdealHeight (inner);

                    if (place)
                        body->setBounds (pad, y, inner, h);

                    y += h + 10;
                }

                // Menus, buttons, steppers and faders first (whole rows), then the knobs in a grid
                int x = 0, rowHeight = 0;

                const auto flow = [&] (bool knobs)
                {
                    auto columns = juce::jmax (1, section.columns);

                    // a few on/off buttons share one row evenly, whatever the knob grid is
                    if (! knobs)
                    {
                        int count = 0, spans = 0;

                        for (auto& c : controls)
                            if (! c->isKnob())
                            {
                                ++count;
                                spans += c->getColumnSpan (columns);
                            }

                        if (count > 0 && spans <= 4)
                            columns = juce::jmax (spans, juce::jmin (columns, 3));
                    }

                    const auto cell = inner / columns;

                    for (auto& c : controls)
                    {
                        if (c->isKnob() != knobs)
                            continue;

                        const auto span = c->getColumnSpan (columns);

                        if (x > 0 && x + span > columns)
                        {
                            y += rowHeight + 4;
                            x = 0;
                            rowHeight = 0;
                        }

                        if (place)
                            c->setBounds (pad + x * cell, y, span * cell, c->getIdealHeight());

                        x += span;
                        rowHeight = juce::jmax (rowHeight, c->getIdealHeight());
                    }

                    if (x > 0)
                    {
                        y += rowHeight + 6;
                        x = 0;
                        rowHeight = 0;
                    }
                };

                flow (section.knobsFirst);
                flow (! section.knobsFirst);
                return section.bare ? y : y + padding - 6;
            }

            void resized() override
            {
                auto title = juce::Rectangle<int> (padding, 0, getWidth() - 2 * padding, P::cardTitleHeight + 2);

                if (power != nullptr)
                    powerButton.setBounds (title.removeFromRight (26).withSizeKeepingCentre (26, 26));

                if (titleSource != nullptr)
                {
                    const auto w = juce::GlyphArrangement::getStringWidthInt (Theme::font (Theme::smallFontSize), titleToggle.getButtonText()) + 30;
                    titleToggle.setBounds (title.removeFromRight (w).withSizeKeepingCentre (w, 22));
                }

                layout (getWidth(), true);
            }

            void paint (juce::Graphics& g) override
            {
                if (section.bare)
                    return;   // the display is the card

                const auto on = wasOn;
                PluginLook::drawModule (g, getLocalBounds().toFloat().reduced (0.5f), on);

                // an EQ band's colour along the top
                if (section.band >= 0)
                {
                    g.setColour (Theme::bandColours[juce::jlimit (0, 3, section.band)].withAlpha (on ? 1.0f : 0.35f));
                    g.fillRoundedRectangle (juce::Rectangle<float> (8.0f, 0.5f, (float) getWidth() - 16.0f, 3.0f), 1.5f);
                }

                g.setColour (on ? Theme::text : Theme::textDim);
                g.setFont (Theme::font (Theme::smallFontSize + 0.5f, Theme::Weight::semibold).withExtraKerningFactor (0.06f));
                g.drawText (section.title.toUpperCase(), juce::Rectangle<int> (padding, 2, getWidth() - 2 * padding - 80, P::cardTitleHeight), juce::Justification::centredLeft, true);

                // the spine: the module's signal colour under the title (grey while it's off)
                const auto spineY = (float) P::cardTitleHeight + 2.0f;
                g.setColour (Theme::hairline);
                g.drawHorizontalLine (juce::roundToInt (spineY), (float) padding, (float) (getWidth() - padding));
                g.setColour (on ? signalColour (section.signal) : P::off);
                g.fillRoundedRectangle (juce::Rectangle<float> ((float) padding, spineY - 0.5f, 26.0f, 2.0f), 1.0f);
            }

        private:
            static constexpr int padding = 12;
            Section section;
            Source* power;
            Source* titleSource;
            PowerButton powerButton;
            MenuTextButton titleToggle;
            std::vector<std::unique_ptr<Control>> controls;
            std::unique_ptr<PluginDisplays::Display> display;
            std::unique_ptr<Body> body;
            float lastOnValue = 1.0f;
            bool firstRefresh = true, wasOn = true, pluginActive = true;

            static juce::String shortName (const juce::String& name)
            {
                // "LFO 1 sync" -> "Sync", "Analog" -> "Analog"
                const auto last = name.fromLastOccurrenceOf (" ", false, false);
                return (last.isNotEmpty() ? last : name).substring (0, 1).toUpperCase() + (last.isNotEmpty() ? last : name).substring (1);
            }

            void togglePower()
            {
                const auto on = power->choiceForValue (power->get()) > 0;

                if (on && section.rememberPower)
                    lastOnValue = power->get();

                power->setOnce (on ? power->valueForChoice (0) : section.rememberPower ? lastOnValue : power->valueForChoice (1));
                refresh();
            }
        };

        //==============================================================================
        /** The whole editor: header (power, name, pages, presets, A/B, reset) and the cards. */
        class Editor : public juce::Component,
                       public PluginDisplays::Values,
                       public Context,
                       private juce::Timer
        {
        public:
            explicit Editor (te::Plugin& p) : plugin (&p)
            {
                setLookAndFeel (&look.get());

                if (auto l = PluginLayouts::forPlugin (p))
                    layout = std::move (*l);
                else
                    layout = fallbackLayout (p);

                settingIds = PluginLayouts::settingIds (layout);
                makeSources();
                buildHeader();

                for (const auto& page : layout.pages)
                    pages.push_back (buildPage (page));

                setSize (layout.width, 200);

                // Every page gets the tallest page's height: the window never jumps between tabs
                for (int i = 0; i < (int) pages.size(); ++i)
                    pageHeight = juce::jmax (pageHeight, layoutPage (i, false));

                showPage (0);
                refresh();
                setWantsKeyboardFocus (false);
                startTimerHz (20);   // follows automation, undo, other windows and the meters
            }

            ~Editor() override
            {
                stopTimer();
                setLookAndFeel (nullptr);
            }

            //==============================================================================
            // PluginDisplays::Values
            float get (const juce::String& id) const override
            {
                if (auto* s = findSource (id))
                    return s->get();

                return 0.0f;
            }

            juce::NormalisableRange<float> rangeOf (const juce::String& id) const override
            {
                if (auto* s = findSource (id))
                    return s->getRange();

                return { 0.0f, 1.0f };
            }

            void beginDrag (const juce::String& id) override     { if (auto* s = findSource (id)) s->begin(); }
            void set (const juce::String& id, float v) override  { if (auto* s = findSource (id)) s->set (v); }
            void endDrag (const juce::String& id) override       { if (auto* s = findSource (id)) s->end(); }
            double getBpm() const override                       { return plugin->edit.tempoSequence.getBpmAt (te::TimePosition()); }

            std::optional<float> getLevel (const juce::String& which) const override
            {
                if (dynamic_cast<EffectPlugin*> (plugin) == nullptr)
                    return {};   // Tracktion's own effects don't publish levels

                if (which == "in")  return inputDb;
                if (which == "out") return outputDb;

                if (which == "gr")
                    if (auto* gate = dynamic_cast<GatePlugin*> (plugin))
                        return juce::Decibels::gainToDecibels (gate->getGateGain(), -60.0f);

                return {};
            }

            //==============================================================================
            // Context
            Source* findSource (const juce::String& id) const override
            {
                const auto it = sources.find (id);
                return it != sources.end() ? it->second.get() : nullptr;
            }

            std::optional<std::pair<float, float>> modRange (const juce::String& id) const override
            {
                auto* synth = dynamic_cast<te::FourOscPlugin*> (plugin);
                auto* param = synth != nullptr ? synth->getAutomatableParameterByID (id).get() : nullptr;

                if (param == nullptr)
                    return {};

                const auto modSources = synth->getModulationSources (param);

                if (modSources.isEmpty())
                    return {};

                // LFOs swing both ways; envelopes, velocity, the wheel and the note push one way
                const auto base = param->valueRange.convertTo0to1 (param->valueRange.snapToLegalValue (param->getCurrentValue()));
                auto lo = base, hi = base;

                for (auto src : modSources)
                {
                    const auto depth = synth->getModulationDepth (src, param);
                    const bool bipolar = src == te::FourOscPlugin::lfo1 || src == te::FourOscPlugin::lfo2;

                    if (bipolar)
                    {
                        lo -= std::abs (depth);
                        hi += std::abs (depth);
                    }
                    else if (depth >= 0.0f)  hi += depth;
                    else                     lo += depth;
                }

                return std::make_pair (juce::jlimit (0.0f, 1.0f, lo), juce::jlimit (0.0f, 1.0f, hi));
            }

            //==============================================================================
            void paint (juce::Graphics& g) override
            {
                g.fillAll (P::chassis);

                auto header = getLocalBounds().removeFromTop (headerHeight);
                g.setGradientFill (juce::ColourGradient::vertical (P::chassisEdge, 0.0f, P::chassis, (float) headerHeight));
                g.fillRect (header);
                g.setColour (Theme::hairline);
                g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

                const auto on = plugin->isEnabled();

                // bypassed: an amber strip under the header
                if (! on)
                {
                    g.setColour (P::gainReduction);
                    g.fillRect (0, headerHeight - 2, getWidth(), 2);
                }

                auto text = header.withLeft (onButton.getRight() + 10)
                                  .withRight ((pageTabs.isEmpty() ? leftmostHeaderControl() : pageTabs.getFirst()->getX()) - 12);

                // a description too long for one line wraps onto two, and the name moves up to make room
                const auto description = on ? layout.description : juce::String ("Bypassed: the sound passes untouched");
                const auto smallFont = Theme::font (Theme::smallFontSize);
                const bool wraps = juce::GlyphArrangement::getStringWidth (smallFont, description) > (float) text.getWidth();
                const int nameBottom = wraps ? 25 : headerHeight / 2 + 2;

                g.setColour (on ? Theme::text : Theme::textDim);
                g.setFont (Theme::font (Theme::titleFontSize + 1.5f, Theme::Weight::semibold));
                g.drawText (plugin->getName(), text.withBottom (nameBottom), juce::Justification::bottomLeft, true);
                g.setColour (on ? Theme::textDim : P::gainReduction);
                g.setFont (smallFont);
                g.drawFittedText (description, text.withTop (nameBottom + 2).withHeight (wraps ? 28 : 16),
                                  juce::Justification::topLeft, wraps ? 2 : 1, 1.0f);

                // the sound flows from card to card: chevrons in the gaps
                if (! pages.empty())
                    for (const auto& row : pages[(size_t) currentPage])
                        for (auto* s : row)
                            if (s->getSection().chainNext && s->isVisible())
                            {
                                const auto c = juce::Point<float> ((float) s->getRight() + (float) gap * 0.5f, (float) s->getY() + 54.0f);
                                juce::Path chevron;
                                chevron.startNewSubPath (c.x - 2.0f, c.y - 4.0f);
                                chevron.lineTo (c.x + 2.0f, c.y);
                                chevron.lineTo (c.x - 2.0f, c.y + 4.0f);
                                g.setColour (Theme::textFaint);
                                g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                            }

                if (sections.empty())
                {
                    g.setColour (Theme::textDim);
                    g.drawText ("This effect has no controls", getLocalBounds().withTrimmedTop (headerHeight), juce::Justification::centred);
                }
            }

            void paintOverChildren (juce::Graphics& g) override
            {
                Planned::paintChips (g, *this);
            }

            void resized() override
            {
                auto header = getLocalBounds().removeFromTop (headerHeight).reduced (margin, 0);
                onButton.setBounds (header.removeFromLeft (30).withSizeKeepingCentre (30, 30));

                auto right = header;
                resetButton.setBounds (right.removeFromRight (30).withSizeKeepingCentre (30, 30));
                right.removeFromRight (6);

                // A | copy | B
                bButton.setBounds (right.removeFromRight (30).withSizeKeepingCentre (30, 28));
                copyButton.setBounds (right.removeFromRight (26).withSizeKeepingCentre (26, 28));
                aButton.setBounds (right.removeFromRight (30).withSizeKeepingCentre (30, 28));
                right.removeFromRight (8);

                // the preset bar: previous, the name, next
                const auto presetWidth = juce::jlimit (136, 240, getWidth() / 5);
                auto bar = right.removeFromRight (presetWidth).withSizeKeepingCentre (presetWidth, 28);
                nextButton.setBounds (bar.removeFromRight (26));
                previousButton.setBounds (bar.removeFromLeft (26));
                presetButton.setBounds (bar);
                right.removeFromRight (8);

                for (auto* t : headerToggles)
                {
                    const auto w = juce::GlyphArrangement::getStringWidthInt (Theme::font (Theme::baseFontSize - 0.5f), t->getButtonText()) + 32;
                    t->setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, 26));
                    right.removeFromRight (6);
                }

                if (! layout.planned.isEmpty())
                {
                    plannedChip.setBounds (right.removeFromRight (70).withSizeKeepingCentre (70, 24));
                    right.removeFromRight (8);
                }

                right.removeFromRight (6);

                for (int i = pageTabs.size(); --i >= 0;)
                {
                    const auto w = juce::GlyphArrangement::getStringWidthInt (Theme::font (Theme::baseFontSize, Theme::Weight::semibold),
                                                                              pageTabs[i]->getButtonText()) + 26;
                    pageTabs[i]->setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, 30));
                }

                layoutPage (currentPage, true);
            }

            bool keyPressed (const juce::KeyPress& key) override
            {
                // Ctrl+1..4: pages; Ctrl+Left/Right: the previous or next preset
                if (key.getModifiers().isCommandDown() && key.getKeyCode() >= '1' && key.getKeyCode() < '1' + (int) pages.size())
                {
                    showPage (key.getKeyCode() - '1');
                    return true;
                }

                if (key.getModifiers().isCommandDown() && (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey))
                {
                    stepPreset (key.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1);
                    return true;
                }

                return false;
            }

        private:
            static constexpr int headerHeight = P::headerHeight, margin = P::margin, gap = P::cardGap;

            juce::SharedResourcePointer<PluginLook> look;
            te::Plugin* plugin;   // Tracktion closes this window when the plugin goes (never a Ptr: that would keep it alive)
            Layout layout;
            juce::StringArray settingIds;
            std::map<juce::String, std::unique_ptr<Source>> sources;
            std::vector<std::vector<std::vector<SectionView*>>> pages;   // page > row > sections
            std::vector<std::unique_ptr<SectionView>> sections;
            int currentPage = 0, pageHeight = 0, routingCount = -1;
            bool presetEdited = false;
            float inputDb = -100.0f, outputDb = -100.0f;

            PowerButton onButton;
            juce::OwnedArray<juce::TextButton> pageTabs;
            juce::OwnedArray<MenuTextButton> headerToggles;
            MenuTextButton presetButton, aButton, bButton;
            juce::TextButton previousButton { utf8 ("\xe2\x97\x80") }, nextButton { utf8 ("\xe2\x96\xb6") }, copyButton { utf8 ("\xe2\x86\x92") };
            juce::TextButton plannedChip { "Planned" };
            IconButton resetButton { "Reset", Icon::undo, {}, IconButton::Style::ghost };

            // A/B: the sound not being heard, kept in this window
            std::array<std::vector<PluginPresets::Value>, 2> slots;
            std::array<bool, 2> slotFilled { false, false };
            int activeSlot = 0;

            //==============================================================================
            void makeSources()
            {
                for (auto* p : plugin->getAutomatableParameters())
                    sources[p->paramID] = std::make_unique<ParamSource> (*p);

                for (const auto& s : layout.settings)
                    if (sources.find (s.id) == sources.end())
                        sources[s.id] = std::make_unique<SettingSource> (*plugin, s);

                for (auto& entry : sources)
                    entry.second->onUserChange = [this] { markPresetEdited(); };
            }

            void markPresetEdited()
            {
                if (! std::exchange (presetEdited, true))
                    updatePresetButton();
            }

            int leftmostHeaderControl() const
            {
                auto x = presetButton.getX() - previousButton.getWidth();

                for (auto* t : headerToggles)
                    x = juce::jmin (x, t->getX());

                if (! layout.planned.isEmpty())
                    x = juce::jmin (x, plannedChip.getX());

                return x;
            }

            static Layout fallbackLayout (te::Plugin& p)
            {
                Layout l;
                Section all ("Controls");
                int knobs = 0;

                for (auto* param : p.getAutomatableParameters())
                {
                    all.items.push_back ({ param->paramID, {}, Item::Size::normal, Item::Style::automatic });
                    knobs += isSwitch (*param) ? 0 : 1;
                }

                all.columns = juce::jlimit (3, 6, knobs);
                l.width = juce::jmax (420, all.columns * 84 + 2 * margin + 24);
                l.description = p.getPluginType() == VocalChainPlugin::xmlTypeName ? "Six macros run a whole vocal chain" : "Built-in effect";

                if (! all.items.empty())
                    l.pages.push_back ({ {}, { { all } } });

                return l;
            }

            std::vector<std::vector<SectionView*>> buildPage (const Page& page)
            {
                std::vector<std::vector<SectionView*>> rows;

                for (const auto& row : page.rows)
                {
                    std::vector<SectionView*> built;

                    for (const auto& s : row)
                    {
                        auto view = std::make_unique<SectionView> (s, s.powerId.isNotEmpty() ? findSource (s.powerId) : nullptr,
                                                                   s.titleToggle.isNotEmpty() ? findSource (s.titleToggle) : nullptr);

                        if (s.display.has_value())
                            view->setDisplay (PluginDisplays::create (*s.display, *this, s.displayIds));

                        for (const auto& item : s.items)
                            if (auto* source = findSource (item.id))
                                view->addControl (std::make_unique<Control> (*source, item, *this));

                        if (s.custom == "modMatrix")
                            if (auto* synth = dynamic_cast<te::FourOscPlugin*> (plugin))
                            {
                                auto matrix = std::make_unique<ModMatrix> (*synth, [this] { markPresetEdited(); });
                                matrix->onCountChange = [this] { updateTabNames(); };
                                view->setBody (std::move (matrix));
                            }

                        if (s.custom == "delaySync")
                            if (auto* length = findSource ("length"))
                                view->setBody (std::make_unique<DelaySync> (*plugin, *length));

                        if (s.custom == "ampTypes")
                            if (auto* mode = findSource ("mode"))
                                view->setBody (std::make_unique<AmpTypes> (*mode));

                        if (s.custom == "cabinet")
                            if (auto* cab = findSource ("cab"))
                                view->setBody (std::make_unique<Cabinet> (*cab, dynamic_cast<AmpPlugin*> (plugin)));

                        addChildComponent (*view);
                        built.push_back (view.get());
                        sections.push_back (std::move (view));
                    }

                    rows.push_back (built);
                }

                return rows;
            }

            void buildHeader()
            {
                onButton.setTooltip ("On: the effect is working. Off: bypassed, the sound passes untouched");
                onButton.onClick = [this]
                {
                    plugin->edit.getUndoManager().beginNewTransaction();
                    plugin->setEnabled (! plugin->isEnabled());   // (the Devices panel notices within a second)
                    refresh();
                    repaint();
                };
                addAndMakeVisible (onButton);

                if (layout.pages.size() > 1)
                    for (int i = 0; i < (int) layout.pages.size(); ++i)
                    {
                        auto* tab = pageTabs.add (new juce::TextButton (layout.pages[(size_t) i].name));
                        tab->setClickingTogglesState (false);
                        tab->getProperties().set ("ezSegment", true);
                        tab->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i + 1 < (int) layout.pages.size() ? juce::Button::ConnectedOnRight : 0));
                        tab->setTooltip ("Show " + layout.pages[(size_t) i].name + " (Ctrl+" + juce::String (i + 1) + ")");
                        tab->onClick = [this, i] { showPage (i); };
                        addAndMakeVisible (tab);
                    }

                for (const auto& id : layout.headerToggles)
                    if (auto* s = findSource (id))
                    {
                        auto* t = headerToggles.add (new MenuTextButton());
                        t->setButtonText (s->getName());
                        t->getProperties().set ("ezToggle", true);
                        t->setTooltip (s->getName() + ": click to switch it on or off");
                        t->onClick = [this, s] { s->setOnce (s->valueForChoice (s->choiceForValue (s->get()) > 0 ? 0 : 1)); refresh(); };
                        addAndMakeVisible (t);
                    }

                if (! layout.planned.isEmpty())
                {
                    Planned::mark (plannedChip, "Next");
                    plannedChip.setTooltip ("Coming next for this plug-in:\n- " + layout.planned.joinIntoString ("\n- "));
                    addAndMakeVisible (plannedChip);
                }

                // the preset bar: previous | name | next, one recessed group
                for (auto* b : { &previousButton, (juce::TextButton*) &presetButton, &nextButton })
                {
                    b->getProperties().set ("ezSegment", true);
                    addAndMakeVisible (b);
                }

                previousButton.setConnectedEdges (juce::Button::ConnectedOnRight);
                presetButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
                nextButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
                previousButton.setTooltip ("The previous preset (Ctrl+Left)");
                nextButton.setTooltip ("The next preset (Ctrl+Right)");
                previousButton.onClick = [this] { stepPreset (-1); };
                nextButton.onClick = [this] { stepPreset (1); };
                presetButton.setTooltip ("Presets: built-in sounds and your own. Save, load or delete them here. A dot: changed since it was loaded");
                presetButton.onClick = [this] { showPresetMenu(); };
                presetButton.onMenu = [this] { showPresetMenu(); };

                aButton.setButtonText ("A");
                bButton.setButtonText ("B");

                for (auto* b : { (juce::TextButton*) &aButton, &copyButton, (juce::TextButton*) &bButton })
                {
                    b->setClickingTogglesState (false);
                    b->getProperties().set ("ezSegment", true);
                    addAndMakeVisible (b);
                }

                aButton.setConnectedEdges (juce::Button::ConnectedOnRight);
                copyButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
                bButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
                aButton.setTooltip ("A/B: compare two settings. Click to hear A; right-click: copy what you hear to A");
                bButton.setTooltip ("A/B: compare two settings. Click to hear B (starts as a copy of A); right-click: copy what you hear to B");
                copyButton.setTooltip ("Copy what you hear to the other slot (A to B, or B to A)");
                aButton.onClick = [this] { switchTo (0); };
                bButton.onClick = [this] { switchTo (1); };
                aButton.onMenu = [this] { copyTo (0); };
                bButton.onMenu = [this] { copyTo (1); };
                copyButton.onClick = [this] { copyTo (1 - activeSlot); };
                aButton.setToggleState (true, juce::dontSendNotification);

                resetButton.setTooltip ("Reset: every control back to its default (one undo step)");
                resetButton.onClick = [this] { resetAll(); };
                addAndMakeVisible (resetButton);

                updatePresetButton();
            }

            void updateTabNames()
            {
                int count = 0;

                for (auto& s : sections)
                    if (auto* matrix = dynamic_cast<ModMatrix*> (s->getBody()))
                        count = matrix->getCount();

                if (count == routingCount)
                    return;

                routingCount = count;

                for (int i = 0; i < pageTabs.size(); ++i)
                {
                    const auto& name = layout.pages[(size_t) i].name;
                    pageTabs[i]->setButtonText (name == "Modulation" && count > 0 ? name + "  " + juce::String (count) : name);
                }

                resized();
            }

            //==============================================================================
            void showPage (int index)
            {
                currentPage = juce::jlimit (0, juce::jmax (0, (int) pages.size() - 1), index);

                for (int p = 0; p < (int) pages.size(); ++p)
                    for (auto& row : pages[(size_t) p])
                        for (auto* s : row)
                            s->setVisible (p == currentPage);

                for (int i = 0; i < pageTabs.size(); ++i)
                    pageTabs[i]->setToggleState (i == currentPage, juce::dontSendNotification);

                setSize (getWidth(), juce::jmax (pageHeight, layoutPage (currentPage, false)));
                layoutPage (currentPage, true);
                refresh();
                repaint();
            }

            int layoutPage (int pageIndex, bool place)
            {
                if (pages.empty())
                    return headerHeight + 80;

                const auto& rows = pages[(size_t) pageIndex];
                const auto width = getWidth() - 2 * margin;

                // the rows' heights first, so a shorter page can sit in the middle of the window
                std::vector<int> heights;
                int total = 0;

                for (const auto& row : rows)
                {
                    float totalWeight = 0.0f;

                    for (auto* s : row)
                        totalWeight += s->getWeight();

                    const auto available = width - gap * ((int) row.size() - 1);
                    int height = 0;

                    for (auto* s : row)
                        height = juce::jmax (height, s->layout (juce::roundToInt ((float) available * s->getWeight() / juce::jmax (0.01f, totalWeight)), false));

                    heights.push_back (height);
                    total += height + gap;
                }

                const auto natural = headerHeight + gap + total + margin - gap;

                if (! place)
                    return natural;

                // a shorter page's last row grows to fill the window (pages share the tallest one's height)
                if (! heights.empty())
                    heights.back() += juce::jmax (0, getHeight() - natural);

                auto y = headerHeight + gap;

                for (size_t r = 0; r < rows.size(); ++r)
                {
                    const auto& row = rows[r];
                    float totalWeight = 0.0f;

                    for (auto* s : row)
                        totalWeight += s->getWeight();

                    const auto available = width - gap * ((int) row.size() - 1);
                    auto x = margin;

                    for (size_t i = 0; i < row.size(); ++i)
                    {
                        const auto w = i + 1 == row.size() ? margin + width - x
                                                           : juce::roundToInt ((float) available * row[i]->getWeight() / juce::jmax (0.01f, totalWeight));
                        row[i]->setBounds (x, y, w, heights[r]);
                        x += w + gap;
                    }

                    y += heights[r] + gap;
                }

                return natural;
            }

            //==============================================================================
            void refresh()
            {
                const auto on = plugin->isEnabled();

                if (on != onButton.getToggleState())
                {
                    onButton.setToggleState (on, juce::dontSendNotification);
                    repaint (getLocalBounds().removeFromTop (headerHeight));
                }

                for (auto* t : headerToggles)
                    if (auto* s = findSource (layout.headerToggles[headerToggles.indexOf (t)]))
                        t->setToggleState (s->choiceForValue (s->get()) > 0, juce::dontSendNotification);

                for (auto& s : sections)
                {
                    s->setPluginActive (on);

                    if (s->isVisible())
                        s->refresh();
                }

                updateTabNames();
            }

            void timerCallback() override
            {
                if (auto* effect = dynamic_cast<EffectPlugin*> (plugin))
                {
                    inputDb = juce::Decibels::gainToDecibels (effect->takeInputPeak(), -100.0f);
                    outputDb = juce::Decibels::gainToDecibels (effect->takeOutputPeak(), -100.0f);
                }

                refresh();
            }

            //==============================================================================
            void updatePresetButton()
            {
                const auto name = PluginPresets::getCurrentName (*plugin);
                presetButton.setButtonText ((name.isEmpty() ? juce::String ("Default") : name)
                                            + (presetEdited ? utf8 (" \xe2\x80\xa2") : juce::String()));
            }

            /** Built-in presets, then yours: what the arrows step through. */
            std::vector<PluginPresets::Preset> allPresets() const
            {
                auto all = PluginPresets::getFactory (plugin->getPluginType());

                for (auto& p : PluginPresets::getUser (plugin->getPluginType()))
                    all.push_back (std::move (p));

                return all;
            }

            void stepPreset (int delta)
            {
                const auto all = allPresets();

                if (all.empty())
                    return;

                const auto current = PluginPresets::getCurrentName (*plugin);
                int index = -1;

                for (int i = 0; i < (int) all.size(); ++i)
                    if (all[(size_t) i].name == current)
                        index = i;

                index = index < 0 ? (delta > 0 ? 0 : (int) all.size() - 1) : (index + delta + (int) all.size()) % (int) all.size();
                loadPreset (all[(size_t) index]);
            }

            void afterSoundChange()
            {
                for (auto& s : sections)
                    if (auto* matrix = dynamic_cast<ModMatrix*> (s->getBody()))
                        matrix->rebuild();

                refresh();
            }

            void loadPreset (const PluginPresets::Preset& preset)
            {
                plugin->edit.getUndoManager().beginNewTransaction();
                PluginPresets::apply (*plugin, preset.values, settingIds, true);
                PluginPresets::setCurrentName (*plugin, preset.name, true);   // undone with the sound
                presetEdited = false;
                updatePresetButton();
                afterSoundChange();
            }

            void resetAll()
            {
                plugin->edit.getUndoManager().beginNewTransaction();
                PluginPresets::resetToDefaults (*plugin, settingIds);
                PluginPresets::setCurrentName (*plugin, {}, true);
                presetEdited = false;
                updatePresetButton();
                afterSoundChange();
            }

            void showPresetMenu()
            {
                const auto type = plugin->getPluginType();
                const auto factory = PluginPresets::getFactory (type);
                const auto user = PluginPresets::getUser (type);
                const auto current = PluginPresets::getCurrentName (*plugin);

                juce::PopupMenu menu;
                menu.addSectionHeader ("Built in");
                menu.addItem (1, "Default (every control reset)");

                // Sounds with a category (the synth's Bass, Keys, Pad...) go in a submenu each, in order
                juce::StringArray categories;

                for (const auto& p : factory)
                    if (p.category.isNotEmpty())
                        categories.addIfNotAlreadyThere (p.category);

                for (const auto& category : categories)
                {
                    juce::PopupMenu sub;
                    bool holdsCurrent = false;

                    for (int i = 0; i < (int) factory.size(); ++i)
                        if (factory[(size_t) i].category == category)
                        {
                            sub.addItem (100 + i, factory[(size_t) i].name, true, factory[(size_t) i].name == current);
                            holdsCurrent = holdsCurrent || factory[(size_t) i].name == current;
                        }

                    menu.addSubMenu (category, sub, true, nullptr, holdsCurrent);
                }

                for (int i = 0; i < (int) factory.size(); ++i)
                    if (factory[(size_t) i].category.isEmpty())
                        menu.addItem (100 + i, factory[(size_t) i].name, true, factory[(size_t) i].name == current);

                menu.addSectionHeader ("Yours");

                if (user.empty())
                    menu.addItem (2, "None yet: save a sound below", false);

                for (int i = 0; i < (int) user.size(); ++i)
                    menu.addItem (1000 + i, user[(size_t) i].name, true, user[(size_t) i].name == current);

                menu.addSeparator();
                menu.addItem (3, "Save as a preset...");

                juce::PopupMenu remove;

                for (int i = 0; i < (int) user.size(); ++i)
                    remove.addItem (2000 + i, user[(size_t) i].name);

                menu.addSubMenu ("Delete one of yours", remove, ! user.empty());
                menu.addItem (4, "Show the presets folder");

                juce::Component::SafePointer<Editor> safe (this);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton), [safe, factory, user] (int result)
                {
                    if (safe == nullptr || result == 0)
                        return;

                    if (result == 1)            safe->resetAll();
                    else if (result == 3)       safe->askPresetName();
                    else if (result == 4)
                    {
                        const auto folder = PluginPresets::getUserFolder (safe->plugin->getPluginType());
                        folder.createDirectory();
                        folder.startAsProcess();
                    }
                    else if (result >= 2000)    PluginPresets::deleteUser (user[(size_t) (result - 2000)]);
                    else if (result >= 1000)    safe->loadPreset (user[(size_t) (result - 1000)]);
                    else if (result >= 100)     safe->loadPreset (factory[(size_t) (result - 100)]);
                });
            }

            void askPresetName()
            {
                auto* window = new juce::AlertWindow ("Save a preset", "A name for this sound:", juce::MessageBoxIconType::NoIcon, this);
                const auto current = PluginPresets::getCurrentName (*plugin);
                window->addTextEditor ("name", current.isNotEmpty() ? current : plugin->getName() + " sound");
                window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
                window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

                juce::Component::SafePointer<Editor> safe (this);
                window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result)
                {
                    if (safe == nullptr || result != 1)
                        return;

                    const auto name = window->getTextEditorContents ("name").trim();
                    juce::String error;

                    if (PluginPresets::saveUser (*safe->plugin, name, safe->settingIds, error))
                    {
                        PluginPresets::setCurrentName (*safe->plugin, name);
                        safe->presetEdited = false;
                        safe->updatePresetButton();
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save the preset", error);
                    }
                }), true);
            }

            //==============================================================================
            void switchTo (int slot)
            {
                if (slot == activeSlot)
                    return;

                slots[(size_t) activeSlot] = PluginPresets::capture (*plugin, settingIds);
                slotFilled[(size_t) activeSlot] = true;

                if (! slotFilled[(size_t) slot])
                {
                    slots[(size_t) slot] = slots[(size_t) activeSlot];   // B starts as a copy of A: change it, then compare
                    slotFilled[(size_t) slot] = true;
                }

                plugin->edit.getUndoManager().beginNewTransaction();
                PluginPresets::apply (*plugin, slots[(size_t) slot], settingIds, true);
                activeSlot = slot;
                aButton.setToggleState (slot == 0, juce::dontSendNotification);
                bButton.setToggleState (slot == 1, juce::dontSendNotification);
                afterSoundChange();
            }

            void copyTo (int slot)
            {
                slots[(size_t) slot] = PluginPresets::capture (*plugin, settingIds);
                slotFilled[(size_t) slot] = true;
                (slot == 0 ? aButton : bButton).setTooltip (juce::String (slot == 0 ? "A" : "B") + " now holds the sound you heard when you copied it");
            }
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

                if (content->getHeight() > maxHeight)
                {
                    auto viewport = std::make_unique<juce::Viewport>();
                    viewport->setScrollBarsShown (true, false);
                    viewport->setSize (content->getWidth() + viewport->getScrollBarThickness(), maxHeight);
                    viewport->setViewedComponent (content.release(), true);
                    setContentOwned (viewport.release(), true);
                }
                else
                {
                    setContentOwned (content.release(), true);   // follows the editor's size (pages differ in height)
                }

                setResizable (false, false);
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

                if (auto* editor = getContentComponent())
                    if (editor->keyPressed (key))
                        return true;

                return juce::DocumentWindow::keyPressed (key);
            }

            void closeButtonPressed() override
            {
                if (plugin != nullptr)
                    plugin->windowState->closeWindowExplicitly();   // destroys this window
            }

        private:
            static constexpr int maxHeight = 820;
            te::Plugin* plugin;   // Tracktion closes this window when the plugin goes (never a Ptr: that would keep it alive)
        };
    }

    std::unique_ptr<juce::Component> ParamEditor::createContent (te::Plugin& plugin)
    {
        return std::make_unique<Editor> (plugin);
    }

    std::unique_ptr<juce::Component> ParamEditor::createWindow (te::Plugin& plugin)
    {
        return std::make_unique<Window> (plugin);
    }
}
