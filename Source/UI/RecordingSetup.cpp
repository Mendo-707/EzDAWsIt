/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Setup > Recording (see RecordingSetup.h).
*/

#include "RecordingSetup.h"
#include "../Engine/AudioEngine.h"

namespace ez
{
    namespace
    {
        constexpr int meterRowHeight = 30;
        constexpr float clipDb = -0.5f, loudDb = -6.0f, quietDb = -30.0f;

        void styleHeading (juce::Label& l, const juce::String& text)
        {
            l.setText (text.toUpperCase(), juce::dontSendNotification);
            l.setFont (Theme::font (Theme::captionFontSize, true));
            l.setColour (juce::Label::textColourId, Theme::textDim);
        }

        void styleHint (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setFont (Theme::font (Theme::smallFontSize));
            l.setColour (juce::Label::textColourId, Theme::textDim);
            l.setJustificationType (juce::Justification::topLeft);
            l.setMinimumHorizontalScale (1.0f);
        }
    }

    juce::String RecordingSetup::describeLevel (float peakDb, bool clippedRecently)
    {
        if (clippedRecently)        return "Clipping: turn the gain down on your interface";
        if (peakDb > loudDb)        return "Loud: a little less gain is safer";
        if (peakDb > quietDb)       return "Good";
        if (peakDb > -60.0f)        return "Too quiet: turn the gain up";
        return "Nothing coming in";
    }

    //==============================================================================
    /** One input: its name, a meter with the peak held, and what to do about the level. */
    struct RecordingSetup::InputMeter : public juce::Component
    {
        InputMeter (te::WaveInputDevice& d) : device (&d), name (d.getAliasOrName())
        {
            device->levelMeasurer.addClient (client);
        }

        /** The device may already be gone (the device list changed): only detach from one that still exists. */
        void detach (const std::vector<te::WaveInputDevice*>& stillThere)
        {
            if (device != nullptr && std::find (stillThere.begin(), stillThere.end(), device) != stillThere.end())
                device->levelMeasurer.removeClient (client);

            device = nullptr;
        }

        void update()
        {
            if (device == nullptr)
                return;

            float now = -100.0f;

            for (int c = 0; c < juce::jmax (1, client.getNumChannelsUsed()); ++c)
                now = juce::jmax (now, client.getAndClearAudioLevel (c).dB);

            const auto ms = juce::Time::getMillisecondCounter();
            level = juce::jmax (now, level - 1.5f);   // falls 45 dB a second (30 Hz)

            if (now >= held || ms - heldAt > 1500)
            {
                held = now;
                heldAt = ms;
            }

            if (now > clipDb)
                clippedAt = ms;

            // The loudest of the last few seconds decides the advice (a quiet moment isn't "too quiet")
            if (now > recentPeak || ms - recentAt > 3000)
            {
                recentPeak = now;
                recentAt = ms;
            }

            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().reduced (0, 3);
            const auto ms = juce::Time::getMillisecondCounter();
            const bool clipped = clippedAt != 0 && ms - clippedAt < 3000;

            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.drawFittedText (name, r.removeFromLeft (200), juce::Justification::centredLeft, 1);

            auto advice = r.removeFromRight (260);
            const auto text = describeLevel (recentPeak, clipped);
            g.setColour (clipped ? Theme::danger : text == "Good" ? Theme::displayText : Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize, clipped));
            g.drawFittedText (text, advice.reduced (8, 0), juce::Justification::centredLeft, 1);

            // The meter: -60 to 0 dB, green, then amber above -6, red at the top
            auto bar = r.reduced (6, 5).toFloat();
            g.setColour (Theme::display);
            g.fillRoundedRectangle (bar, 3.0f);

            const auto xOf = [&bar] (float db) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
            const auto fill = bar.withRight (xOf (level));
            juce::ColourGradient gradient (juce::Colour (0xff2f9a5c), bar.getX(), 0, Theme::record, bar.getRight(), 0, false);
            gradient.addColour ((double) ((loudDb + 60.0f) / 60.0f), juce::Colour (0xffd8c03a));
            g.setGradientFill (gradient);
            g.fillRoundedRectangle (fill, 3.0f);

            g.setColour (Theme::displayLabel.withAlpha (0.5f));

            for (float db : { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f })
                g.drawVerticalLine ((int) xOf (db), bar.getY(), bar.getBottom());

            if (held > -60.0f)
            {
                g.setColour (held > clipDb ? Theme::record : Theme::text);
                g.fillRect (juce::Rectangle<float> (xOf (held) - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
            }

            if (clipped)
            {
                g.setColour (Theme::record);
                g.fillRoundedRectangle (bar.withLeft (bar.getRight() - 6.0f), 2.0f);
            }
        }

        te::WaveInputDevice* device = nullptr;
        juce::String name;
        te::LevelMeasurer::Client client;
        float level = -100.0f, held = -100.0f, recentPeak = -100.0f;
        juce::uint32 heldAt = 0, clippedAt = 0, recentAt = 0;
    };

    //==============================================================================
    RecordingSetup::RecordingSetup (AudioEngine& e, Session& s)
        : audioEngine (e), session (s)
    {
        styleHeading (inputsHeading, "Inputs");
        styleHeading (levelsHeading, "Input level check");
        styleHeading (delayHeading, "Recording delay fix");
        styleHeading (punchHeading, "Punch and takes");
        styleHint (levelsHint, "Play or sing as loud as you will in the song. Aim for \"Good\": peaks between -30 and -6 dB. "
                               "Set the level with the gain knob on your interface.");
        styleHint (delayHint, "If recordings land late (out of time with what you played along to), move them earlier here. "
                              "Most interfaces need 0; some USB ones need 5 to 20 ms.");
        styleHint (takesHint, "Recording over a loop keeps every pass as a take of one clip. T plays the next take, Shift+T the one before, "
                              "or right-click the clip > Takes. To comp, split the clip (Ctrl+E) and pick a take for each part.");

        for (auto* l : { &inputsHeading, &levelsHeading, &delayHeading, &punchHeading, &levelsHint, &delayHint, &takesHint })
            addAndMakeVisible (l);

        monoButton.setRadioGroupId (41);
        stereoButton.setRadioGroupId (41);
        monoButton.setTooltip ("Each channel of your interface is its own input: input 1 a kick mic, input 2 a snare mic, input 3 a guitar...");
        stereoButton.setTooltip ("Channels 1+2, 3+4 ... are stereo inputs (keyboards, drum machines, a mixer's outputs)");

        const auto pick = [this] (bool mono)
        {
            if (audioEngine.areInputsMono() == mono)
                return;

            if (session.isRecording())
            {
                session.showHint ("Stop recording before changing the inputs");
                refreshControls();
                return;
            }

            dropMeters();   // their devices are about to be replaced (the meters come back with the new ones)
            pendingMono = mono ? 1 : 0;
            audioEngine.setInputsMono (mono);
            session.showHint (mono ? "Inputs: one per channel. Pick each track's input in Routing."
                                   : "Inputs: stereo pairs. Pick each track's input in Routing.");
            refreshControls();
        };

        monoButton.onClick = [this, pick] { if (monoButton.getToggleState()) pick (true); };
        stereoButton.onClick = [this, pick] { if (stereoButton.getToggleState()) pick (false); };
        addAndMakeVisible (monoButton);
        addAndMakeVisible (stereoButton);

        delaySlider.setSliderStyle (juce::Slider::LinearHorizontal);
        delaySlider.setRange (-50.0, 50.0, 0.5);
        delaySlider.setTextValueSuffix (" ms earlier");
        delaySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 120, 22);
        delaySlider.setDoubleClickReturnValue (true, 0.0);
        delaySlider.setTooltip ("Moves new recordings earlier by this much (double-click: 0)");
        delaySlider.onValueChange = [this] { audioEngine.setRecordOffsetMs (delaySlider.getValue()); };
        addAndMakeVisible (delaySlider);

        punchButton.setTooltip ("Only what plays inside the loop region is recorded; the rest plays as it was (P)");
        punchButton.onClick = [this]
        {
            if (! session.setPunch (punchButton.getToggleState()))
                session.showHint (session.getLastError());

            refreshControls();
        };
        addAndMakeVisible (punchButton);

        meterViewport.setViewedComponent (&meterContent, false);
        meterViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (meterViewport);

        audioEngine.getEngine().getDeviceManager().addChangeListener (this);
        session.addListener (this);
        rebuildMeters();
        refreshControls();
        startTimerHz (30);
    }

    RecordingSetup::~RecordingSetup()
    {
        stopTimer();
        session.removeListener (this);
        audioEngine.getEngine().getDeviceManager().removeChangeListener (this);
        dropMeters();
    }

    void RecordingSetup::dropMeters()
    {
        const auto stillThere = audioEngine.getEngine().getDeviceManager().getWaveInputDevices();

        for (auto* m : meters)
            m->detach (stillThere);

        meters.clear();
        lastSignature = {};
    }

    void RecordingSetup::rebuildMeters()
    {
        const auto inputs = audioEngine.getAudioInputs();
        juce::String signature;

        for (auto* input : inputs)
            signature << input->getDeviceID() << "|";

        if (signature == lastSignature && meters.size() == inputs.size())
            return;

        dropMeters();
        lastSignature = signature;

        for (auto* input : inputs)
            meterContent.addAndMakeVisible (meters.add (new InputMeter (*input)));

        resized();
    }

    void RecordingSetup::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // The old devices may be gone already: detach only from ones still listed, then start again
        pendingMono = -1;
        dropMeters();
        rebuildMeters();
        refreshControls();
    }

    void RecordingSetup::sessionStateChanged()   { refreshControls(); }
    void RecordingSetup::sessionSongChanged()    { refreshControls(); }

    void RecordingSetup::refreshControls()
    {
        const bool mono = pendingMono >= 0 ? pendingMono == 1 : audioEngine.areInputsMono();
        monoButton.setToggleState (mono, juce::dontSendNotification);
        stereoButton.setToggleState (! mono, juce::dontSendNotification);
        monoButton.setEnabled (! session.isRecording());
        stereoButton.setEnabled (! session.isRecording());

        if (! delaySlider.isMouseButtonDown())
            delaySlider.setValue (audioEngine.getRecordOffsetMs(), juce::dontSendNotification);

        punchButton.setToggleState (session.isPunchOn(), juce::dontSendNotification);
        punchButton.setEnabled (session.hasSong() && ! session.isRecording());
    }

    void RecordingSetup::timerCallback()
    {
        for (auto* m : meters)
            m->update();
    }

    void RecordingSetup::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::panel);

        if (meters.isEmpty())
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.drawFittedText ("No audio inputs: pick an interface with inputs in Audio device.",
                              meterViewport.getBounds().reduced (4), juce::Justification::topLeft, 2);
        }
    }

    void RecordingSetup::resized()
    {
        auto r = getLocalBounds().reduced (4);
        const auto line = [&r] (int h) { auto row = r.removeFromTop (h); r.removeFromTop (4); return row; };

        inputsHeading.setBounds (line (18));
        monoButton.setBounds (line (24));
        stereoButton.setBounds (line (24));
        r.removeFromTop (8);

        // The bottom sections have fixed heights; the meters take what's left
        auto bottom = r.removeFromBottom (190);
        levelsHeading.setBounds (line (18));
        levelsHint.setBounds (line (32));
        meterViewport.setBounds (r);
        const auto contentWidth = meterViewport.getWidth() - (meterViewport.canScrollVertically() ? meterViewport.getScrollBarThickness() : 0);
        meterContent.setSize (juce::jmax (0, contentWidth), meters.size() * meterRowHeight);

        for (int i = 0; i < meters.size(); ++i)
            meters[i]->setBounds (0, i * meterRowHeight, meterContent.getWidth(), meterRowHeight);

        r = bottom;
        r.removeFromTop (8);
        delayHeading.setBounds (line (18));
        delayHint.setBounds (line (32));
        delaySlider.setBounds (line (24).withWidth (juce::jmin (r.getWidth(), 460)));
        r.removeFromTop (6);
        punchHeading.setBounds (line (18));
        punchButton.setBounds (line (24));
        takesHint.setBounds (r);
    }
}
