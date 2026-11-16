/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "ParamEditor.h"
#include "PluginLayouts.h"
#include "Theme.h"
#include "Widgets.h"
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
        /** A knob, a toggle, a menu or a row of buttons for one Source. */
        class Control : public juce::Component
        {
        public:
            using Size = Item::Size;
            using Style = Item::Style;

            Control (Source& s, const juce::String& labelText, Style style, Size sizeToUse) : source (s), size (sizeToUse)
            {
                const auto choices = s.getChoices();
                kind = style != Style::automatic ? style
                     : s.isToggle() ? Style::toggle
                     : ! choices.isEmpty() ? (choices.size() <= 4 ? Style::buttons : Style::menu)
                     : Style::knob;

                if ((kind == Style::menu || kind == Style::buttons) && choices.isEmpty())
                    kind = Style::knob;

                name.setText (labelText.isNotEmpty() ? labelText : s.getName(), juce::dontSendNotification);
                name.setJustificationType (juce::Justification::centred);
                name.setFont (Theme::font (Theme::captionFontSize + 0.5f, Theme::Weight::medium));
                name.setColour (juce::Label::textColourId, Theme::textDim);
                name.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (name);

                const auto tip = s.getName() + (s.isAutomatable() ? "" : " (a setting: not automatable)");

                if (kind == Style::toggle)
                {
                    toggle.setButtonText (name.getText());
                    toggle.setClickingTogglesState (false);
                    toggle.setColour (juce::TextButton::buttonOnColourId, Theme::activeFill);
                    toggle.setTooltip (tip + ": click to switch it on or off");
                    toggle.onClick = [this]
                    {
                        if (! source.isLocked())
                            source.setOnce (source.valueForChoice (source.choiceForValue (source.get()) > 0 ? 0 : 1));

                        refresh();
                    };
                    toggle.onMenu = [this] { showMenu(); };
                    addAndMakeVisible (toggle);
                    name.setVisible (false);
                }
                else if (kind == Style::menu)
                {
                    for (int i = 0; i < choices.size(); ++i)
                        combo.addItem (choices[i], i + 1);

                    combo.onChange = [this]
                    {
                        if (! source.isLocked() && combo.getSelectedItemIndex() >= 0)
                            source.setOnce (source.valueForChoice (combo.getSelectedItemIndex()));

                        refresh();
                    };
                    combo.setTooltip (tip);
                    addAndMakeVisible (combo);
                    name.setJustificationType (juce::Justification::centredLeft);
                }
                else if (kind == Style::buttons)
                {
                    for (int i = 0; i < choices.size(); ++i)
                    {
                        auto* b = segments.add (new juce::TextButton (choices[i]));
                        b->setClickingTogglesState (false);
                        b->setColour (juce::TextButton::buttonOnColourId, Theme::activeFill);
                        b->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i + 1 < choices.size() ? juce::Button::ConnectedOnRight : 0));
                        b->setTooltip (tip + ": " + choices[i]);
                        b->onClick = [this, i]
                        {
                            if (! source.isLocked())
                                source.setOnce (source.valueForChoice (i));

                            refresh();
                        };
                        addAndMakeVisible (b);
                    }

                    name.setJustificationType (juce::Justification::centredLeft);
                }
                else
                {
                    kind = Style::knob;
                    const auto range = s.getRange();
                    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                    knob.setNormalisableRange ({ (double) range.start, (double) range.end, (double) range.interval, (double) range.skew });

                    if (const auto def = s.getDefault())
                        knob.setDoubleClickReturnValue (true, (double) *def);

                    knob.setVelocityModeParameters (0.3, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Shift: fine
                    knob.setPopupDisplayEnabled (true, true, nullptr);
                    knob.textFromValueFunction = [this] (double v) { return source.toText ((float) v); };
                    knob.onDragStart = [this] { source.begin(); dragging = true; };
                    knob.onDragEnd = [this] { source.end(); dragging = false; refresh(); };
                    knob.onValueChange = [this]
                    {
                        if (source.isLocked())
                            return refresh();

                        if (dragging)
                            source.set ((float) knob.getValue());
                        else
                            source.setOnce ((float) knob.getValue());   // a wheel step or double-click: its own undo step

                        value.setText (source.toText ((float) knob.getValue()), juce::dontSendNotification);
                    };
                    knob.setTooltip (tip + ": drag (Shift: fine) or scroll; double-click: default; double-click the number to type; right-click for more");
                    knob.onMenu = [this] { showMenu(); };
                    addAndMakeVisible (knob);

                    value.setJustificationType (juce::Justification::centred);
                    value.setFont (Theme::font (Theme::captionFontSize + 0.5f));
                    value.setColour (juce::Label::textColourId, Theme::text);
                    value.setEditable (false, true);   // double-click the number to type one
                    value.onTextChange = [this]
                    {
                        if (! source.isLocked())
                            source.setOnce (source.fromText (value.getText()));

                        refresh();
                    };
                    addAndMakeVisible (value);
                }

                refresh();
            }

            void refresh()
            {
                const auto now = source.get();
                const bool locked = source.isLocked();

                if (kind == Style::toggle)
                {
                    toggle.setToggleState (source.choiceForValue (now) > 0, juce::dontSendNotification);
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
                else
                {
                    if (! knob.isMouseButtonDown())
                        knob.setValue (now, juce::dontSendNotification);

                    knob.setColour (juce::Slider::rotarySliderFillColourId, locked ? Theme::textDim : Theme::accent);

                    if (! value.isBeingEdited())
                        value.setText (source.toText (now) + (locked ? " (lane)" : ""), juce::dontSendNotification);
                }
            }

            /** Grid columns this control spans. */
            int getColumnSpan (int columns) const
            {
                if (kind == Style::buttons)
                    return juce::jmin (columns, juce::jmax (2, segments.size()));

                if (kind == Style::menu)
                    return juce::jmin (columns, 2);

                if (kind == Style::toggle)
                    return juce::jmin (columns, toggle.getButtonText().length() > 7 ? 2 : 1);

                return 1;
            }

            int getIdealHeight() const
            {
                if (kind == Style::knob)
                    return size == Size::large ? 106 : size == Size::small ? 76 : 88;

                return kind == Style::toggle ? 32 : 48;   // menus and buttons carry their name above
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
                    name.setBounds (r.removeFromTop (16).reduced (3, 0));
                    combo.setBounds (r.removeFromTop (26).reduced (3, 0));
                }
                else if (kind == Style::buttons)
                {
                    name.setBounds (r.removeFromTop (16).reduced (3, 0));
                    auto row = r.removeFromTop (26).reduced (3, 0);
                    const auto w = row.getWidth() / juce::jmax (1, segments.size());

                    for (int i = 0; i < segments.size(); ++i)
                        segments[i]->setBounds (i + 1 == segments.size() ? row : row.removeFromLeft (w));
                }
                else
                {
                    r.reduce (2, 0);
                    name.setBounds (r.removeFromTop (16));
                    value.setBounds (r.removeFromBottom (16));
                    const auto maxSize = size == Size::large ? 66 : size == Size::small ? 40 : 50;
                    const auto knobSize = juce::jmin (r.getWidth(), r.getHeight(), maxSize);
                    knob.setBounds (r.withSizeKeepingCentre (knobSize, knobSize));
                }
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                if (e.mods.isPopupMenu())
                    showMenu();
            }

        private:
            Source& source;
            Size size;
            Style kind = Style::knob;
            juce::Label name, value;
            MenuSlider knob;
            juce::ComboBox combo;
            MenuTextButton toggle;
            juce::OwnedArray<juce::TextButton> segments;
            bool dragging = false;

            void showMenu()
            {
                juce::PopupMenu menu;
                menu.addSectionHeader (source.getName() + ": " + source.toText (source.get()));
                menu.addItem (1, "Back to default\tDouble-click", source.getDefault().has_value() && ! source.isLocked());
                menu.addItem (2, "Type a value...", ! source.isLocked() && kind == Style::knob);
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
        /** The synth's modulation routings: source, destination and amount, one row each. */
        class ModMatrix : public juce::Component
        {
        public:
            ModMatrix (te::FourOscPlugin& p, std::function<void()> changedCallback) : synth (&p), onChange (std::move (changedCallback))
            {
                addButton.setButtonText ("+ Add a routing");
                addButton.setTooltip ("Adds LFO 1 to the filter cut-off; then pick the source, the destination and the amount");
                addButton.onClick = [this] { addRouting(); };
                addAndMakeVisible (addButton);
                rebuild();
            }

            void rebuild()
            {
                rows.clear();

                for (auto* param : synth->getAutomatableParameters())
                    for (auto src : synth->getModulationSources (param))
                        addAndMakeVisible (rows.add (new Row (*this, src, param)));

                addButton.setEnabled (rows.size() < 6);
                resized();
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                if (rows.isEmpty())
                {
                    g.setColour (Theme::textFaint);
                    g.setFont (Theme::font (Theme::smallFontSize));
                    g.drawFittedText ("Nothing is routed yet. The LFOs and mod envelopes change the sound once they're routed here.",
                                      getLocalBounds().withTrimmedBottom (34).reduced (4), juce::Justification::centred, 3);
                }
            }

            void resized() override
            {
                auto r = getLocalBounds();
                addButton.setBounds (r.removeFromBottom (28).removeFromLeft (150));
                r.removeFromBottom (4);

                for (auto* row : rows)
                    row->setBounds (r.removeFromTop (28));
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

        private:
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

                    int i = 1;

                    for (auto* target : owner.synth->getAutomatableParameters())
                        destination.addItem (target->getParameterName(), i++);

                    destination.setSelectedItemIndex (owner.synth->getAutomatableParameters().indexOf (p), juce::dontSendNotification);
                    destination.setTooltip ("The control it moves");
                    destination.onChange = [this] { change (src, owner.synth->getAutomatableParameters()[destination.getSelectedItemIndex()]); };
                    addAndMakeVisible (destination);

                    depth.setSliderStyle (juce::Slider::LinearBar);
                    depth.setRange (-1.0, 1.0, 0.01);
                    depth.setValue (owner.synth->getModulationDepth (src, param), juce::dontSendNotification);
                    depth.setDoubleClickReturnValue (true, 0.5);
                    depth.textFromValueFunction = [] (double v) { return (v >= 0.0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + "%"; };
                    depth.updateText();
                    depth.setColour (juce::Slider::trackColourId, Theme::accent.withAlpha (0.45f));
                    depth.setColour (juce::Slider::textBoxTextColourId, Theme::text);
                    depth.setTooltip ("How much: drag (negative moves it the other way). Double-click: 50%");
                    depth.onValueChange = [this]
                    {
                        owner.synth->setModulationDepth (src, param, (float) depth.getValue());
                        owner.changed();
                    };
                    addAndMakeVisible (depth);

                    remove.setButtonText (juce::CharPointer_UTF8 ("\xc3\x97"));
                    remove.setTooltip ("Remove this routing");
                    remove.onClick = [this]
                    {
                        owner.synth->clearModulation (src, param);
                        owner.changed();
                        owner.rebuildSoon();
                    };
                    addAndMakeVisible (remove);
                }

                void change (te::FourOscPlugin::ModSource newSource, te::AutomatableParameter* newParam)
                {
                    if (newParam == nullptr)
                        return;

                    const auto amount = (float) depth.getValue();
                    owner.synth->clearModulation (src, param);
                    owner.synth->setModulationDepth (newSource, newParam, amount);
                    owner.changed();
                    owner.rebuildSoon();
                }

                void resized() override
                {
                    auto r = getLocalBounds().reduced (0, 2);
                    remove.setBounds (r.removeFromRight (24));
                    r.removeFromRight (4);
                    source.setBounds (r.removeFromLeft (r.getWidth() * 30 / 100));
                    r.removeFromLeft (4);
                    destination.setBounds (r.removeFromLeft (r.getWidth() * 55 / 100));
                    r.removeFromLeft (4);
                    depth.setBounds (r);
                }

                ModMatrix& owner;
                te::FourOscPlugin::ModSource src;
                te::AutomatableParameter::Ptr param;
                juce::ComboBox source, destination;
                juce::Slider depth;
                juce::TextButton remove;
            };

            te::FourOscPlugin* synth;
            std::function<void()> onChange;
            juce::OwnedArray<Row> rows;
            juce::TextButton addButton;

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

            void addRouting()
            {
                // LFO 1 to the cut-off is the classic first routing; else the next free source
                auto* target = synth->filterFreq.get();

                for (auto src : sources())
                    if (! synth->getModulationSources (target).contains (src))
                    {
                        synth->setModulationDepth (src, target, 0.5f);
                        break;
                    }

                changed();
                rebuild();
            }
        };

        //==============================================================================
        /** Note-length buttons that set the delay time from the song's tempo. */
        class DelaySync : public juce::Component
        {
        public:
            DelaySync (te::Plugin& p, Source& lengthSource) : plugin (&p), length (lengthSource)
            {
                const std::pair<const char*, double> notes[] { { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/8 dot", 0.75 }, { "1/16", 0.25 },
                                                               { "1/4 T", 2.0 / 3.0 }, { "1/8 T", 1.0 / 3.0 } };

                for (const auto& [label, beats] : notes)
                {
                    auto* b = buttons.add (new juce::TextButton (label));
                    b->setTooltip (juce::String ("Delay time: a ") + label + " note at the song's tempo");
                    b->onClick = [this, beatCount = beats]
                    {
                        const auto bpm = plugin->edit.tempoSequence.getBpmAt (te::TimePosition());
                        length.setOnce ((float) juce::jlimit (1.0, 2000.0, std::round (beatCount * 60000.0 / juce::jmax (20.0, bpm))));
                    };
                    addAndMakeVisible (b);
                }
            }

            void resized() override
            {
                const auto columns = 2, h = 26;
                const auto w = getWidth() / columns;

                for (int i = 0; i < buttons.size(); ++i)
                    buttons[i]->setBounds ((i % columns) * w + 2, (i / columns) * (h + 6), w - 4, h);
            }

        private:
            te::Plugin* plugin;
            Source& length;
            juce::OwnedArray<juce::TextButton> buttons;
        };

        //==============================================================================
        /** A card: title (and maybe an on switch), a display, then the controls in a grid. */
        class SectionView : public juce::Component, public juce::SettableTooltipClient
        {
        public:
            SectionView (const Section& s, Source* powerSource) : section (s), power (powerSource)
            {
                setTooltip (s.tip);

                if (power != nullptr)
                {
                    powerButton.setTooltip (s.title + " on or off");
                    powerButton.setClickingTogglesState (false);
                    powerButton.onClick = [this] { power->setOnce (power->valueForChoice (power->choiceForValue (power->get()) > 0 ? 0 : 1)); };
                    addAndMakeVisible (powerButton);
                }
            }

            float getWeight() const     { return section.weight; }

            void addControl (std::unique_ptr<Control> c)                    { addAndMakeVisible (*c); controls.push_back (std::move (c)); }
            void setDisplay (std::unique_ptr<PluginDisplays::Display> d)    { display = std::move (d); addAndMakeVisible (*display); }
            void setBody (std::unique_ptr<juce::Component> b)               { body = std::move (b); addAndMakeVisible (*body); }
            juce::Component* getBody() const                                { return body.get(); }

            void refresh()
            {
                for (auto& c : controls)
                    c->refresh();

                if (display != nullptr)
                    display->refresh();

                if (power != nullptr)
                {
                    const auto on = power->choiceForValue (power->get()) > 0;

                    if (on != powerButton.getToggleState() || firstRefresh)
                    {
                        powerButton.setToggleState (on, juce::dontSendNotification);

                        for (auto& c : controls)
                            c->setAlpha (on ? 1.0f : 0.45f);

                        repaint();
                    }
                }

                firstRefresh = false;
            }

            /** The height this section needs at a width (and places its children when asked). */
            int layout (int width, bool place)
            {
                auto y = titleHeight + 8;
                const auto inner = width - 2 * padding;

                if (display != nullptr)
                {
                    if (place)
                        display->setBounds (padding, y, inner, section.displayHeight);

                    y += section.displayHeight + 8;
                }

                if (body != nullptr)
                {
                    const auto h = section.custom == "modMatrix" ? 210 : 96;

                    if (place)
                        body->setBounds (padding, y, inner, h);

                    y += h + 8;
                }

                // Menus and buttons first (whole rows), then the knobs in a grid
                int x = 0, rowHeight = 0;

                const auto flow = [&] (bool knobs)
                {
                    auto columns = juce::jmax (1, section.columns);

                    // a few on/off buttons share one row evenly, whatever the knob grid is
                    if (! knobs)
                    {
                        int count = 0;
                        bool allSingle = true;

                        for (auto& c : controls)
                            if (! c->isKnob())
                            {
                                ++count;
                                allSingle = allSingle && c->getColumnSpan (columns) == 1;
                            }

                        if (allSingle && count > 0 && count <= 4)
                            columns = juce::jmax (count, juce::jmin (columns, 3));
                    }

                    const auto cell = inner / columns;

                    for (auto& c : controls)
                    {
                        if (c->isKnob() != knobs)
                            continue;

                        const auto span = c->getColumnSpan (columns);

                        if (x > 0 && x + span > columns)
                        {
                            y += rowHeight + 2;
                            x = 0;
                            rowHeight = 0;
                        }

                        if (place)
                            c->setBounds (padding + x * cell, y, span * cell, c->getIdealHeight());

                        x += span;
                        rowHeight = juce::jmax (rowHeight, c->getIdealHeight());
                    }

                    if (x > 0)
                    {
                        y += rowHeight + 4;
                        x = 0;
                        rowHeight = 0;
                    }
                };

                flow (false);
                flow (true);
                return y + padding - 4;
            }

            void resized() override
            {
                if (power != nullptr)
                    powerButton.setBounds (getWidth() - padding - 26, (titleHeight - 24) / 2 + 2, 26, 24);

                layout (getWidth(), true);
            }

            void paint (juce::Graphics& g) override
            {
                drawCard (g, getLocalBounds().toFloat(), false);
                const auto on = power == nullptr || powerButton.getToggleState();
                g.setColour (on ? Theme::text : Theme::textDim);
                g.setFont (Theme::font (Theme::baseFontSize, Theme::Weight::semibold));
                g.drawText (section.title, juce::Rectangle<int> (padding, 2, getWidth() - 2 * padding - 40, titleHeight), juce::Justification::centredLeft, true);

                if (power != nullptr)
                {
                    g.setColour (on ? Theme::success : Theme::textFaint.withAlpha (0.6f));
                    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) getWidth() - (float) padding - 38.0f, (float) titleHeight * 0.5f + 2.0f }));
                }

                g.setColour (Theme::hairline);
                g.drawHorizontalLine (titleHeight + 2, (float) padding, (float) (getWidth() - padding));
            }

        private:
            static constexpr int titleHeight = 30, padding = 10;
            Section section;
            Source* power;
            IconButton powerButton { "On", Icon::power, {}, IconButton::Style::ghost };
            std::vector<std::unique_ptr<Control>> controls;
            std::unique_ptr<PluginDisplays::Display> display;
            std::unique_ptr<juce::Component> body;
            bool firstRefresh = true;
        };

        //==============================================================================
        /** The whole editor: header (on, name, pages, presets, A/B, reset) and the cards. */
        class Editor : public juce::Component,
                       public PluginDisplays::Values,
                       private juce::Timer
        {
        public:
            explicit Editor (te::Plugin& p) : plugin (&p)
            {
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
                showPage (0);
                refresh();
                setWantsKeyboardFocus (false);
                startTimerHz (20);   // follows automation, undo and other windows
            }

            ~Editor() override
            {
                stopTimer();
            }

            //==============================================================================
            // PluginDisplays::Values
            float get (const juce::String& id) const override
            {
                if (auto* s = find (id))
                    return s->get();

                return 0.0f;
            }

            juce::NormalisableRange<float> rangeOf (const juce::String& id) const override
            {
                if (auto* s = find (id))
                    return s->getRange();

                return { 0.0f, 1.0f };
            }

            void beginDrag (const juce::String& id) override     { if (auto* s = find (id)) s->begin(); }
            void set (const juce::String& id, float v) override  { if (auto* s = find (id)) s->set (v); }
            void endDrag (const juce::String& id) override       { if (auto* s = find (id)) s->end(); }
            double getBpm() const override                       { return plugin->edit.tempoSequence.getBpmAt (te::TimePosition()); }

            //==============================================================================
            void paint (juce::Graphics& g) override
            {
                g.fillAll (Theme::background);

                auto header = getLocalBounds().removeFromTop (headerHeight);
                g.setGradientFill (juce::ColourGradient::vertical (Theme::panel.brighter (0.035f), 0.0f, Theme::panel, (float) headerHeight));
                g.fillRect (header);
                g.setColour (Theme::hairline);
                g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

                const auto on = plugin->isEnabled();
                auto text = header.withLeft (onButton.getRight() + 10)
                                  .withRight ((pageTabs.isEmpty() ? presetButton.getX() : pageTabs.getFirst()->getX()) - 12);

                // a description too long for one line wraps onto two, and the name moves up to make room
                const auto description = on ? layout.description : juce::String ("Bypassed: the sound passes untouched");
                const auto smallFont = Theme::font (Theme::smallFontSize);
                const bool wraps = juce::GlyphArrangement::getStringWidth (smallFont, description) > (float) text.getWidth();
                const int nameBottom = wraps ? 26 : headerHeight / 2 + 3;

                g.setColour (on ? Theme::text : Theme::textDim);
                g.setFont (Theme::font (Theme::titleFontSize + 1.5f, Theme::Weight::semibold));
                g.drawText (plugin->getName(), text.withBottom (nameBottom), juce::Justification::bottomLeft, true);
                g.setColour (on ? Theme::textDim : Theme::warning);
                g.setFont (smallFont);
                g.drawFittedText (description, text.withTop (nameBottom + 2).withHeight (wraps ? 28 : 16),
                                  juce::Justification::topLeft, wraps ? 2 : 1, 1.0f);

                if (sections.empty())
                {
                    g.setColour (Theme::textDim);
                    g.drawText ("This effect has no controls", getLocalBounds().withTrimmedTop (headerHeight), juce::Justification::centred);
                }
            }

            void resized() override
            {
                auto header = getLocalBounds().removeFromTop (headerHeight).reduced (margin, 0);
                onButton.setBounds (header.removeFromLeft (30).withSizeKeepingCentre (30, 30));

                auto right = header;
                resetButton.setBounds (right.removeFromRight (30).withSizeKeepingCentre (30, 30));
                right.removeFromRight (6);
                bButton.setBounds (right.removeFromRight (30).withSizeKeepingCentre (30, 28));
                aButton.setBounds (right.removeFromRight (30).withSizeKeepingCentre (30, 28));
                right.removeFromRight (8);
                const auto presetWidth = juce::jlimit (124, 220, getWidth() / 5);
                presetButton.setBounds (right.removeFromRight (presetWidth).withSizeKeepingCentre (presetWidth, 30));
                right.removeFromRight (14);

                for (int i = pageTabs.size(); --i >= 0;)
                {
                    const auto w = juce::GlyphArrangement::getStringWidthInt (Theme::font (Theme::baseFontSize - 0.5f, Theme::Weight::semibold),
                                                                              pageTabs[i]->getButtonText()) + 24;
                    pageTabs[i]->setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, 28));
                }

                layoutPage (true);
            }

            bool keyPressed (const juce::KeyPress& key) override
            {
                // Ctrl+1..4: pages
                if (key.getModifiers().isCommandDown() && key.getKeyCode() >= '1' && key.getKeyCode() < '1' + (int) pages.size())
                {
                    showPage (key.getKeyCode() - '1');
                    return true;
                }

                return false;
            }

        private:
            static constexpr int headerHeight = 58, margin = 14, gap = 10;

            te::Plugin* plugin;   // Tracktion closes this window when the plugin goes (never a Ptr: that would keep it alive)
            Layout layout;
            juce::StringArray settingIds;
            std::map<juce::String, std::unique_ptr<Source>> sources;
            std::vector<std::vector<std::vector<SectionView*>>> pages;   // page > row > sections
            std::vector<std::unique_ptr<SectionView>> sections;
            int currentPage = 0;
            bool presetEdited = false;

            IconButton onButton { "On", Icon::power, {}, IconButton::Style::ghost };
            juce::OwnedArray<juce::TextButton> pageTabs;
            MenuTextButton presetButton, aButton, bButton;
            IconButton resetButton { "Reset", Icon::undo, {}, IconButton::Style::ghost };

            // A/B: the sound not being heard, kept in this window
            std::array<std::vector<PluginPresets::Value>, 2> slots;
            std::array<bool, 2> slotFilled { false, false };
            int activeSlot = 0;

            //==============================================================================
            Source* find (const juce::String& id) const
            {
                const auto it = sources.find (id);
                return it != sources.end() ? it->second.get() : nullptr;
            }

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

            static Layout fallbackLayout (te::Plugin& p)
            {
                Layout l;
                Section all { "Controls" };
                int knobs = 0;

                for (auto* param : p.getAutomatableParameters())
                {
                    all.items.push_back ({ param->paramID, {}, Item::Size::normal, Item::Style::automatic });
                    knobs += isSwitch (*param) ? 0 : 1;
                }

                all.columns = juce::jlimit (3, 6, knobs);
                l.width = juce::jmax (420, all.columns * 84 + 2 * margin + 20);
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
                        auto view = std::make_unique<SectionView> (s, s.powerId.isNotEmpty() ? find (s.powerId) : nullptr);

                        if (s.display.has_value())
                            view->setDisplay (PluginDisplays::create (*s.display, *this, s.displayIds));

                        for (const auto& item : s.items)
                            if (auto* source = find (item.id))
                                view->addControl (std::make_unique<Control> (*source, item.label, item.style, item.size));

                        if (s.custom == "modMatrix")
                            if (auto* synth = dynamic_cast<te::FourOscPlugin*> (plugin))
                                view->setBody (std::make_unique<ModMatrix> (*synth, [this] { markPresetEdited(); }));

                        if (s.custom == "delaySync")
                            if (auto* length = find ("length"))
                                view->setBody (std::make_unique<DelaySync> (*plugin, *length));

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
                onButton.setClickingTogglesState (false);
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
                        tab->setColour (juce::TextButton::buttonOnColourId, Theme::activeFill);
                        tab->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i + 1 < (int) layout.pages.size() ? juce::Button::ConnectedOnRight : 0));
                        tab->setTooltip ("Show " + layout.pages[(size_t) i].name + " (Ctrl+" + juce::String (i + 1) + ")");
                        tab->onClick = [this, i] { showPage (i); };
                        addAndMakeVisible (tab);
                    }

                presetButton.setTooltip ("Presets: built-in sounds and your own. Save, load or delete them here");
                presetButton.onClick = [this] { showPresetMenu(); };
                presetButton.onMenu = [this] { showPresetMenu(); };
                addAndMakeVisible (presetButton);

                aButton.setButtonText ("A");
                bButton.setButtonText ("B");

                for (auto* b : { &aButton, &bButton })
                {
                    b->setClickingTogglesState (false);
                    b->setColour (juce::TextButton::buttonOnColourId, Theme::activeFill);
                    addAndMakeVisible (b);
                }

                aButton.setConnectedEdges (juce::Button::ConnectedOnRight);
                bButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
                aButton.setTooltip ("A/B: compare two settings. Click to hear A; right-click: copy what you hear to A");
                bButton.setTooltip ("A/B: compare two settings. Click to hear B (starts as a copy of A); right-click: copy what you hear to B");
                aButton.onClick = [this] { switchTo (0); };
                bButton.onClick = [this] { switchTo (1); };
                aButton.onMenu = [this] { copyTo (0); };
                bButton.onMenu = [this] { copyTo (1); };
                aButton.setToggleState (true, juce::dontSendNotification);

                resetButton.setTooltip ("Reset: every control back to its default (one undo step)");
                resetButton.onClick = [this] { resetAll(); };
                addAndMakeVisible (resetButton);

                updatePresetButton();
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

                // Pages differ in height: the window follows
                setSize (getWidth(), layoutPage (false));
                layoutPage (true);
                refresh();
            }

            int layoutPage (bool place)
            {
                if (pages.empty())
                    return headerHeight + 80;

                auto y = headerHeight + gap;
                const auto width = getWidth() - 2 * margin;

                for (const auto& row : pages[(size_t) currentPage])
                {
                    float totalWeight = 0.0f;

                    for (auto* s : row)
                        totalWeight += s->getWeight();

                    // Cards in a row share the tallest one's height
                    const auto available = width - gap * ((int) row.size() - 1);
                    std::vector<int> widths;
                    int height = 0;

                    for (auto* s : row)
                    {
                        widths.push_back (juce::roundToInt ((float) available * s->getWeight() / juce::jmax (0.01f, totalWeight)));
                        height = juce::jmax (height, s->layout (widths.back(), false));
                    }

                    if (place)
                    {
                        auto x = margin;

                        for (size_t i = 0; i < row.size(); ++i)
                        {
                            const auto w = i + 1 == row.size() ? margin + width - x : widths[i];
                            row[i]->setBounds (x, y, w, height);
                            x += w + gap;
                        }
                    }

                    y += height + gap;
                }

                return y + margin - gap;
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

                for (auto& s : sections)
                    if (s->isVisible())
                        s->refresh();
            }

            void timerCallback() override   { refresh(); }

            //==============================================================================
            void updatePresetButton()
            {
                const auto name = PluginPresets::getCurrentName (*plugin);
                presetButton.setButtonText ((name.isEmpty() ? juce::String ("Default") : name) + (presetEdited && name.isNotEmpty() ? " (edited)" : "")
                                            + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")));
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
                PluginPresets::setCurrentName (*plugin, preset.name);
                presetEdited = false;
                updatePresetButton();
                afterSoundChange();
            }

            void resetAll()
            {
                plugin->edit.getUndoManager().beginNewTransaction();
                PluginPresets::resetToDefaults (*plugin, settingIds);
                PluginPresets::setCurrentName (*plugin, {});
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

                for (int i = 0; i < (int) factory.size(); ++i)
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
