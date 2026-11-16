/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "LevelMeter.h"
#include "Theme.h"

namespace ez
{
    namespace
    {
        const juce::Colour green = Theme::meterLow, amber = Theme::meterMid, red = Theme::meterHigh;
        constexpr float topDb = 6.0f;

        /** 0..1 along the bar: 0 dB sits at 90 %, -60 dB at the bottom. */
        float position (float db)
        {
            return juce::jlimit (0.0f, 1.0f, (db - MeterBank::floorDb) / (topDb - MeterBank::floorDb));
        }

        juce::String dbText (float db)
        {
            return db <= MeterBank::floorDb + 0.01f ? juce::String ("-inf") : juce::String (db, 1) + " dB";
        }

        juce::String lufsText (float lufs, bool valid)
        {
            return valid && lufs > te::LoudnessMeter::silenceFloorDb + 1.0f ? juce::String (lufs, 1) : juce::String ("-");
        }
    }

    LevelMeter::Loudness LevelMeter::loudnessShown = LevelMeter::Loudness::shortTerm;

    LevelMeter::LevelMeter (Session& s, int index, bool h) : session (s), trackIndex (index), horizontal (h)
    {
        setRepaintsOnMouseActivity (false);
        setOpaque (true);   // repaints 30 times a second: never make the parent repaint too
        setTooltip (describe());
        startTimerHz (MeterBank::pollHz);
    }

    MeterBank::MainReading LevelMeter::read() const
    {
        if (isMain())
            return session.getMeters().getMain();

        MeterBank::MainReading r;
        static_cast<MeterBank::Reading&> (r) = session.getMeters().getTrack (trackIndex);
        return r;
    }

    void LevelMeter::timerCallback()
    {
        const auto now = read();
        const auto changed = std::abs (now.levelDb[0] - shown.levelDb[0]) > 0.05f || std::abs (now.levelDb[1] - shown.levelDb[1]) > 0.05f
                          || std::abs (now.holdDb[0] - shown.holdDb[0]) > 0.05f || std::abs (now.holdDb[1] - shown.holdDb[1]) > 0.05f
                          || now.clipped != shown.clipped || now.channels != shown.channels
                          || (isMain() && std::abs (now.loudness.shortTermLufs - shown.loudness.shortTermLufs) > 0.05f)
                          || (isMain() && std::abs (now.loudness.momentaryLufs - shown.loudness.momentaryLufs) > 0.05f)
                          || (isMain() && std::abs (now.loudness.integratedLufs - shown.loudness.integratedLufs) > 0.05f);
        // The tooltip only changes when the clip light or the highest level (in whole dB) does:
        // a tooltip whose text keeps changing never appears
        const auto tipChanged = now.clipped != shown.clipped || std::round (now.maxDb) != std::round (shown.maxDb);
        shown = now;

        if (changed)
            repaint();

        if (tipChanged)
            setTooltip (describe());
    }

    juce::String LevelMeter::describe() const
    {
        juce::String text;

        if (isMain())
            text << "Main output: level, and loudness in LUFS (right-click to pick short-term, momentary or integrated). ";
        else
            text << "Track level after volume and pan. ";

        text << "Highest since reset: " << (shown.maxDb <= MeterBank::floorDb + 0.01f ? juce::String ("-inf") : juce::String (juce::roundToInt (shown.maxDb)) + " dB") << ".";

        if (shown.clipped)
            text << " CLIPPED: something hit 0 dB, turn it down.";

        return text + " Click to reset, Shift+click to reset every meter (Ctrl+Shift+M), right-click for options.";
    }

    void LevelMeter::drawBar (juce::Graphics& g, juce::Rectangle<float> r, float levelDb, float holdDb) const
    {
        g.setColour (Theme::display);
        g.fillRect (r);

        const auto zone = [&] (float fromDb, float toDb, juce::Colour c)
        {
            const auto a = position (fromDb), b = position (juce::jmin (toDb, levelDb));

            if (b <= a)
                return;

            g.setColour (c);

            if (horizontal)  g.fillRect (r.withX (r.getX() + r.getWidth() * a).withWidth (r.getWidth() * (b - a)));
            else             g.fillRect (r.withY (r.getBottom() - r.getHeight() * b).withHeight (r.getHeight() * (b - a)));
        };

        zone (MeterBank::floorDb, -6.0f, green);
        zone (-6.0f, 0.0f, amber);
        zone (0.0f, topDb, red);

        if (holdDb > MeterBank::floorDb + 0.5f)
        {
            const auto p = position (holdDb);
            g.setColour (holdDb >= 0.0f ? red : holdDb >= -6.0f ? amber : Theme::text);

            if (horizontal)  g.fillRect (r.getX() + r.getWidth() * p - 1.0f, r.getY(), 2.0f, r.getHeight());
            else             g.fillRect (r.getX(), r.getBottom() - r.getHeight() * p - 1.0f, r.getWidth(), 2.0f);
        }
    }

    void LevelMeter::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.fillAll (isMain() && horizontal ? Theme::panel : Theme::display);   // opaque: the transport behind / the meter's own background

        if (isMain() && horizontal)
        {
            // MAIN | bars | number
            g.setColour (Theme::display);
            g.fillRoundedRectangle (r, (float) Theme::cornerRadius);
            g.setColour (Theme::hairline);
            g.drawRoundedRectangle (r.reduced (0.5f), (float) Theme::cornerRadius, 1.0f);
            auto inner = r.reduced (8.0f, 0.0f);
            g.setColour (shown.clipped ? red : Theme::text);
            g.setFont (Theme::font (Theme::captionFontSize, true));
            g.drawText (shown.clipped ? "CLIP" : "MAIN", inner.removeFromLeft (30.0f), juce::Justification::centredLeft, false);

            const auto& l = shown.loudness;
            const auto value = loudnessShown == Loudness::momentary  ? lufsText (l.momentaryLufs, l.momentaryValid)
                             : loudnessShown == Loudness::integrated ? lufsText (l.integratedLufs, l.integratedValid)
                                                                     : lufsText (l.shortTermLufs, l.shortTermValid);
            const auto label = loudnessShown == Loudness::momentary ? "LUFS M" : loudnessShown == Loudness::integrated ? "LUFS I" : "LUFS";
            const bool compact = getWidth() < 180;   // narrow transport: the number without its label
            auto numberArea = inner.removeFromRight (compact ? 38.0f : 82.0f);
            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::smallFontSize));
            g.drawText (compact ? value : value + " " + label, numberArea.withTrimmedLeft (6.0f), juce::Justification::centredLeft, false);

            auto bars = inner.withSizeKeepingCentre (inner.getWidth(), 12.0f);
            drawBar (g, bars.removeFromTop (5.0f), shown.levelDb[0], shown.holdDb[0]);
            bars.removeFromTop (2.0f);
            drawBar (g, bars.removeFromTop (5.0f), shown.levelDb[1], shown.holdDb[1]);
            return;
        }

        // Track meter: a clip light on top, two bars under it
        auto light = r.removeFromTop (4.0f);
        g.setColour (shown.clipped ? red : Theme::raised);
        g.fillRoundedRectangle (light, 1.0f);
        r.removeFromTop (1.0f);
        const auto barWidth = (r.getWidth() - 1.0f) / 2.0f;
        drawBar (g, r.removeFromLeft (barWidth), shown.levelDb[0], shown.holdDb[0]);
        r.removeFromLeft (1.0f);
        drawBar (g, r, shown.levelDb[1], shown.holdDb[1]);
    }

    void LevelMeter::reset()
    {
        if (isMain())   session.getMeters().resetMain();
        else            session.getMeters().resetTrack (trackIndex);

        timerCallback();
        repaint();
    }

    void LevelMeter::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            showMenu();
        else if (e.mods.isShiftDown())
            session.getMeters().resetAll();
        else
            reset();

        timerCallback();
    }

    void LevelMeter::showMenu()
    {
        auto& meters = session.getMeters();
        juce::PopupMenu menu, holdMenu, showMenu_;
        menu.addItem (1, isMain() ? "Reset the main meter (and integrated loudness)" : "Reset this meter");
        juce::PopupMenu::Item all ("Reset every meter");
        all.itemID = 2;
        all.shortcutKeyDescription = "Ctrl+Shift+M";
        menu.addItem (all);

        const std::pair<MeterBank::Hold, const char*> holds[] = { { MeterBank::Hold::off, "Off" }, { MeterBank::Hold::short_, "1.5 seconds" },
                                                                 { MeterBank::Hold::long_, "3 seconds" }, { MeterBank::Hold::untilReset, "Until reset" } };

        for (int i = 0; i < (int) std::size (holds); ++i)
            holdMenu.addItem (10 + i, holds[i].second, true, meters.getHold() == holds[i].first);

        menu.addSubMenu ("Peak hold", holdMenu);

        if (isMain())
        {
            showMenu_.addItem (20, "Short-term loudness (3 s)", true, loudnessShown == Loudness::shortTerm);
            showMenu_.addItem (21, "Momentary loudness (0.4 s)", true, loudnessShown == Loudness::momentary);
            showMenu_.addItem (22, "Integrated loudness (since reset)", true, loudnessShown == Loudness::integrated);
            menu.addSubMenu ("Number shows", showMenu_);
        }

        juce::Component::SafePointer<LevelMeter> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto& m = safe->session.getMeters();

            if (result == 1)
                safe->reset();
            else if (result == 2)
            {
                if (safe->onResetAll)   safe->onResetAll();
                else                    m.resetAll();
            }
            else if (result >= 10 && result < 14)
                m.setHold (std::array<MeterBank::Hold, 4> { MeterBank::Hold::off, MeterBank::Hold::short_, MeterBank::Hold::long_, MeterBank::Hold::untilReset }[(size_t) (result - 10)]);
            else if (result >= 20 && result < 23)
                loudnessShown = std::array<Loudness, 3> { Loudness::shortTerm, Loudness::momentary, Loudness::integrated }[(size_t) (result - 20)];

            safe->timerCallback();
            safe->repaint();
        });
    }
}
