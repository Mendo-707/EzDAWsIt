/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "Ruler.h"
#include "../Theme.h"

namespace ez
{
    Ruler::Ruler (Session& s, TimelineViewState& v)
        : session (s), view (v)
    {
        setTooltip ("Top: click to move the playhead.  Bottom: drag to set the loop, double-click to turn looping on/off.");
    }

    te::TimePosition Ruler::snappedTimeAt (int x) const
    {
        const auto time = view.xToTime (x);
        return view.snapEnabled ? session.snapToGrid (time, view.getGridBeats (session.getBpm())) : time;
    }

    void Ruler::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::panel);

        auto* edit = session.getEdit();

        if (edit == nullptr)
            return;

        // Loop region
        const auto loop = session.getLoopRange();

        if (loop.getLength() > te::TimeDuration())
        {
            const auto x1 = view.timeToX (loop.getStart()), x2 = view.timeToX (loop.getEnd());
            g.setColour (session.isLooping() ? Theme::loopRegion : Theme::loopRegionOff);
            g.fillRect (juce::Rectangle<int> (x1, getHeight() / 2, x2 - x1, getHeight() / 2));
        }

        // Bar and beat ticks
        const auto& tempo = edit->tempoSequence;
        const auto gridBeats = view.getGridBeats (session.getBpm());
        g.setFont (juce::Font (juce::FontOptions (Theme::statusFontSize)));

        // Bar lines follow the time signatures (a zoomed-out grid steps by whole bars)
        for (auto [beat, isBar] : session.getGridLines (tempo.toBeats (view.xToTime (0)).inBeats(), tempo.toBeats (view.xToTime (getWidth())).inBeats(), gridBeats))
        {
            const auto x = view.timeToX (tempo.toTime (te::BeatPosition::fromBeats (beat)));
            g.setColour (isBar ? Theme::text : Theme::textDim);
            g.drawVerticalLine (x, isBar ? 0.0f : (float) getHeight() * 0.6f, (float) getHeight());

            if (isBar)
                g.drawText (juce::String (session.getBarAt (beat + 1.0e-7).first + 1), x + 3, 0, 40, getHeight() / 2, juce::Justification::centredLeft);
        }

        // Playhead marker
        const auto px = view.timeToX (session.getPosition());
        juce::Path marker;
        marker.addTriangle ((float) px - 6.0f, 0.0f, (float) px + 6.0f, 0.0f, (float) px, 10.0f);
        g.setColour (Theme::playhead);
        g.fillPath (marker);
    }

    void Ruler::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
        {
            showMenu (snappedTimeAt (e.x));
            return;
        }

        draggingLoop = e.y > getHeight() / 2;

        if (draggingLoop)
            loopDragStart = snappedTimeAt (e.x);
        else
            session.setPosition (snappedTimeAt (e.x));

        repaint();
    }

    void Ruler::showMenu (te::TimePosition time)
    {
        enum { playFromHereId = 1, loopStartId, loopEndId, loopToggleId, punchToggleId, punchInId, punchOutId };

        const auto loop = session.getLoopRange();
        const bool canEditLoop = ! session.isRecording();

        juce::PopupMenu menu;
        menu.addItem (playFromHereId, "Move playhead here");
        menu.addSeparator();
        menu.addItem (loopStartId,  "Start loop here", canEditLoop && (loop.getLength() <= te::TimeDuration() || time < loop.getEnd()));
        menu.addItem (loopEndId,    "End loop here", canEditLoop && time > loop.getStart());
        menu.addItem (loopToggleId, "Loop on\tL", canEditLoop, session.isLooping());
        menu.addSeparator();
        menu.addItem (punchInId,     "Punch in here (recording starts here)\tCtrl+Shift+I", canEditLoop);
        menu.addItem (punchOutId,    "Punch out here (recording stops here)\tCtrl+Shift+O", canEditLoop && time > loop.getStart());
        menu.addItem (punchToggleId, "Punch on (record only inside the loop)\tP", canEditLoop, session.isPunchOn());

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                            [safeThis = juce::Component::SafePointer<Ruler> (this), time] (int result)
                            {
                                if (safeThis == nullptr || result <= 0)
                                    return;

                                auto& s = safeThis->session;
                                const auto range = s.getLoopRange();
                                const auto oneBar = s.getBarLengthAt (time);

                                switch (result)
                                {
                                    case playFromHereId: s.setPosition (time); break;
                                    case loopStartId:
                                        s.setLoopRange (time, range.getLength() > te::TimeDuration() && range.getEnd() > time ? range.getEnd() : time + oneBar);
                                        s.setLooping (true);
                                        break;
                                    case loopEndId:
                                        s.setLoopRange (range.getStart(), time);
                                        s.setLooping (true);
                                        break;
                                    case loopToggleId:   s.setLooping (! s.isLooping()); break;
                                    case punchInId:
                                        s.setLoopRange (time, range.getLength() > te::TimeDuration() && range.getEnd() > time ? range.getEnd() : time + oneBar);
                                        if (! s.setPunch (true)) s.showHint (s.getLastError());
                                        break;
                                    case punchOutId:
                                        s.setLoopRange (range.getStart(), time);
                                        if (! s.setPunch (true)) s.showHint (s.getLastError());
                                        break;
                                    case punchToggleId:
                                        if (! s.setPunch (! s.isPunchOn())) s.showHint (s.getLastError());
                                        break;
                                    default: break;
                                }

                                safeThis->repaint();
                            });
    }

    void Ruler::mouseDrag (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;

        if (draggingLoop)
        {
            const auto end = snappedTimeAt (e.x);

            if (end != loopDragStart && ! session.isRecording())
            {
                session.setLoopRange (loopDragStart, end);
                session.setLooping (true);
            }
        }
        else
        {
            session.setPosition (snappedTimeAt (e.x));
        }

        repaint();
    }

    void Ruler::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.y > getHeight() / 2 && ! session.isRecording())
        {
            session.setLooping (! session.isLooping());
            repaint();
        }
    }
}
