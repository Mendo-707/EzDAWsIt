/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The Tuner window (see TunerView.h).
*/

#include "TunerView.h"
#include "Theme.h"

namespace ez
{
    namespace
    {
        constexpr float inTune = 3.0f;   // cents: green
    }

    TunerView::TunerView (Session& s, std::function<int()> highlighted)
        : session (s), getHighlightedTrack (std::move (highlighted))
    {
        trackLabel.setFont (Theme::font (Theme::titleFontSize, true));
        trackLabel.setColour (juce::Label::textColourId, Theme::text);
        trackLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (trackLabel);

        armButton.setTooltip ("Arm the track so the tuner hears your guitar or bass (Shift+R)");
        armButton.onClick = [this]
        {
            if (! session.setTrackArmed (track, true))
                problem = session.getLastError();

            followTrack();
        };
        addAndMakeVisible (armButton);

        addButton.setTooltip ("Put a Tuner first on this track again");
        addButton.onClick = [this]
        {
            addedFor = -1;
            problem = {};
            followTrack();
        };
        addChildComponent (addButton);

        muteButton.setClickingTogglesState (true);
        muteButton.setTooltip ("Silence the track while this window is open, so the audience doesn't hear you tune");
        muteButton.onClick = [this]
        {
            if (tuner != nullptr)
                tuner->setMuted (muteButton.getToggleState());
        };
        addAndMakeVisible (muteButton);

        lowerButton.setTooltip ("Tune to a lower A (most bands use A = 440 Hz)");
        higherButton.setTooltip ("Tune to a higher A (most bands use A = 440 Hz)");
        lowerButton.onClick  = [this] { if (tuner != nullptr) setReference (tuner->getStored (TunerPlugin::reference) - 1.0f); };
        higherButton.onClick = [this] { if (tuner != nullptr) setReference (tuner->getStored (TunerPlugin::reference) + 1.0f); };
        addAndMakeVisible (lowerButton);
        addAndMakeVisible (higherButton);

        referenceLabel.setFont (Theme::font (Theme::baseFontSize));
        referenceLabel.setColour (juce::Label::textColourId, Theme::textDim);
        referenceLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (referenceLabel);

        setSize (preferredWidth, preferredHeight);
        session.addListener (this);
        followTrack();
        startTimerHz (30);
    }

    TunerView::~TunerView()
    {
        session.removeListener (this);

        if (tuner != nullptr)
            tuner->setMuted (false);   // never leave a track silent
    }

    void TunerView::sessionSongClosing()
    {
        if (tuner != nullptr)
            tuner->setMuted (false);

        tuner = nullptr;
        track = -2;   // look again once the next song is open
    }

    void TunerView::setReference (float hz)
    {
        if (tuner != nullptr)
            tuner->set (TunerPlugin::reference, juce::jlimit (430.0f, 450.0f, hz));

        updateControls();
    }

    void TunerView::followTrack()
    {
        const auto wanted = getHighlightedTrack != nullptr ? getHighlightedTrack() : -1;

        if (wanted != track)
        {
            if (tuner != nullptr)
                tuner->setMuted (false);

            tuner = nullptr;
            track = wanted;
            problem = {};
        }

        const bool valid = session.hasSong() && juce::isPositiveAndBelow (track, session.getNumAudioTracks())
                            && session.getTrackKind (track) == Session::TrackKind::normal;

        if (! valid)
        {
            tuner = nullptr;
        }
        else if (session.isTrackArmed (track))
        {
            // The window's tuner is the one on the track now (an undo can take it away)
            auto* current = session.getTuner (track);

            // Added once per track; if an undo takes it away, it stays away until asked for again
            if (current == nullptr && problem.isEmpty() && addedFor != track)
            {
                addedFor = track;
                current = session.ensureTuner (track);

                if (current == nullptr)
                    problem = session.getLastError();
            }

            if (current != tuner.get())
            {
                if (tuner != nullptr)
                    tuner->setMuted (false);

                tuner = current;

                if (tuner != nullptr)
                    tuner->setMuted (muteButton.getToggleState());
            }
        }
        else if (tuner != nullptr)
        {
            tuner->setMuted (false);
            tuner = nullptr;
        }

        updateControls();
    }

    void TunerView::updateControls()
    {
        const bool valid = session.hasSong() && juce::isPositiveAndBelow (track, session.getNumAudioTracks());
        trackLabel.setText (valid ? "Tuning: " + session.getTrackName (track) : juce::String ("Click a track to tune"),
                            juce::dontSendNotification);

        armButton.setVisible (valid && session.getTrackKind (track) == Session::TrackKind::normal && ! session.isTrackArmed (track));
        addButton.setVisible (valid && ! armButton.isVisible() && tuner == nullptr && session.getTrackKind (track) == Session::TrackKind::normal);
        const bool hasTuner = tuner != nullptr;
        lowerButton.setEnabled (hasTuner);
        higherButton.setEnabled (hasTuner);
        referenceLabel.setText ("A = " + juce::String (hasTuner ? juce::roundToInt (tuner->getStored (TunerPlugin::reference)) : 440) + " Hz",
                                juce::dontSendNotification);
    }

    void TunerView::timerCallback()
    {
        if (! session.hasSong())
        {
            tuner = nullptr;
            return;
        }

        followTrack();

        const auto now = juce::Time::getMillisecondCounter();
        reading = tuner != nullptr ? tuner->read() : TunerPlugin::Reading();

        if (reading.hasNote)
        {
            // Steady needle: follows quickly when far off, slowly when close
            const auto rate = std::abs (reading.cents - shownCents) > 15.0f ? 0.6f : 0.25f;
            shownCents += (reading.cents - shownCents) * rate;
            lastNoteTime = now;
        }

        repaint (displayArea);
    }

    void TunerView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::panel);

        auto area = displayArea.toFloat();
        g.setColour (Theme::display);
        g.fillRoundedRectangle (area, (float) Theme::cornerRadius * 2);

        const bool showing = juce::Time::getMillisecondCounter() - lastNoteTime < 600 && lastNoteTime != 0;
        const bool good = showing && std::abs (shownCents) <= inTune;
        const auto colour = ! showing ? Theme::textDim : good ? Theme::displayText : Theme::warning;

        // The needle's arc: -50 to +50 cents across 120 degrees
        const auto centre = juce::Point<float> (area.getCentreX(), area.getBottom() - 26.0f);
        const auto radius = juce::jmin (area.getWidth() * 0.42f, area.getHeight() - 40.0f);
        const auto angleOf = [] (float cents) { return juce::degreesToRadians (juce::jlimit (-50.0f, 50.0f, cents) * 1.2f); };

        for (int c = -50; c <= 50; c += 10)
        {
            const auto a = angleOf ((float) c);
            const auto inner = c == 0 ? 0.78f : 0.86f;
            g.setColour (c == 0 ? Theme::displayText : Theme::displayLabel.withAlpha (0.7f));
            g.drawLine ({ centre.getPointOnCircumference (radius * inner, a), centre.getPointOnCircumference (radius, a) }, c == 0 ? 2.5f : 1.2f);
        }

        g.setColour (Theme::displayLabel);
        g.setFont (Theme::font (Theme::captionFontSize, true));
        const auto leftEnd = centre.getPointOnCircumference (radius + 4, angleOf (-50));
        const auto rightEnd = centre.getPointOnCircumference (radius + 4, angleOf (50));
        g.drawText ("FLAT", juce::Rectangle<float> (leftEnd.x - 44, leftEnd.y + 4, 40, 14), juce::Justification::centredRight);
        g.drawText ("SHARP", juce::Rectangle<float> (rightEnd.x + 4, rightEnd.y + 4, 44, 14), juce::Justification::centredLeft);

        if (showing)
        {
            const auto a = angleOf (shownCents);
            g.setColour (colour);
            g.drawLine ({ centre, centre.getPointOnCircumference (radius * 0.95f, a) }, 3.0f);
        }

        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre (centre));

        // The note, big, with what to do underneath
        auto text = area.reduced (12).withTrimmedBottom (radius * 0.45f);
        g.setFont (Theme::font (56.0f, true));
        g.drawText (showing ? reading.noteName : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")),
                    text.removeFromTop (64), juce::Justification::centred);

        g.setFont (Theme::font (Theme::baseFontSize));
        g.setColour (Theme::displayLabel);

        juce::String hint;

        if (! problem.isEmpty())
            hint = problem;
        else if (tuner == nullptr)
            hint = armButton.isVisible() ? "Arm the track so the tuner can hear it"
                 : addButton.isVisible() ? "The tuner was taken off this track: Add the tuner puts it back"
                 : "Tune a track you record into";
        else if (! showing)
            hint = "Play one string at a time";
        else if (good)
            hint = "In tune  (" + juce::String (reading.hz, 1) + " Hz)";
        else
            hint = juce::String (std::abs (juce::roundToInt (shownCents))) + " cents " + (shownCents < 0 ? "flat: tune up" : "sharp: tune down")
                   + "  (" + juce::String (reading.hz, 1) + " Hz)";

        g.drawFittedText (hint, text.removeFromTop (22).toNearestInt(), juce::Justification::centred, 1);
    }

    void TunerView::resized()
    {
        auto r = getLocalBounds().reduced (Theme::gap * 2);
        auto top = r.removeFromTop (Theme::buttonHeight);
        armButton.setBounds (top.removeFromRight (110));
        addButton.setBounds (armButton.getBounds().withLeft (armButton.getRight() - 120));
        trackLabel.setBounds (top);
        r.removeFromTop (Theme::gap);

        auto bottom = r.removeFromBottom (Theme::buttonHeight);
        muteButton.setBounds (bottom.removeFromLeft (150));
        higherButton.setBounds (bottom.removeFromRight (Theme::buttonHeight));
        referenceLabel.setBounds (bottom.removeFromRight (90));
        lowerButton.setBounds (bottom.removeFromRight (Theme::buttonHeight));
        r.removeFromBottom (Theme::gap);

        displayArea = r;
    }

    //==============================================================================
    namespace
    {
        class TunerWindow : public juce::DocumentWindow
        {
        public:
            TunerWindow (Session& session, std::function<int()> getTrack, std::function<void()> closed)
                : juce::DocumentWindow ("Tuner", Theme::panel, juce::DocumentWindow::closeButton), onClosed (std::move (closed))
            {
                setUsingNativeTitleBar (true);
                setContentOwned (new TunerView (session, std::move (getTrack)), true);
                setResizable (true, false);
                setResizeLimits (TunerView::preferredWidth, TunerView::preferredHeight, 900, 700);
                setAlwaysOnTop (true);
                centreWithSize (getWidth(), getHeight());
            }

            void closeButtonPressed() override
            {
                if (onClosed != nullptr)
                    juce::MessageManager::callAsync ([cb = onClosed] { cb(); });   // the owner deletes us, after this returns
            }

        private:
            std::function<void()> onClosed;
        };
    }

    std::unique_ptr<juce::DocumentWindow> createTunerWindow (Session& session, std::function<int()> getTrack, std::function<void()> onClose)
    {
        return std::make_unique<TunerWindow> (session, std::move (getTrack), std::move (onClose));
    }
}
