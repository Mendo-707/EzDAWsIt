/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "SpectrumView.h"
#include "Theme.h"
#include "../Engine/Meters.h"

namespace ez
{
    namespace
    {
        constexpr float floorDb = -84.0f, topDb = -6.0f;
    }

    SpectrumView::SpectrumView (Session& s) : session (s)
    {
        setOpaque (false);
        setTooltip ("Spectrum of the main output: bass on the left, treble on the right. Play the song to see it");
    }

    void SpectrumView::visibilityChanged()
    {
        if (isShowing()) startTimerHz (30);
        else             stopTimer();
    }

    void SpectrumView::timerCallback()
    {
        if (! isShowing())
            return stopTimer();

        const auto& now = session.getMeters().getSpectrum();

        if (now != lastDrawn)
        {
            lastDrawn = now;
            repaint();
        }
    }

    void SpectrumView::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Theme::display);
        g.fillRoundedRectangle (bounds, (float) Theme::cornerRadius);
        const auto area = bounds.reduced (6.0f, 4.0f);
        const auto xFor = [&area] (float hz) { return area.getX() + area.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };

        // the frequency grid, labelled when there's room
        g.setFont (Theme::font (Theme::captionFontSize - 0.5f));

        for (auto hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const bool decade = hz == 100.0f || hz == 1000.0f || hz == 10000.0f;
            const auto x = xFor (hz);
            g.setColour (Theme::hairline.withAlpha (decade ? 0.9f : 0.4f));
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

            if (decade && area.getHeight() >= 36.0f)
            {
                g.setColour (Theme::textFaint);
                g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (x + 3.0f, area.getBottom() - 11.0f, 28.0f, 10.0f), juce::Justification::centredLeft, false);
            }
        }

        const auto& bands = lastDrawn;

        if (bands.empty() || *std::max_element (bands.begin(), bands.end()) <= floorDb)
        {
            g.setColour (Theme::textFaint);
            g.setFont (Theme::font (Theme::smallFontSize));
            g.drawText ("Spectrum: play the song", area, juce::Justification::centred, false);
            return;
        }

        // the curve: one point per band, smoothed into a path, filled from the floor
        juce::Path curve;
        const auto n = (int) bands.size();

        for (int b = 0; b < n; ++b)
        {
            const auto x = area.getX() + area.getWidth() * ((float) b + 0.5f) / (float) n;
            const auto y = juce::jmap (juce::jlimit (floorDb, topDb, bands[(size_t) b]), floorDb, topDb, area.getBottom(), area.getY());

            if (b == 0) curve.startNewSubPath (area.getX(), y);
            curve.lineTo (x, y);
        }

        curve.lineTo (area.getRight(), curve.getCurrentPosition().y);
        juce::Path fill (curve);
        fill.lineTo (area.getRight(), area.getBottom());
        fill.lineTo (area.getX(), area.getBottom());
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient::vertical (Theme::accent.withAlpha (0.45f), area.getY(), Theme::accent.withAlpha (0.05f), area.getBottom()));
        g.fillPath (fill);
        g.setColour (Theme::accent);
        g.strokePath (curve, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
