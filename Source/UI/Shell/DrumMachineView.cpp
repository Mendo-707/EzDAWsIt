/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "BrowserPanel.h"
#include "DrumMachineView.h"

namespace ez
{
    namespace
    {
        constexpr int toolbarHeight = 32, editorWidth = 210, labelWidth = 78, msWidth = 42, headerHeight = 14;
        constexpr int rowHeight = 22, rowGap = 2, velocityHeight = 46, padGap = 6;
        constexpr int editorMinViewWidth = 1000;   // narrower than this and the pad editor starts hidden
        constexpr int defaultVelocity = 100, accentVelocity = 127;

        const int stepsChoices[] = { 8, 12, 16, 24, 32 };
        const int lengthChoices[] = { 1, 2, 4 };   // bars
        const int swingChoices[] = { 50, 54, 58, 62, 66, 70, 75 };
        const int velocityChoices[] = { 40, 70, 100, 127 };
        const char* const velocityNames[] = { "Ghost (40)", "Soft (70)", "Normal (100)", "Accent (127)" };
        const char* const patternNames[] = { "A", "B", "C", "D" };
        constexpr int numPages = 4;

        int padRow (int pad)    { return pad; }   // rows top to bottom are pads 1-16

        juce::String tuneText (double v)    { return v == 0.0 ? juce::String ("0 st") : (v > 0 ? "+" : "") + juce::String (v, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " st"; }
        juce::String percentText (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
        juce::String levelText (double v)   { return v <= DrumMachinePlugin::minLevel + 0.01 ? juce::String ("Off") : juce::String (v, 1) + " dB"; }

        juce::String panText (double v)
        {
            const auto amount = juce::roundToInt (std::abs (v) * 100.0);
            return amount == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (amount);
        }

        double parseNumber (const juce::String& text)
        {
            return text.retainCharacters ("-+.0123456789").getDoubleValue();
        }
    }

    //==============================================================================
    /** One pad of the 4x4 grid. */
    class DrumMachineView::Pad : public juce::Component,
                                 public juce::SettableTooltipClient
    {
    public:
        Pad (DrumMachineView& o, int i) : owner (o), index (i)
        {
            setRepaintsOnMouseActivity (true);
            setTooltip ("Pad " + juce::String (index + 1) + ": click to play (higher = louder). Double-click or drop a file to load a sound. Right-click for more.");
        }

        float flash = 0.0f;
        juce::uint32 lastHits = 0;

        void paint (juce::Graphics& g) override
        {
            auto* drums = owner.getDrums();

            if (drums == nullptr)
                return;

            const auto s = drums->getPad (index);
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool isSelected = owner.selectedPad == index;
            const auto colour = DrumKits::getPadColour (index);

            g.setColour (Theme::raised.brighter (isMouseOver() ? 0.08f : 0.0f).interpolatedWith (colour, flash * 0.45f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (owner.dropPad == index ? Theme::accent : isSelected ? Theme::selection : Theme::border);
            g.drawRoundedRectangle (r.reduced (isSelected || owner.dropPad == index ? 0.5f : 0.0f), 5.0f, isSelected || owner.dropPad == index ? 2.0f : 1.0f);

            auto inner = getLocalBounds().reduced (6, 4);
            g.setColour (s.muted ? colour.withAlpha (0.35f) : colour);
            g.fillRoundedRectangle (inner.removeFromTop (4).withWidth (18).toFloat(), 2.0f);

            const bool roomy = getHeight() >= 40;
            const auto nameFont = Theme::font (roomy ? Theme::smallFontSize : Theme::captionFontSize, true);
            inner.removeFromTop (roomy ? 2 : 0);
            g.setColour (s.muted ? Theme::textDim : Theme::text);
            g.setFont (nameFont);
            g.drawFittedText (s.name, inner.removeFromTop (juce::jmin (inner.getHeight(), (int) std::ceil (nameFont.getHeight()) + 2)),
                              juce::Justification::centredLeft, 1, 0.7f);   // shrink a little before "Closed..."

            if (inner.getHeight() >= 12)
            {
                auto bottom = inner.removeFromBottom (12);
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (Theme::captionFontSize));
                g.drawText (juce::String (index + 1), bottom, juce::Justification::centredLeft, false);

                juce::String padFlags;

                if (s.muted)       padFlags << "M ";
                if (s.soloed)      padFlags << "S ";
                if (s.reversed)    padFlags << "R ";

                if (drums->getPadLoadError (index).isNotEmpty())
                    padFlags << "!";

                g.setColour (drums->getPadLoadError (index).isNotEmpty() ? Theme::danger : s.soloed ? Theme::accent : Theme::textDim);
                g.drawText (padFlags.trim(), bottom, juce::Justification::centredRight, false);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                owner.selectPad (index, false);
                owner.showPadMenu (index);
                return;
            }

            // Higher on the pad plays louder, like a velocity-sensitive pad
            const auto velocity = juce::jlimit (20, 127, juce::roundToInt (127.0f - 95.0f * (float) e.y / (float) juce::jmax (1, getHeight())));
            owner.selectPad (index, true, velocity);
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                owner.loadSampleWithChooser (index);
        }

    private:
        DrumMachineView& owner;
        const int index;
    };

    //==============================================================================
    /** The selected pad's sound and values. Lives in a Viewport (the panel can be short). */
    class DrumMachineView::PadEditor : public juce::Component
    {
    public:
        static constexpr int contentHeight = 396;

        explicit PadEditor (DrumMachineView& o) : owner (o)
        {
            nameEditor.setTooltip ("The pad's name (Enter to apply)");
            nameEditor.onReturnKey = [this] { commitName(); owner.returnFocusToMainWindow(); };
            nameEditor.onEscapeKey = [this] { refresh (true); owner.returnFocusToMainWindow(); };
            nameEditor.onFocusLost = [this] { commitName(); };
            addAndMakeVisible (nameEditor);

            setupKnob (tune, DrumMachinePlugin::Value::tune, "Tune", DrumMachinePlugin::minTune, DrumMachinePlugin::maxTune, 0.1, tuneText,
                       "Tune: higher or lower in semitones (double-click: 0)");
            setupKnob (decay, DrumMachinePlugin::Value::decay, "Decay", DrumMachinePlugin::minDecay, DrumMachinePlugin::maxDecay, 0.01, percentText,
                       "Decay: how much of the sound plays before it fades (double-click: 100%)");
            setupKnob (start, DrumMachinePlugin::Value::start, "Start", DrumMachinePlugin::minStart, DrumMachinePlugin::maxStart, 0.01, percentText,
                       "Start: skip the beginning of the sound (double-click: 0%)");
            setupKnob (pan, DrumMachinePlugin::Value::pan, "Pan", -1.0, 1.0, 0.01, panText, "Pan: left or right (double-click: centre)");
            setupKnob (level, DrumMachinePlugin::Value::level, "Level", DrumMachinePlugin::minLevel, DrumMachinePlugin::maxLevel, 0.1, levelText,
                       "Level: how loud the pad is (double-click: 0 dB)");
            level.setSkewFactorFromMidPoint (-12.0);

            chokeBox.addItem ("None", 1);

            for (int g = 1; g <= DrumMachinePlugin::maxChokeGroup; ++g)
                chokeBox.addItem ("Group " + juce::String (g), g + 1);

            chokeBox.setTooltip ("Choke: pads in the same group cut each other off (a closed hat stops the open hat)");
            chokeBox.setScrollWheelEnabled (false);
            chokeBox.onChange = [this]
            {
                if (auto* drums = owner.getDrums())
                    owner.report (owner.session.setDrumPadChoke (*drums, owner.selectedPad, chokeBox.getSelectedId() - 1), "Could not change the pad");
            };
            addAndMakeVisible (chokeBox);

            muteButton.setTooltip ("Mute this pad (M)");
            soloButton.setTooltip ("Hear only soloed pads (S)");
            reverseButton.setTooltip ("Play the sound backwards");
            loadButton.setTooltip ("Load a sound from a file (or drop a file on a pad)");
            resetButton.setTooltip ("Back to the kit's sound and default values");
            muteButton.onClick = [this] { owner.toggleMute (owner.selectedPad); };
            soloButton.onClick = [this] { owner.toggleSolo (owner.selectedPad); };
            reverseButton.onClick = [this]
            {
                if (auto* drums = owner.getDrums())
                    owner.report (owner.session.setDrumPadReversed (*drums, owner.selectedPad, ! drums->getPad (owner.selectedPad).reversed), "Could not change the pad");
            };
            loadButton.onClick = [this] { owner.loadSampleWithChooser (owner.selectedPad); };
            resetButton.onClick = [this]
            {
                if (auto* drums = owner.getDrums())
                    owner.report (owner.session.resetDrumPad (*drums, owner.selectedPad), "Could not reset the pad");
            };

            for (auto* b : { &muteButton, &soloButton, &reverseButton, &loadButton, &resetButton })
            {
                b->setWantsKeyboardFocus (false);
                addAndMakeVisible (b);
            }
        }

        void refresh (bool forceName)
        {
            auto* drums = owner.getDrums();

            if (drums == nullptr)
                return;

            const auto s = drums->getPad (owner.selectedPad);

            if (forceName || ! nameEditor.hasKeyboardFocus (true))
            {
                nameEditor.setText (s.name, false);
                namePad = owner.selectedPad;   // a rename always goes to the pad whose name was shown
                nameDrums = owner.drumsID;
            }

            for (auto& k : knobs)
                if (! k.slider->isMouseButtonDown())
                    k.slider->setValue (valueOf (s, k.which), juce::dontSendNotification);

            chokeBox.setSelectedId (s.choke + 1, juce::dontSendNotification);
            muteButton.setToggleState (s.muted, juce::dontSendNotification);
            soloButton.setToggleState (s.soloed, juce::dontSendNotification);
            reverseButton.setToggleState (s.reversed, juce::dontSendNotification);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::panel);
            auto* drums = owner.getDrums();

            if (drums == nullptr)
                return;

            const auto s = drums->getPad (owner.selectedPad);

            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::captionFontSize, true));
            g.drawText (("Pad " + juce::String (owner.selectedPad + 1)).toUpperCase(), headingArea, juce::Justification::centredLeft, false);

            // Waveform, with the part that plays lit
            g.setColour (Theme::display);
            g.fillRoundedRectangle (waveArea.toFloat(), 4.0f);
            g.setColour (Theme::border);
            g.drawRoundedRectangle (waveArea.toFloat().reduced (0.5f), 4.0f, 1.0f);

            if (auto audio = drums->getPadAudio (owner.selectedPad))
            {
                const auto r = waveArea.reduced (4, 4).toFloat();
                const auto n = audio->getNumSamples();
                auto firstX = r.getX() + r.getWidth() * (float) s.start;
                auto lastX = firstX + (r.getRight() - firstX) * (float) s.decay;

                if (s.reversed)   // the drawing is mirrored: so is the part that plays
                {
                    const auto a = r.getRight() - (lastX - r.getX()), b = r.getRight() - (firstX - r.getX());
                    firstX = a;
                    lastX = b;
                }

                for (int x = 0; x < (int) r.getWidth(); ++x)
                {
                    const auto from = (int) ((double) x / r.getWidth() * n), to = juce::jmax (from + 1, (int) ((double) (x + 1) / r.getWidth() * n));
                    const auto peak = audio->getMagnitude (0, from, juce::jmin (to, n) - from);
                    const auto px = r.getX() + (float) x;
                    const auto wx = s.reversed ? r.getRight() - (float) x - 1.0f : px;
                    g.setColour (wx >= firstX && wx <= lastX ? juce::Colour (0xff8dbdf0) : juce::Colour (0xff3c4a5c));
                    const auto h = juce::jmin (1.0f, peak) * r.getHeight() * 0.5f;
                    g.drawVerticalLine ((int) wx, r.getCentreY() - h, r.getCentreY() + h + 1.0f);
                }

                g.setColour (Theme::selection.withAlpha (0.8f));
                g.drawVerticalLine ((int) firstX, r.getY(), r.getBottom());
                g.drawVerticalLine ((int) lastX, r.getY(), r.getBottom());
            }

            const auto error = drums->getPadLoadError (owner.selectedPad);
            g.setColour (error.isNotEmpty() ? Theme::danger : Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize));
            g.drawFittedText (error.isNotEmpty() ? error : DrumMachinePlugin::describeSource (s.file), sourceArea, juce::Justification::centredLeft, 2);

            g.setFont (Theme::font (Theme::smallFontSize));

            for (auto& k : knobs)
            {
                g.setColour (Theme::text);
                g.drawText (k.label, k.labelArea, juce::Justification::centred, false);
            }

            g.setColour (Theme::textDim);
            g.drawText ("Choke", chokeLabel, juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            const int pad = 10, w = getWidth() - 2 * pad;
            int y = 8;
            headingArea = { pad, y, w, 14 };
            y += 16;
            nameEditor.setBounds (pad, y, w, 24);
            y += 30;
            waveArea = { pad, y, w, 46 };
            y += 50;
            sourceArea = { pad, y, w, 28 };
            y += 32;

            const int columns = 3, knobSize = 46, cellW = w / columns;

            for (size_t i = 0; i < knobs.size(); ++i)
            {
                const auto col = (int) i % columns, row = (int) i / columns;
                const auto x = pad + col * cellW;
                const auto top = y + row * 82;
                knobs[i].labelArea = { x, top, cellW, 14 };
                knobs[i].slider->setBounds (x + (cellW - knobSize) / 2 - 6, top + 14, knobSize + 12, knobSize + 18);
            }

            y += 2 * 82 + 2;
            chokeLabel = { pad, y, 44, 24 };
            chokeBox.setBounds (pad + 48, y, w - 48, 24);
            y += 30;

            const auto third = (w - 8) / 3;
            muteButton.setBounds (pad, y, third, 24);
            soloButton.setBounds (pad + third + 4, y, third, 24);
            reverseButton.setBounds (pad + 2 * (third + 4), y, w - 2 * (third + 4), 24);
            y += 30;
            loadButton.setBounds (pad, y, (w - 4) / 2, 24);
            resetButton.setBounds (pad + (w - 4) / 2 + 4, y, w - (w - 4) / 2 - 4, 24);
            y += 30;
            jassert (y <= contentHeight);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (waveArea.contains (e.getPosition()))
                owner.selectPad (owner.selectedPad, true);
        }

    private:
        struct Knob
        {
            juce::Slider* slider;
            DrumMachinePlugin::Value which;
            juce::String label;
            juce::Rectangle<int> labelArea;
            bool dragging = false, started = false;
            juce::uint32 lastChangeMs = 0;
        };

        DrumMachineView& owner;
        juce::TextEditor nameEditor;
        juce::Slider tune, decay, start, pan, level;
        std::vector<Knob> knobs;
        juce::ComboBox chokeBox;
        IconButton muteButton { "Mute", Icon::none, "Mute" }, soloButton { "Solo", Icon::none, "Solo" }, reverseButton { "Reverse", Icon::none, "Reverse" };
        IconButton loadButton { "Load sound", Icon::open, "Load..." }, resetButton { "Reset pad", Icon::undo, "Reset" };
        juce::Rectangle<int> headingArea, waveArea, sourceArea, chokeLabel;

        static double valueOf (const DrumMachinePlugin::PadSettings& s, DrumMachinePlugin::Value v)
        {
            switch (v)
            {
                case DrumMachinePlugin::Value::tune:   return s.tune;
                case DrumMachinePlugin::Value::decay:  return s.decay;
                case DrumMachinePlugin::Value::start:  return s.start;
                case DrumMachinePlugin::Value::pan:    return s.pan;
                case DrumMachinePlugin::Value::level:  return s.level;
            }

            return 0.0;
        }

    public:
        /** Applies a name being typed, to the pad it was typed for. */
        void commitName()
        {
            auto* drums = owner.getDrums();

            if (drums == nullptr || nameDrums != owner.drumsID || namePad < 0)
            {
                refresh (true);
                return;
            }

            const auto name = nameEditor.getText().trim();

            if (name.isNotEmpty() && name != drums->getPad (namePad).name)
                owner.report (owner.session.renameDrumPad (*drums, namePad, name), "Could not rename the pad");

            refresh (true);
        }

        bool isTypingName() const   { return nameEditor.hasKeyboardFocus (true); }

    private:
        int namePad = -1;
        te::EditItemID nameDrums;

        void setupKnob (juce::Slider& s, DrumMachinePlugin::Value which, const juce::String& label, double lo, double hi, double step,
                        std::function<juce::String (double)> toText, const juce::String& tip)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 16);
            s.setRange (lo, hi, step);
            s.setDoubleClickReturnValue (true, DrumMachinePlugin::defaultFor (which));
            s.setTooltip (tip);
            s.textFromValueFunction = toText;
            s.valueFromTextFunction = [which] (const juce::String& text)
            {
                auto v = parseNumber (text);

                if (which == DrumMachinePlugin::Value::decay || which == DrumMachinePlugin::Value::start)
                    v /= 100.0;
                else if (which == DrumMachinePlugin::Value::pan)
                    v = (text.trim().startsWithIgnoreCase ("L") ? -1.0 : 1.0) * std::abs (v) / 100.0;

                return DrumMachinePlugin::clampValue (which, v);
            };
            s.setColour (juce::Slider::rotarySliderFillColourId, Theme::accent);
            s.setColour (juce::Slider::rotarySliderOutlineColourId, Theme::border);
            s.setColour (juce::Slider::thumbColourId, Theme::text);
            s.setColour (juce::Slider::textBoxTextColourId, Theme::text);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s.updateText();   // the box was written before toText existed ("0.0" instead of "0 st" until the value changed)
            addAndMakeVisible (s);

            const auto index = knobs.size();
            knobs.push_back ({ &s, which, label, {} });

            // A drag (or a wheel turn) is one undo step; a typed value or double-click is its own
            s.onDragStart = [this, index] { knobs[index].dragging = true; knobs[index].started = false; };
            s.onDragEnd = [this, index] { knobs[index].dragging = false; };
            s.onValueChange = [this, index]
            {
                auto& k = knobs[index];
                auto* drums = owner.getDrums();

                if (drums == nullptr)
                    return;

                // A wheel turn (no drag) is one step too: changes close together continue it
                const auto now = juce::Time::getMillisecondCounter();

                if (! k.dragging && now - k.lastChangeMs > 400)
                    k.started = false;

                k.lastChangeMs = now;

                owner.report (owner.session.setDrumPadValue (*drums, owner.selectedPad, k.which, k.slider->getValue(), k.started), "Could not change the pad");
                k.started = true;
            };
        }
    };

    //==============================================================================
    /** The step rows: name, steps, M and S for each pad. Scrolls vertically. */
    class DrumMachineView::StepGrid : public juce::Component
    {
    public:
        explicit StepGrid (DrumMachineView& o) : owner (o)   { setOpaque (true); }

        int scrollY = 0, scrollX = 0, playingStep = -1;
        static constexpr int minStepWidth = 20;   // narrower than this and they scroll sideways instead
        std::vector<int> cells;
        std::vector<int> hits;   // notes per step   // velocity of the hit in each [pad][step], 0 = none

        int getContentHeight() const        { return DrumMachinePlugin::numPads * (rowHeight + rowGap); }
        int getVisibleRowsHeight() const    { return getHeight() - headerHeight; }

        void rebuildCells (te::MidiClip* clip)
        {
            const auto steps = owner.getNumSteps();
            cells.assign ((size_t) (DrumMachinePlugin::numPads * steps), 0);
            hits.assign ((size_t) (DrumMachinePlugin::numPads * steps), 0);

            if (clip == nullptr)
                return;

            const auto pageStart = owner.getPageStart(), stepBeats = owner.getStepBeats();

            for (auto* n : owner.session.getNotesInRange (*clip, -1, pageStart, pageStart + owner.getPatternBeats()))
            {
                const auto pad = DrumMachinePlugin::padForNote (n->getNoteNumber());
                const auto step = (int) std::floor ((n->getStartBeat().inBeats() - pageStart) / stepBeats + 1.0e-6);

                if (pad >= 0 && juce::isPositiveAndBelow (step, steps))
                {
                    cells[(size_t) (pad * steps + step)] = juce::jmax (cells[(size_t) (pad * steps + step)], n->getVelocity());
                    ++hits[(size_t) (pad * steps + step)];
                }
            }
        }

        /** How many hits the step has (2 or more: a roll). */
        int cellHits (int pad, int step) const
        {
            const auto steps = owner.getNumSteps();
            const auto i = (size_t) (pad * steps + step);
            return juce::isPositiveAndBelow (pad, DrumMachinePlugin::numPads) && juce::isPositiveAndBelow (step, steps) && i < hits.size() ? hits[i] : 0;
        }

        int cellVelocity (int pad, int step) const
        {
            const auto steps = owner.getNumSteps();
            const auto i = (size_t) (pad * steps + step);
            return juce::isPositiveAndBelow (pad, DrumMachinePlugin::numPads) && juce::isPositiveAndBelow (step, steps) && i < cells.size() ? cells[i] : 0;
        }

        juce::Rectangle<int> cellsArea() const    { return { labelWidth, headerHeight, juce::jmax (0, getWidth() - labelWidth - msWidth), getVisibleRowsHeight() }; }

        /** All the steps side by side: the visible width, or wider (then they scroll). */
        int getContentWidth() const     { return juce::jmax (cellsArea().getWidth(), owner.getNumSteps() * minStepWidth); }
        int getMaxScrollX() const       { return juce::jmax (0, getContentWidth() - cellsArea().getWidth()); }

        float stepX (int step) const
        {
            const auto area = cellsArea();
            return (float) (area.getX() - scrollX) + (float) getContentWidth() * (float) step / (float) owner.getNumSteps();
        }

        /** The step under x, or -1 off the steps (over the names, M/S, or scrolled out of view). */
        int stepAtX (int x) const
        {
            const auto area = cellsArea();

            if (area.getWidth() <= 0 || x < area.getX() || x >= area.getRight())
                return -1;

            return (int) std::floor ((double) (x - area.getX() + scrollX) * owner.getNumSteps() / getContentWidth());
        }

        int rowAtY (int y) const    { return (y - headerHeight + scrollY) / (rowHeight + rowGap); }
        int rowY (int row) const    { return headerHeight + row * (rowHeight + rowGap) - scrollY; }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::panel);
            auto* drums = owner.getDrums();

            if (drums == nullptr)
                return;

            const auto steps = owner.getNumSteps();
            const auto area = cellsArea();
            const auto beatSteps = juce::jmax (1, juce::roundToInt (1.0 / owner.getStepBeats()));   // steps in a beat (for shading)

            // Step numbers (only over the steps: they scroll under the names and M/S)
            {
                juce::Graphics::ScopedSaveState state (g);
                g.reduceClipRegion (area.getX(), 0, area.getWidth(), headerHeight);
                g.setFont (Theme::font (Theme::captionFontSize - 0.5f));

                for (int s = 0; s < steps; ++s)
                {
                    const auto x1 = stepX (s), x2 = stepX (s + 1);
                    g.setColour (s == playingStep ? Theme::text : Theme::textDim);

                    if (x2 - x1 >= 12.0f || s % beatSteps == 0)
                        g.drawText (juce::String (s + 1), juce::Rectangle<float> (x1, 0.0f, juce::jmax (12.0f, x2 - x1), (float) headerHeight),
                                    juce::Justification::centred, false);
                }
            }

            g.saveState();
            g.reduceClipRegion (0, headerHeight, getWidth(), getVisibleRowsHeight());

            for (int row = 0; row < DrumMachinePlugin::numPads; ++row)
            {
                const auto y = rowY (row);

                if (y + rowHeight < headerHeight || y > getHeight())
                    continue;

                const auto pad = row;
                const auto s = drums->getPad (pad);
                const bool isSelected = pad == owner.selectedPad;
                const auto colour = DrumKits::getPadColour (pad);

                g.setColour (isSelected ? Theme::selection : s.muted ? Theme::textDim : Theme::text);
                g.setFont (Theme::font (Theme::smallFontSize, isSelected));
                g.drawText (s.name, juce::Rectangle<int> (4, y, labelWidth - 8, rowHeight), juce::Justification::centredLeft, true);

                {   // the steps, clipped to their area (they scroll under the names and M/S)
                    juce::Graphics::ScopedSaveState rowState (g);
                    g.reduceClipRegion (area.getX(), y, area.getWidth(), rowHeight);

                    for (int step = 0; step < steps; ++step)
                    {
                        const auto x1 = stepX (step) + 1.5f, x2 = stepX (step + 1) - 1.5f;

                        if (x2 < (float) area.getX() || x1 > (float) area.getRight())
                            continue;   // scrolled out of view

                        const auto cell = juce::Rectangle<float> (x1, (float) y, juce::jmax (2.0f, x2 - x1), (float) rowHeight);
                        const auto velocity = cellVelocity (pad, step);
                        const bool beatGroup = (step / beatSteps) % 2 == 0;

                        if (velocity > 0)
                            g.setColour ((s.muted ? colour.withSaturation (0.15f) : colour).withMultipliedBrightness (0.55f + 0.45f * (float) velocity / 127.0f));
                        else
                            g.setColour (beatGroup ? Theme::raised : Theme::panel);

                        g.fillRoundedRectangle (cell, 3.0f);
                        g.setColour (step == playingStep ? Theme::playhead.withAlpha (0.7f) : Theme::laneSeparator);
                        g.drawRoundedRectangle (cell.reduced (0.5f), 3.0f, 1.0f);

                        if (const auto n = cellHits (pad, step); n > 1 && velocity > 0)
                        {
                            // A roll: one tick per hit along the bottom of the cell
                            g.setColour (Theme::ink.withAlpha (0.75f));
                            const auto tickArea = cell.reduced (3.0f, 0.0f).withTrimmedTop (cell.getHeight() * 0.62f).withTrimmedBottom (2.0f);
                            const auto w = tickArea.getWidth() / (float) n;

                            for (int k = 0; k < n; ++k)
                                g.fillRect (tickArea.getX() + w * (float) k + w * 0.2f, tickArea.getY(), juce::jmax (1.0f, w * 0.6f), tickArea.getHeight());
                        }

                        if (velocity >= accentVelocity)
                        {
                            g.setColour (Theme::ink.withAlpha (0.7f));
                            g.fillEllipse (cell.getCentreX() - 2.0f, cell.getCentreY() - 2.0f, 4.0f, 4.0f);
                        }
                    }
                }

                // M and S
                const auto ms = juce::Rectangle<int> (getWidth() - msWidth + 4, y + 2, msWidth - 4, rowHeight - 4);
                const auto m = ms.withWidth (ms.getWidth() / 2 - 1), so = ms.withTrimmedLeft (ms.getWidth() / 2 + 1);
                drawToggle (g, m, "M", s.muted, Theme::danger);
                drawToggle (g, so, "S", s.soloed, Theme::accent);
            }

            g.restoreState();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            painting = false;
            auto* drums = owner.getDrums();

            if (drums == nullptr || e.y < headerHeight)
                return;

            const auto row = rowAtY (e.y);

            if (! juce::isPositiveAndBelow (row, DrumMachinePlugin::numPads))
                return;

            const auto pad = row;

            if (e.x < labelWidth)   // the row's name
            {
                owner.selectPad (pad, ! e.mods.isPopupMenu());

                if (e.mods.isPopupMenu())
                    owner.showPadMenu (pad);

                return;
            }

            if (e.x >= getWidth() - msWidth)   // M / S
            {
                if (e.x < getWidth() - msWidth / 2)   owner.toggleMute (pad);
                else                                   owner.toggleSolo (pad);

                return;
            }

            auto* clip = owner.getClip();
            const auto step = stepAtX (e.x);

            if (clip == nullptr || ! juce::isPositiveAndBelow (step, owner.getNumSteps()))
                return;

            owner.selectPad (pad, false);

            if (e.mods.isPopupMenu())
            {
                owner.showStepMenu (pad, step);
                return;
            }

            const auto pitch = DrumMachinePlugin::noteForPad (pad);
            const auto stepStart = owner.getPageStart() + step * owner.getStepBeats();
            const auto velocity = cellVelocity (pad, step);

            if (e.mods.isAltDown())   // the next roll: 1, 2, 3, 4, 6, 8 hits
            {
                const auto current = juce::jmax (1, cellHits (pad, step));
                static const int rolls[] = { 1, 2, 3, 4, 6, 8 };
                int next = 2;

                for (int i = 0; i < (int) std::size (rolls); ++i)
                    if (rolls[i] == current)
                        next = rolls[(i + 1) % (int) std::size (rolls)];

                owner.report (owner.session.setDrumRoll (*clip, pitch, stepStart, owner.getStepBeats(), next, defaultVelocity), "Could not make the roll");
                owner.session.showHint (next == 1 ? juce::String ("Single hit (Alt+click a step for a roll)")
                                                  : "Roll: " + juce::String (next) + " hits in the step (Alt+click again for more)");
                owner.session.auditionDrumPad (*drums, pad, velocity > 0 ? velocity : defaultVelocity);
                owner.refreshAll();
                return;
            }

            if (e.mods.isShiftDown())   // accent on / off
            {
                if (velocity > 0)
                {
                    const auto notes = owner.session.getNotesInRange (*clip, pitch, stepStart, stepStart + owner.getStepBeats());
                    owner.report (owner.session.setMidiNoteVelocity (*clip, notes, velocity >= accentVelocity ? defaultVelocity : accentVelocity), "Could not change the step");
                }
                else
                {
                    owner.report (owner.session.setDrumStep (*clip, pitch, stepStart, owner.getStepBeats(), true, accentVelocity, false, nullptr,
                                                             owner.getStepStart (*clip, step)), "Could not change the step");
                    owner.session.auditionDrumPad (*drums, pad, accentVelocity);
                }

                owner.refreshAll();
                return;
            }

            // Paint: every step you drag over becomes what the first one became
            painting = true;
            paintOn = velocity == 0;
            lastPad = pad;
            lastStep = step;
            apply (*clip, pad, step, false);

            if (paintOn)
                owner.session.auditionDrumPad (*drums, pad, defaultVelocity);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto* clip = owner.getClip();

            if (! painting || clip == nullptr)
                return;

            const auto pad = juce::jlimit (0, DrumMachinePlugin::numPads - 1, rowAtY (juce::jlimit (headerHeight, getHeight() - 1, e.y)));
            // Painting past an edge stays on the edge step (and a drag past the visible ones scrolls to them)
            const auto area = cellsArea();

            if (e.x >= area.getRight())        owner.setStepScroll (scrollX + minStepWidth / 2);
            else if (e.x < area.getX())        owner.setStepScroll (scrollX - minStepWidth / 2);

            const auto step = juce::jlimit (0, owner.getNumSteps() - 1, stepAtX (juce::jlimit (area.getX(), juce::jmax (area.getX(), area.getRight() - 1), e.x)));

            if (pad == lastPad && step == lastStep)
                return;

            lastPad = pad;
            lastStep = step;
            apply (*clip, pad, step, true);
        }

        void mouseUp (const juce::MouseEvent&) override    { painting = false; }
        void stopPainting()                                 { painting = false; }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            // Sideways: a sideways swipe, or Shift+wheel. Otherwise up and down through the rows
            const bool sideways = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) || e.mods.isShiftDown();

            if (sideways)
            {
                const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
                owner.setStepScroll (scrollX - juce::roundToInt (delta * 160.0f));
                return;
            }

            scrollY = juce::jlimit (0, juce::jmax (0, getContentHeight() - getVisibleRowsHeight()), scrollY - juce::roundToInt (wheel.deltaY * 120.0f));
            owner.rowScroll.setCurrentRangeStart (scrollY, juce::dontSendNotification);
            repaint();
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            setMouseCursor (e.x >= labelWidth && e.x < getWidth() - msWidth && e.y >= headerHeight ? juce::MouseCursor::PointingHandCursor
                                                                                                    : juce::MouseCursor::NormalCursor);
        }

    private:
        DrumMachineView& owner;
        bool painting = false, paintOn = true;
        int lastPad = -1, lastStep = -1;

        void apply (te::MidiClip& clip, int pad, int step, bool continues)
        {
            const auto stepStart = owner.getPageStart() + step * owner.getStepBeats();

            if (! owner.session.setDrumStep (clip, DrumMachinePlugin::noteForPad (pad), stepStart, owner.getStepBeats(), paintOn,
                                             defaultVelocity, continues, nullptr, owner.getStepStart (clip, step)))
            {
                painting = false;
                owner.report (false, "Could not change the step");
            }

            owner.refreshAll();
        }

        static void drawToggle (juce::Graphics& g, juce::Rectangle<int> r, const char* text, bool on, juce::Colour onColour)
        {
            g.setColour (on ? onColour.withAlpha (0.85f) : Theme::raised);
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (on ? Theme::ink : Theme::textDim);
            g.setFont (Theme::font (Theme::captionFontSize, true));
            g.drawText (text, r, juce::Justification::centred, false);
        }
    };

    //==============================================================================
    /** Velocity of the selected pad's hits in this pattern. Drag across to change them. */
    class DrumMachineView::VelocityLane : public juce::Component,
                                          public juce::SettableTooltipClient
    {
    public:
        explicit VelocityLane (DrumMachineView& o) : owner (o)
        {
            setOpaque (true);
            setTooltip ("Velocity of the selected pad's steps: drag across the bars to change them");
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::panel);
            auto* drums = owner.getDrums();

            if (drums == nullptr)
                return;

            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::captionFontSize));
            g.drawFittedText (drums->getPad (owner.selectedPad).name + "\nvelocity", juce::Rectangle<int> (4, 0, labelWidth - 8, getHeight()),
                              juce::Justification::centredLeft, 2);

            const auto area = juce::Rectangle<int> (labelWidth, 2, juce::jmax (0, getWidth() - labelWidth - msWidth), getHeight() - 4);
            g.setColour (Theme::display);
            g.fillRoundedRectangle (area.toFloat(), 3.0f);

            const auto steps = owner.getNumSteps();
            const auto colour = DrumKits::getPadColour (owner.selectedPad);
            g.reduceClipRegion (area);   // the stems scroll with the steps

            for (int step = 0; step < steps; ++step)
            {
                const auto velocity = owner.steps->cellVelocity (owner.selectedPad, step);

                if (velocity <= 0)
                    continue;

                const auto x1 = owner.steps->stepX (step) + 2.0f, x2 = owner.steps->stepX (step + 1) - 2.0f;
                const auto h = (float) (area.getHeight() - 4) * (float) velocity / 127.0f;
                g.setColour (colour);
                g.fillRoundedRectangle (x1, (float) area.getBottom() - 2.0f - h, juce::jmax (2.0f, x2 - x1), h, 2.0f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override    { started = false; apply (e); }
        void mouseDrag (const juce::MouseEvent& e) override    { apply (e); }

    private:
        DrumMachineView& owner;
        bool started = false;

        void apply (const juce::MouseEvent& e)
        {
            auto* clip = owner.getClip();

            if (clip == nullptr)
                return;

            const auto step = owner.steps->stepAtX (e.x);

            if (! juce::isPositiveAndBelow (step, owner.getNumSteps()))
                return;

            const auto stepStart = owner.getPageStart() + step * owner.getStepBeats();
            const auto notes = owner.session.getNotesInRange (*clip, DrumMachinePlugin::noteForPad (owner.selectedPad), stepStart, stepStart + owner.getStepBeats());

            if (notes.isEmpty())
                return;

            const auto velocity = juce::jlimit (1, 127, juce::roundToInt (127.0f * (1.0f - (float) (e.y - 4) / (float) juce::jmax (1, getHeight() - 8))));

            if (owner.session.setMidiNoteVelocity (*clip, notes, velocity, started))
                started = true;   // the rest of the drag joins this undo step

            owner.refreshAll();
        }
    };

    //==============================================================================
    DrumMachineView::DrumMachineView (Session& s) : session (s)
    {
        for (int i = 0; i < DrumMachinePlugin::numPads; ++i)
            addChildComponent (pads.add (new Pad (*this, i)));

        editor = std::make_unique<PadEditor> (*this);
        editorViewport.setViewedComponent (editor.get(), false);
        editorViewport.setScrollBarsShown (true, false);
        editorViewport.setScrollBarThickness (Theme::scrollBarSize);
        addChildComponent (editorViewport);

        steps = std::make_unique<StepGrid> (*this);
        velocityLane = std::make_unique<VelocityLane> (*this);
        addChildComponent (*steps);
        addChildComponent (*velocityLane);
        rowScroll.setAutoHide (false);
        rowScroll.addListener (this);
        addChildComponent (rowScroll);
        stepScroll.setAutoHide (false);   // shown by updateStepScroll, only when the steps don't fit
        stepScroll.addListener (this);
        addChildComponent (stepScroll);

        // Kits, by genre
        genreBox.addItem ("All genres", 1);
        genreBox.addItemList (DrumKits::getGenres(), 2);
        genreBox.setSelectedId (1, juce::dontSendNotification);
        genreBox.setTooltip ("Show the kits for one style of music");
        genreBox.onChange = [this] { refreshToolbar(); };
        kitBox.setTooltip ("Drum kit: every pad gets the kit's sound (your steps stay)");
        kitBox.onChange = [this]
        {
            auto* drums = getDrums();
            const auto kit = kitBox.getSelectedId() - 1;

            if (drums != nullptr && kit >= 0 && juce::String (DrumKits::getKit (kit).name) != drums->getKitName())
                report (session.loadDrumKit (*drums, kit), "Could not load the kit");
        };

        for (int i = 0; i < numPages; ++i)
        {
            auto* b = patternButtons.add (new IconButton ("Pattern " + juce::String (patternNames[i]), Icon::none, patternNames[i]));
            b->setTooltip ("Pattern " + juce::String (patternNames[i]) + " (" + juce::String (i + 1) + "): part " + juce::String (i + 1) + " of the clip, one Length long");
            b->onClick = [this, i] { setPage (i); };
            addChildComponent (b);
        }

        for (auto n : stepsChoices)   stepsBox.addItem (n % 3 == 0 ? juce::String (n) + " (triplets)" : juce::String (n), n);
        for (auto n : lengthChoices)  lengthBox.addItem (n == 1 ? "1 bar" : juce::String (n) + " bars", n);
        for (auto n : swingChoices)   swingBox.addItem (juce::String (n) + "%", n);

        stepsBox.setTooltip ("How many steps the pattern has (1 bar with 16 steps = sixteenth notes; 12 and 24 are triplets)");
        lengthBox.setTooltip ("How long one pattern is");
        swingBox.setTooltip ("Swing: 50% is straight, 66% is a triplet shuffle. Moves this pattern's off-beat steps.");
        stepsBox.onChange = [this] { setStepsIndex (stepsBox.getSelectedItemIndex()); };
        lengthBox.onChange = [this] { setLengthIndex (lengthBox.getSelectedItemIndex()); };
        swingBox.onChange = [this] { setSwingIndex (swingBox.getSelectedItemIndex()); };

        fillClipButton.setTooltip ("Repeat this pattern to the end of the clip (replaces what is after it)");
        fillClipButton.onClick = [this] { fillClip(); };
        newClipButton.setTooltip ("Make a 4-bar pattern clip on this track at the playhead");
        newClipButton.onClick = [this] { newClip(); };
        editorButton.setTooltip ("Show or hide the pad editor (it needs a wide enough panel to leave room for the steps)");
        editorButton.onClick = [this] { editorOverride = ! isEditorShown(); resized(); };

        for (auto* c : std::initializer_list<juce::Component*> { &genreBox, &kitBox, &stepsBox, &lengthBox, &swingBox, &fillClipButton, &newClipButton, &editorButton })
        {
            c->setWantsKeyboardFocus (false);
            addChildComponent (c);
        }

        for (auto* b : patternButtons)
            b->setWantsKeyboardFocus (false);

        for (auto* box : { &genreBox, &kitBox, &stepsBox, &lengthBox, &swingBox })
            box->setScrollWheelEnabled (false);

        session.addListener (this);
        startTimerHz (30);
    }

    DrumMachineView::~DrumMachineView()
    {
        session.removeListener (this);
    }

    void DrumMachineView::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
    {
        if (bar == &stepScroll)
        {
            setStepScroll (juce::roundToInt (newRangeStart));
            return;
        }

        steps->scrollY = juce::roundToInt (newRangeStart);
        steps->repaint();
    }

    void DrumMachineView::setStepScroll (int newScrollX)
    {
        const auto clamped = juce::jlimit (0, steps->getMaxScrollX(), newScrollX);

        if (clamped != steps->scrollX)
        {
            steps->scrollX = clamped;
            steps->repaint();
            velocityLane->repaint();
        }

        stepScroll.setCurrentRangeStart (steps->scrollX, juce::dontSendNotification);
    }

    void DrumMachineView::updateStepScroll()
    {
        const auto visible = steps->cellsArea().getWidth();
        steps->scrollX = juce::jlimit (0, steps->getMaxScrollX(), steps->scrollX);
        stepScroll.setRangeLimits (0.0, (double) steps->getContentWidth(), juce::dontSendNotification);
        stepScroll.setCurrentRange ((double) steps->scrollX, (double) visible, juce::dontSendNotification);
        stepScroll.setVisible (steps->isVisible() && steps->getMaxScrollX() > 0);
    }

    void DrumMachineView::sessionSongClosing()
    {
        // Item IDs restart in every song
        drumsID = {};
        clipID = {};
        settingsPerClip.clear();
        songWasClosed = true;
    }

    //==============================================================================
    DrumMachineView::Target DrumMachineView::resolveTarget() const
    {
        if (! session.hasSong())
            return {};

        const auto selected = session.getSelectedClips();

        if (selected.size() == 1)
            if (auto* clip = dynamic_cast<te::MidiClip*> (selected.getFirst()))
                if (auto* drums = session.getDrumMachineFor (*clip))
                    return { drums, clip };

        const auto track = getFocusedTrack ? getFocusedTrack() : -1;
        return { session.getDrumMachine (track), nullptr };
    }

    bool DrumMachineView::hasSomethingToShow() const    { return resolveTarget().drums != nullptr; }
    bool DrumMachineView::hasClip() const               { return resolveTarget().clip != nullptr; }

    DrumMachinePlugin* DrumMachineView::getDrums() const
    {
        if (! drumsID.isValid())
            return nullptr;

        for (int i = 0; i < session.getNumAudioTracks(); ++i)
            if (auto* drums = session.getDrumMachine (i))
                if (drums->itemID == drumsID)
                    return drums;

        return nullptr;
    }

    te::MidiClip* DrumMachineView::getClip() const
    {
        auto* edit = const_cast<Session&> (session).getEdit();

        if (edit == nullptr || ! clipID.isValid())
            return nullptr;

        auto* clip = dynamic_cast<te::MidiClip*> (te::findClipForID (*edit, clipID));
        return clip != nullptr && clip->getClipTrack() != nullptr ? clip : nullptr;
    }

    void DrumMachineView::retarget (const Target& t)
    {
        finishTyping();
        rememberPattern();
        drumsID = t.drums != nullptr ? t.drums->itemID : te::EditItemID();
        clipID = t.clip != nullptr ? t.clip->itemID : te::EditItemID();

        const auto found = settingsPerClip.find (clipID);
        pattern = found != settingsPerClip.end() ? found->second : PatternSettings();

        steps->stopPainting();
        lastSignature = 0;
        resized();
        refreshAll();
    }

    void DrumMachineView::rememberPattern()
    {
        if (clipID.isValid())
            settingsPerClip[clipID] = pattern;
    }

    //==============================================================================
    int DrumMachineView::getNumSteps() const        { return stepsChoices[juce::jlimit (0, (int) std::size (stepsChoices) - 1, pattern.stepsIndex)]; }
    double DrumMachineView::getPatternBeats() const
    {
        // Bars as long as the time signature where the clip starts
        auto* clip = getClip();
        const auto beatsPerBar = session.getBeatsPerBarAt (clip != nullptr ? clip->getPosition().getStart() : te::TimePosition());
        return lengthChoices[juce::jlimit (0, (int) std::size (lengthChoices) - 1, pattern.lengthIndex)] * beatsPerBar;
    }

    double DrumMachineView::getSwingAmount() const
    {
        // MPC-style percent -> how far (in half steps) every second step is delayed
        return (swingChoices[juce::jlimit (0, (int) std::size (swingChoices) - 1, pattern.swingIndex)] - 50) / 25.0;
    }

    double DrumMachineView::getStepStart (te::MidiClip& clip, int step) const
    {
        const auto plain = getPageStart() + step * getStepBeats();
        const auto swing = getSwingAmount();

        if (swing <= 0.0)
            return plain;

        // Same rule as quantise: odd lines of the song's grid are late
        const auto songStep = juce::roundToInt ((session.getContentStartInSongBeats (clip) + plain) / getStepBeats());
        return songStep % 2 != 0 ? plain + swing * 0.5 * getStepBeats() : plain;
    }

    int DrumMachineView::getNumPages() const
    {
        auto* clip = getClip();

        if (clip == nullptr)
            return 1;

        const auto end = clip->getContentBeatAtTime (clip->getPosition().getEnd()).inBeats();
        return juce::jlimit (1, numPages, (int) std::ceil (end / getPatternBeats() - 1.0e-6));
    }

    void DrumMachineView::setPage (int page)
    {
        pattern.page = juce::jlimit (0, getNumPages() - 1, page);   // never past the clip's end
        rememberPattern();
        refreshAll();
    }

    void DrumMachineView::setStepsIndex (int index)
    {
        pattern.stepsIndex = juce::jlimit (0, (int) std::size (stepsChoices) - 1, index);
        rememberPattern();
        refreshAll();
    }

    void DrumMachineView::setLengthIndex (int index)
    {
        pattern.lengthIndex = juce::jlimit (0, (int) std::size (lengthChoices) - 1, index);
        pattern.page = juce::jmin (pattern.page, getNumPages() - 1);
        rememberPattern();
        refreshAll();
    }

    void DrumMachineView::setSwingIndex (int index)
    {
        pattern.swingIndex = juce::jlimit (0, (int) std::size (swingChoices) - 1, index);
        rememberPattern();

        // Move this pattern's hits onto the new swing (straight grid lines, every second one late)
        if (auto* clip = getClip())
        {
            const auto notes = session.getNotesInRange (*clip, -1, getPageStart(), getPageStart() + getPatternBeats());

            if (! notes.isEmpty())
                report (session.quantiseMidiNotes (*clip, notes, getStepBeats(), 1.0, getSwingAmount()), "Could not apply the swing");
        }

        refreshAll();
    }

    //==============================================================================
    void DrumMachineView::selectPad (int pad, bool play, int velocity)
    {
        finishTyping();
        selectedPad = juce::jlimit (0, DrumMachinePlugin::numPads - 1, pad);
        focus.setFocus (true);

        if (play)
            if (auto* drums = getDrums())
                session.auditionDrumPad (*drums, selectedPad, velocity);

        // Keep the pad's row in view
        const auto y = selectedPad * (rowHeight + rowGap);

        if (y < steps->scrollY)
            steps->scrollY = y;
        else if (y + rowHeight > steps->scrollY + steps->getVisibleRowsHeight())
            steps->scrollY = juce::jmax (0, y + rowHeight - steps->getVisibleRowsHeight());

        rowScroll.setCurrentRangeStart (steps->scrollY, juce::dontSendNotification);
        refreshAll();
    }

    void DrumMachineView::loadSampleWithChooser (int pad)
    {
        selectPad (pad, false);
        chooser = std::make_unique<juce::FileChooser> ("Load a sound onto pad " + juce::String (pad + 1),
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<DrumMachineView> safe (this);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, pad] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && fc.getResult() != juce::File())
                safe->loadSample (pad, fc.getResult());
        });
    }

    void DrumMachineView::loadSample (int pad, const juce::File& file)
    {
        if (auto* drums = getDrums())
        {
            report (session.loadDrumPadSample (*drums, pad, file), "Could not load " + file.getFileName());
            selectPad (pad, false);
        }
    }

    void DrumMachineView::toggleMute (int pad)
    {
        if (auto* drums = getDrums())
            report (session.setDrumPadMuted (*drums, pad, ! drums->getPad (pad).muted), "Could not change the pad");

        refreshAll();
    }

    void DrumMachineView::toggleSolo (int pad)
    {
        if (auto* drums = getDrums())
            report (session.setDrumPadSoloed (*drums, pad, ! drums->getPad (pad).soloed), "Could not change the pad");

        refreshAll();
    }

    void DrumMachineView::fillRow (int pad, int every)
    {
        if (auto* clip = getClip())
            report (session.fillDrumRow (*clip, DrumMachinePlugin::noteForPad (pad), getPageStart(), getPageStart() + getPatternBeats(),
                                         getStepBeats(), every, defaultVelocity), "Could not fill the row");

        refreshAll();
    }

    void DrumMachineView::shiftRow (int pad, int stepCount)
    {
        if (auto* clip = getClip())
            report (session.shiftDrumRow (*clip, pad < 0 ? -1 : DrumMachinePlugin::noteForPad (pad), getPageStart(), getPageStart() + getPatternBeats(),
                                          stepCount * getStepBeats()), "Could not shift the steps");

        refreshAll();
    }

    void DrumMachineView::clearRow (int pad)
    {
        if (auto* clip = getClip())
            report (session.clearDrumRow (*clip, pad < 0 ? -1 : DrumMachinePlugin::noteForPad (pad), getPageStart(), getPageStart() + getPatternBeats()),
                    "Could not clear the steps");

        refreshAll();
    }

    void DrumMachineView::copyPatternTo (int page)
    {
        if (auto* clip = getClip())
        {
            if (page >= getNumPages())
            {
                report (false, "Could not copy the pattern",
                        "Make the clip longer first: pattern " + juce::String (patternNames[juce::jlimit (0, numPages - 1, page)]) + " is past its end.");
                return;
            }

            report (session.copyDrumPattern (*clip, getPageStart(), getPageStart() + getPatternBeats(), page * getPatternBeats()), "Could not copy the pattern");
        }

        refreshAll();
    }

    void DrumMachineView::fillClip()
    {
        if (auto* clip = getClip())
            report (session.repeatDrumPatternToEnd (*clip, getPageStart(), getPageStart() + getPatternBeats()), "Could not fill the clip");

        refreshAll();
    }

    void DrumMachineView::newClip()
    {
        auto* drums = getDrums();
        auto* edit = session.getEdit();

        if (drums == nullptr || edit == nullptr)
            return;

        int trackIndex = -1;

        for (int i = 0; i < session.getNumAudioTracks(); ++i)
            if (session.getDrumMachine (i) == drums)
                trackIndex = i;

        // At the bar the playhead is in, 4 bars long (bars follow the time signature)
        const auto bar = session.getBarAt (edit->tempoSequence.toBeats (session.getPosition()).inBeats()).first;
        const auto start = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (session.getBarStartBeat (bar)));
        const auto end = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (session.getBarStartBeat (bar + 4)));

        if (! session.createMidiClip (trackIndex, start, end - start, "Pattern"))   // one undo step
        {
            report (false, "Could not make a clip");
            return;
        }
    }

    //==============================================================================
    void DrumMachineView::showPadMenu (int pad)
    {
        auto* drums = getDrums();

        if (drums == nullptr)
            return;

        const auto s = drums->getPad (pad);
        const bool hasClipNow = getClip() != nullptr;
        juce::PopupMenu menu, copyMenu, chokeMenu, fillMenu;

        menu.addSectionHeader ("Pad " + juce::String (pad + 1) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + s.name);
        menu.addItem (1, "Play");
        menu.addItem (2, "Load sound...");
        menu.addItem (3, "Reset to the kit's sound");
        menu.addSeparator();

        for (int other = 0; other < DrumMachinePlugin::numPads; ++other)
            if (other != pad)
                copyMenu.addItem (100 + other, "Pad " + juce::String (other + 1) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + drums->getPad (other).name);

        menu.addSubMenu ("Copy this pad to", copyMenu);

        for (int g = 0; g <= DrumMachinePlugin::maxChokeGroup; ++g)
            chokeMenu.addItem (200 + g, g == 0 ? juce::String ("None") : "Group " + juce::String (g), true, s.choke == g);

        menu.addSubMenu ("Choke group", chokeMenu);
        menu.addItem (4, "Mute", true, s.muted);
        menu.addItem (5, "Solo", true, s.soloed);
        menu.addItem (6, "Reverse", true, s.reversed);
        menu.addSeparator();

        for (int every : { 1, 2, 4, 8 })
            fillMenu.addItem (300 + every, every == 1 ? juce::String ("Every step") : "Every " + juce::String (every) + " steps");

        menu.addSubMenu ("Fill steps", fillMenu, hasClipNow);
        menu.addItem (7, "Clear this pad's steps", hasClipNow);

        juce::Component::SafePointer<DrumMachineView> safe (this);
        menu.showMenuAsync ({}, [safe, pad] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto* d = safe->getDrums();

            if (d == nullptr)
                return;

            auto& sess = safe->session;

            if (result >= 300)              safe->fillRow (pad, result - 300);
            else if (result >= 200)         safe->report (sess.setDrumPadChoke (*d, pad, result - 200), "Could not change the pad");
            else if (result >= 100)         safe->report (sess.copyDrumPad (*d, pad, result - 100), "Could not copy the pad");
            else if (result == 1)           safe->selectPad (pad, true);
            else if (result == 2)           safe->loadSampleWithChooser (pad);
            else if (result == 3)           safe->report (sess.resetDrumPad (*d, pad), "Could not reset the pad");
            else if (result == 4)           safe->toggleMute (pad);
            else if (result == 5)           safe->toggleSolo (pad);
            else if (result == 6)           safe->report (sess.setDrumPadReversed (*d, pad, ! d->getPad (pad).reversed), "Could not change the pad");
            else if (result == 7)           safe->clearRow (pad);

            safe->refreshAll();
        });
    }

    void DrumMachineView::showStepMenu (int pad, int step)
    {
        auto* clip = getClip();

        if (clip == nullptr)
            return;

        const auto velocity = steps->cellVelocity (pad, step);
        juce::PopupMenu menu, velocityMenu, fillMenu, copyMenu;

        menu.addSectionHeader (getDrums() != nullptr ? getDrums()->getPad (pad).name + ", step " + juce::String (step + 1) : juce::String ("Step"));
        menu.addItem (1, velocity > 0 ? "Turn off" : "Turn on");

        for (int i = 0; i < (int) std::size (velocityChoices); ++i)
            velocityMenu.addItem (100 + i, velocityNames[i], true, velocity == velocityChoices[i]);

        menu.addSubMenu ("Velocity", velocityMenu);

        juce::PopupMenu rollMenu;
        const auto hitsNow = steps->cellHits (pad, step);

        for (int n : { 1, 2, 3, 4, 6, 8 })
        {
            juce::PopupMenu::Item item (n == 1 ? juce::String ("No roll (one hit)") : juce::String (n) + " hits");
            item.itemID = 200 + n;
            item.isTicked = velocity > 0 && hitsNow == n;

            if (n == 2)
                item.shortcutKeyDescription = "Alt+click";

            rollMenu.addItem (item);
        }

        menu.addSubMenu ("Roll", rollMenu);
        menu.addSeparator();

        for (int every : { 1, 2, 4, 8 })
            fillMenu.addItem (300 + every, every == 1 ? juce::String ("Every step") : "Every " + juce::String (every) + " steps");

        menu.addSubMenu ("Fill this row", fillMenu);
        menu.addItem (2, "Shift row left", "Left");
        menu.addItem (3, "Shift row right", "Right");
        menu.addItem (4, "Clear this row", "Del");
        menu.addSeparator();
        menu.addItem (5, "Shift pattern left", "Shift+Left");
        menu.addItem (6, "Shift pattern right", "Shift+Right");
        menu.addItem (7, "Clear the whole pattern");

        for (int p = 0; p < numPages; ++p)
            if (p != pattern.page)
                copyMenu.addItem (400 + p, "Pattern " + juce::String (patternNames[p]), p < getNumPages());

        menu.addSubMenu ("Copy pattern to", copyMenu);
        menu.addItem (8, "Fill the clip with this pattern");

        juce::Component::SafePointer<DrumMachineView> safe (this);
        menu.showMenuAsync ({}, [safe, pad, step, velocity] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto* c = safe->getClip();

            if (c == nullptr)
                return;

            auto& sess = safe->session;
            const auto pitch = DrumMachinePlugin::noteForPad (pad);
            const auto stepStart = safe->getPageStart() + step * safe->getStepBeats();

            if (result >= 400)          safe->copyPatternTo (result - 400);
            else if (result >= 300)     safe->fillRow (pad, result - 300);
            else if (result >= 200)
                safe->report (sess.setDrumRoll (*c, pitch, stepStart, safe->getStepBeats(), result - 200, defaultVelocity), "Could not make the roll");
            else if (result >= 100)
            {
                const auto v = velocityChoices[result - 100];

                if (velocity > 0)
                    safe->report (sess.setMidiNoteVelocity (*c, sess.getNotesInRange (*c, pitch, stepStart, stepStart + safe->getStepBeats()), v), "Could not change the step");
                else
                    safe->report (sess.setDrumStep (*c, pitch, stepStart, safe->getStepBeats(), true, v, false, nullptr, safe->getStepStart (*c, step)),
                                  "Could not change the step");
            }
            else if (result == 1)
                safe->report (sess.setDrumStep (*c, pitch, stepStart, safe->getStepBeats(), velocity == 0, defaultVelocity, false, nullptr,
                                                safe->getStepStart (*c, step)), "Could not change the step");
            else if (result == 2)       safe->shiftRow (pad, -1);
            else if (result == 3)       safe->shiftRow (pad, 1);
            else if (result == 4)       safe->clearRow (pad);
            else if (result == 5)       safe->shiftRow (-1, -1);
            else if (result == 6)       safe->shiftRow (-1, 1);
            else if (result == 7)       safe->clearRow (-1);
            else if (result == 8)       safe->fillClip();

            safe->refreshAll();
        });
    }

    //==============================================================================
    bool DrumMachineView::hasEditFocus() const
    {
        return focus.hasFocus() && isShowing() && getDrums() != nullptr;
    }

    bool DrumMachineView::deleteSelection()
    {
        clearRow (selectedPad);
        return true;
    }

    void DrumMachineView::deselectAll()
    {
        steps->stopPainting();
    }

    void DrumMachineView::nudge (int direction)
    {
        shiftRow (selectedPad, direction);
    }

    void DrumMachineView::copyNotes()
    {
        if (auto* clip = getClip())
        {
            clipboard = session.getDrumHits (*clip, getPageStart(), getPageStart() + getPatternBeats());
            clipboardBeats = getPatternBeats();
        }
    }

    void DrumMachineView::cutNotes()
    {
        copyNotes();
        clearRow (-1);
    }

    void DrumMachineView::pasteNotes()
    {
        auto* clip = getClip();

        if (clip == nullptr || clipboardBeats <= 0.0)
            return;

        // Into this pattern; a longer copied pattern is cut to fit
        report (session.replaceDrumRange (*clip, getPageStart(), getPageStart() + getPatternBeats(), clipboard), "Could not paste the pattern");
        refreshAll();
    }

    void DrumMachineView::duplicateNotes()
    {
        if (pattern.page + 1 >= numPages)
        {
            report (false, "Could not duplicate the pattern", "Pattern D is the last one.");
            return;
        }

        copyPatternTo (pattern.page + 1);

        if (pattern.page + 1 < getNumPages())
            setPage (pattern.page + 1);
    }

    bool DrumMachineView::handleKey (const juce::KeyPress& key)
    {
        if (! hasEditFocus())
            return false;

        const auto code = key.getKeyCode();
        const auto mods = key.getModifiers();

        if (mods.isCommandDown() || mods.isAltDown())
            return false;

        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            selectPad (selectedPad + (code == juce::KeyPress::upKey ? -1 : 1), false);
            return true;
        }

        if (mods.isShiftDown() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
        {
            shiftRow (-1, code == juce::KeyPress::leftKey ? -1 : 1);
            return true;
        }

        if (code == juce::KeyPress::returnKey)          { selectPad (selectedPad, true); return true; }
        if (code == juce::KeyPress::pageUpKey)          { setPage (pattern.page - 1); return true; }
        if (code == juce::KeyPress::pageDownKey)        { setPage (juce::jmin (pattern.page + 1, getNumPages() - 1)); return true; }

        for (int p = 0; p < numPages; ++p)
            if (key == juce::KeyPress ('1' + p))
            {
                setPage (p);
                return true;
            }

        if (key == juce::KeyPress ('m'))    { toggleMute (selectedPad); return true; }
        if (key == juce::KeyPress ('s'))    { toggleSolo (selectedPad); return true; }

        return false;
    }

    //==============================================================================
    int DrumMachineView::padAt (juce::Point<int> p) const
    {
        for (int i = 0; i < pads.size(); ++i)
            if (pads[i]->isVisible() && pads[i]->getBounds().contains (p))
                return i;

        // A row name in the step grid counts as that pad
        if (steps->isVisible() && steps->getBounds().contains (p))
        {
            const auto local = p - steps->getPosition();

            if (local.x < labelWidth)
            {
                const auto row = steps->rowAtY (local.y);

                if (juce::isPositiveAndBelow (row, DrumMachinePlugin::numPads) && local.y >= headerHeight)
                    return row;
            }
        }

        return -1;
    }

    bool DrumMachineView::isInterestedInFileDrag (const juce::StringArray& files)
    {
        return getDrums() != nullptr && files.size() == 1;
    }

    void DrumMachineView::fileDragMove (const juce::StringArray&, int x, int y)
    {
        const auto pad = padAt ({ x, y });

        if (pad != dropPad)
        {
            dropPad = pad;

            for (auto* p : pads)
                p->repaint();
        }
    }

    void DrumMachineView::fileDragExit (const juce::StringArray&)
    {
        dropPad = -1;

        for (auto* p : pads)
            p->repaint();
    }

    bool DrumMachineView::isInterestedInDragSource (const SourceDetails& d)
    {
        return isInterestedInFileDrag (BrowserDrag::filesIn (d.description));
    }

    void DrumMachineView::itemDragMove (const SourceDetails& d)     { fileDragMove ({}, d.localPosition.x, d.localPosition.y); }
    void DrumMachineView::itemDragExit (const SourceDetails&)       { fileDragExit ({}); }

    void DrumMachineView::itemDropped (const SourceDetails& d)
    {
        filesDropped (BrowserDrag::filesIn (d.description), d.localPosition.x, d.localPosition.y);
    }

    void DrumMachineView::filesDropped (const juce::StringArray& files, int x, int y)
    {
        const auto pad = padAt ({ x, y });
        dropPad = -1;

        if (pad >= 0 && ! files.isEmpty())
            loadSample (pad, juce::File (files[0]));
        else
            refreshAll();
    }

    //==============================================================================
    void DrumMachineView::finishTyping()
    {
        if (editor != nullptr && editor->isTypingName())
        {
            editor->commitName();
            returnFocusToMainWindow();
        }
    }

    void DrumMachineView::returnFocusToMainWindow()
    {
        // Typing a name took the keyboard: hand it back so Space and the shortcuts work again
        for (auto* p = getParentComponent(); p != nullptr; p = p->getParentComponent())
        {
            if (p->getWantsKeyboardFocus())
            {
                p->grabKeyboardFocus();
                return;
            }
        }

        juce::Component::unfocusAllComponents();
    }

    void DrumMachineView::report (bool ok, const juce::String& title, const juce::String& message)
    {
        // Problems the view finds itself say so; Session's last error is only for Session's refusals
        if (! ok && onProblem)
            onProblem (title, message.isNotEmpty() ? message : session.getLastError());
    }

    bool DrumMachineView::isEditorShown() const
    {
        return editorOverride.has_value() ? *editorOverride : getWidth() >= editorMinViewWidth;
    }

    juce::int64 DrumMachineView::makeSignature (const Target& t) const
    {
        juce::int64 h = 17;
        const auto mix = [&h] (juce::int64 v) { h = h * 1000003 + v; };

        if (t.drums != nullptr)
        {
            mix (t.drums->getKitName().hashCode64());

            for (int pad = 0; pad < DrumMachinePlugin::numPads; ++pad)
            {
                const auto s = t.drums->getPad (pad);
                mix (s.name.hashCode64() ^ s.file.hashCode64());
                mix ((juce::int64) std::llround (s.tune * 100 + s.decay * 1e4 + s.start * 1e6 + s.pan * 1e8 + s.level * 1e10));
                mix (s.choke * 8 + (s.muted ? 1 : 0) + (s.soloed ? 2 : 0) + (s.reversed ? 4 : 0));
                mix (t.drums->getPadLoadError (pad).length() + (t.drums->getPadAudio (pad) != nullptr ? 1 : 0));
            }
        }

        if (t.clip != nullptr)
        {
            for (auto* n : t.clip->getSequence().getNotes())
                mix ((juce::int64) std::llround (n->getStartBeat().inBeats() * 9600.0) * 131 + n->getNoteNumber() * 128 + n->getVelocity());

            mix ((juce::int64) std::llround (t.clip->getPosition().getEnd().inSeconds() * 1000.0));
        }

        return h;
    }

    void DrumMachineView::timerCallback()
    {
        if (std::exchange (songWasClosed, false))
            retarget ({});

        const auto t = resolveTarget();
        const auto wantedDrums = t.drums != nullptr ? t.drums->itemID : te::EditItemID();
        const auto wantedClip = t.clip != nullptr ? t.clip->itemID : te::EditItemID();

        if (wantedDrums != drumsID || wantedClip != clipID)
            retarget (t);

        if (! isShowing() || t.drums == nullptr)
            return;

        if (const auto signature = makeSignature (t); signature != lastSignature)
        {
            lastSignature = signature;
            refreshAll();
        }

        // Pads flash when they are played (by the song, your pads or a click)
        for (int i = 0; i < pads.size(); ++i)
        {
            auto* pad = pads[i];
            const auto hitsNow = t.drums->getPadHitCount (i);

            if (hitsNow != pad->lastHits)
            {
                pad->lastHits = hitsNow;
                pad->flash = 1.0f;
            }

            if (pad->flash > 0.0f)
            {
                pad->flash = juce::jmax (0.0f, pad->flash - 0.12f);
                pad->repaint();
            }
        }

        // The playing step
        int playing = -1;

        if (t.clip != nullptr && session.isPlaying())
        {
            const auto beat = t.clip->getContentBeatAtTime (session.getPosition()).inBeats() - getPageStart();

            if (beat >= 0.0 && beat < getPatternBeats())
                playing = (int) std::floor (beat / getStepBeats());
        }

        if (playing != steps->playingStep)
        {
            steps->playingStep = playing;

            // Follow the playing step when the steps scroll: jump a view's width when it runs off the edge
            if (playing >= 0 && steps->getMaxScrollX() > 0)
            {
                const auto area = steps->cellsArea();
                const auto x1 = steps->stepX (playing), x2 = steps->stepX (playing + 1);

                if (x1 < (float) area.getX() || x2 > (float) area.getRight())
                    setStepScroll (steps->scrollX + juce::roundToInt (x1) - area.getX());
            }

            steps->repaint();
        }
    }

    void DrumMachineView::refreshAll()
    {
        updateStepScroll();   // the step count may have changed
        steps->rebuildCells (getClip());
        refreshToolbar();
        editor->refresh (false);

        for (auto* p : pads)
            p->repaint();

        steps->repaint();
        velocityLane->repaint();
        repaint (messageArea);
    }

    void DrumMachineView::refreshToolbar()
    {
        auto* drums = getDrums();
        const bool hasClipNow = getClip() != nullptr;

        // Kits of the chosen genre (the current kit is shown even if it is in another genre)
        const auto genre = genreBox.getSelectedId() > 1 ? genreBox.getText() : juce::String();
        kitBox.clear (juce::dontSendNotification);

        for (int k = 0; k < DrumKits::getNumKits(); ++k)
            if (genre.isEmpty() || genre == DrumKits::getKit (k).genre)
                kitBox.addItem (DrumKits::getKit (k).name, k + 1);

        if (drums != nullptr)
        {
            const auto kit = DrumKits::findKit (drums->getKitName());

            if (kit >= 0 && kitBox.indexOfItemId (kit + 1) >= 0)
                kitBox.setSelectedId (kit + 1, juce::dontSendNotification);
            else
                kitBox.setText (drums->getKitName().isNotEmpty() ? drums->getKitName() : juce::String ("Custom"), juce::dontSendNotification);
        }

        const auto pages = getNumPages();

        for (int i = 0; i < patternButtons.size(); ++i)
        {
            patternButtons[i]->setToggleState (i == pattern.page, juce::dontSendNotification);
            patternButtons[i]->setEnabled (i < pages || i == pattern.page);
        }

        stepsBox.setSelectedItemIndex (pattern.stepsIndex, juce::dontSendNotification);
        lengthBox.setSelectedItemIndex (pattern.lengthIndex, juce::dontSendNotification);
        swingBox.setSelectedItemIndex (pattern.swingIndex, juce::dontSendNotification);
        fillClipButton.setEnabled (hasClipNow);
        editorButton.setToggleState (editorViewport.isVisible(), juce::dontSendNotification);
    }

    //==============================================================================
    void DrumMachineView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::background);
        auto* drums = getDrums();

        if (drums == nullptr)
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.drawFittedText ("No Drum Machine on this track.\nDouble-click a kit in the browser's Drum kits to add one.",
                              getLocalBounds().reduced (20), juce::Justification::centred, 3);
            return;
        }

        g.setColour (Theme::panel);
        g.fillRect (toolbarArea);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (toolbarArea.getBottom() - 1, 0.0f, (float) getWidth());

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::captionFontSize));

        for (auto& label : toolbarLabels)
            g.drawText (label.text, label.bounds, juce::Justification::centredRight, false);

        if (! hintArea.isEmpty())
        {
            g.setFont (Theme::font (Theme::smallFontSize));
            g.drawFittedText ("Click a pad to hear it. Drop a sound on a pad to replace it. Alt+click a step for a roll.", hintArea, juce::Justification::topLeft, 2);
        }

        if (editorViewport.isVisible())
        {
            g.setColour (Theme::laneSeparator);
            g.drawVerticalLine (editorViewport.getX() - 1, (float) editorViewport.getY(), (float) editorViewport.getBottom());
            g.drawVerticalLine (editorViewport.getRight(), (float) editorViewport.getY(), (float) editorViewport.getBottom());
        }

        if (! messageArea.isEmpty())
        {
            g.setColour (Theme::panel);
            g.fillRoundedRectangle (messageArea.toFloat(), 6.0f);
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::baseFontSize - 0.5f));
            g.drawFittedText ("No pattern clip selected.\nSelect a clip on this track, or make one with New pattern clip.",
                              messageArea.reduced (12), juce::Justification::centred, 3);
        }
    }

    void DrumMachineView::resized()
    {
        auto* drums = getDrums();
        const bool show = drums != nullptr;
        const bool hasClipNow = getClip() != nullptr;
        const bool showEditor = show && isEditorShown();
        toolbarLabels.clear();
        hintArea = messageArea = {};

        for (auto* p : pads)
            p->setVisible (show);

        editorViewport.setVisible (showEditor);
        steps->setVisible (show);
        velocityLane->setVisible (show && hasClipNow);
        rowScroll.setVisible (show);

        juce::Component* const toolbarItems[] = { &genreBox, &kitBox, &stepsBox, &lengthBox, &swingBox, &fillClipButton, &newClipButton, &editorButton };

        for (auto* c : toolbarItems)
            c->setVisible (false);

        for (auto* b : patternButtons)
            b->setVisible (false);

        if (! show)
            return;

        auto r = getLocalBounds();
        toolbarArea = r.removeFromTop (toolbarHeight);

        // Toolbar: left to right; the least important drop out first when narrow
        {
            const auto h = Theme::buttonHeight - 2;
            auto bar = toolbarArea.reduced (10, 0);
            const auto place = [&] (juce::Component& c, int w)
            {
                c.setBounds (bar.removeFromLeft (w).withSizeKeepingCentre (w, h));
                c.setVisible (true);
            };
            const auto label = [&] (const char* text)
            {
                const auto w = juce::GlyphArrangement::getStringWidthInt (Theme::font (Theme::captionFontSize), text) + 6;
                toolbarLabels.add ({ text, bar.removeFromLeft (w).withTrimmedRight (2) });
            };

            // Right side
            editorButton.setBounds (bar.removeFromRight (editorButton.getIconOnlyWidth (h)).withSizeKeepingCentre (editorButton.getIconOnlyWidth (h), h));
            editorButton.setVisible (true);
            bar.removeFromRight (6);
            auto& action = hasClipNow ? fillClipButton : newClipButton;
            const auto actionWidth = action.getIdealWidth (h);
            action.setBounds (bar.removeFromRight (actionWidth).withSizeKeepingCentre (actionWidth, h));
            action.setVisible (true);
            bar.removeFromRight (12);

            const int kitW = 150, genreW = 104, patternW = numPages * 26 + 6;
            const int stepsW = 50 + 36, lengthW = 70 + 44, swingW = 62 + 40;
            int needed = kitW + 12 + (hasClipNow ? patternW + 12 : 0);
            const bool showGenre   = bar.getWidth() >= (needed += genreW + 6);
            const bool showSteps   = hasClipNow && bar.getWidth() >= (needed += stepsW + 8);
            const bool showLength  = hasClipNow && bar.getWidth() >= (needed += lengthW + 8);
            const bool showSwing   = hasClipNow && bar.getWidth() >= (needed += swingW + 8);

            place (kitBox, kitW);

            if (showGenre)
            {
                bar.removeFromLeft (6);
                place (genreBox, genreW);
            }

            if (hasClipNow)
            {
                bar.removeFromLeft (12);

                for (auto* b : patternButtons)
                {
                    place (*b, 24);
                    bar.removeFromLeft (2);
                }

                bar.removeFromLeft (4);

                if (showSteps)   { bar.removeFromLeft (8); label ("Steps");  place (stepsBox, 50); }
                if (showLength)  { bar.removeFromLeft (8); label ("Length"); place (lengthBox, 70); }
                if (showSwing)   { bar.removeFromLeft (8); label ("Swing");  place (swingBox, 62); }
            }
        }

        auto body = r.reduced (8, 6);

        // Pads: square-ish, as big as the height allows
        const auto gapY = body.getHeight() < 140 ? 3 : padGap;
        const auto padH = juce::jlimit (16, 56, (body.getHeight() - 3 * gapY) / 4);
        const auto padW = juce::jlimit (40, 70, juce::roundToInt (padH * 1.3));
        auto padColumn = body.removeFromLeft (4 * padW + 3 * padGap);

        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
            {
                const auto pad = (3 - row) * 4 + col;   // pad 1 bottom-left
                pads[pad]->setBounds (padColumn.getX() + col * (padW + padGap), padColumn.getY() + row * (padH + gapY), padW, padH);
            }

        const auto hintTop = padColumn.getY() + 4 * padH + 3 * gapY + 6;

        if (padColumn.getBottom() - hintTop >= 28)
            hintArea = { padColumn.getX(), hintTop, padColumn.getWidth(), padColumn.getBottom() - hintTop };

        body.removeFromLeft (12);

        // The editor never squeezes the steps below a usable size, even when asked for
        constexpr int minStepsWidth = labelWidth + msWidth + 16 * 12 + Theme::scrollBarSize;
        const bool editorFits = body.getWidth() - editorWidth - 12 >= minStepsWidth;
        editorViewport.setVisible (showEditor && editorFits);
        editorButton.setToggleState (showEditor && editorFits, juce::dontSendNotification);

        if (showEditor && editorFits)
        {
            editorViewport.setBounds (body.removeFromLeft (editorWidth));
            editor->setSize (editorViewport.getMaximumVisibleWidth(), PadEditor::contentHeight);
            body.removeFromLeft (12);
        }

        // Steps (with the velocity lane under them when there is a clip)
        sequencerArea = body;
        auto seq = body;

        if (hasClipNow)
            velocityLane->setBounds (seq.removeFromBottom (velocityHeight).withTrimmedRight (Theme::scrollBarSize));
        else
            messageArea = seq.removeFromBottom (juce::jmin (64, seq.getHeight() / 3)).withTrimmedRight (Theme::scrollBarSize);

        // The bar along the steps sits right under them, as wide as the steps (it hides itself when they fit)
        auto along = seq.removeFromBottom (Theme::scrollBarSize);
        rowScroll.setBounds (seq.removeFromRight (Theme::scrollBarSize).withTrimmedTop (headerHeight));
        steps->setBounds (seq);
        stepScroll.setBounds (along.withX (seq.getX() + labelWidth).withWidth (juce::jmax (0, seq.getWidth() - labelWidth - msWidth)));

        const auto visible = steps->getVisibleRowsHeight();
        steps->scrollY = juce::jlimit (0, juce::jmax (0, steps->getContentHeight() - visible), steps->scrollY);
        rowScroll.setRangeLimits (0.0, (double) steps->getContentHeight(), juce::dontSendNotification);
        rowScroll.setCurrentRange ((double) steps->scrollY, (double) visible, juce::dontSendNotification);

        refreshAll();
    }
}
