/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "SetupView.h"
#include "RoutingView.h"
#include "RecordingSetup.h"
#include "MidiSetup.h"
#include "../Engine/AudioEngine.h"

namespace ez
{
    namespace
    {
        constexpr int sidebarWidth = 200, rowHeight = 32, footerHeight = 52;
    }

    class SetupView::PageRow : public NavRow
    {
    public:
        PageRow (const juce::String& name, Icon rowIcon, Page p) : NavRow (name, rowIcon, false), page (p) {}
        const Page page;
    };

    SetupView::SetupView (AudioEngine& audioEngine, Session& session, Page firstPage)
    {
        auto& engine = audioEngine.getEngine();

        audioPage = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.getDeviceManager().deviceManager,
                                                                          0, 512,    // audio inputs
                                                                          1, 512,    // audio outputs
                                                                          true,      // MIDI inputs
                                                                          false,     // MIDI output
                                                                          true,      // stereo pairs
                                                                          false);    // hide advanced options
        routingPage = std::make_unique<RoutingView> (audioEngine, session);
        recordingPage = std::make_unique<RecordingSetup> (audioEngine, session);
        midiPage = std::make_unique<MidiSetup> (session);

        auto& pm = engine.getPluginManager();
        pluginsPage = std::make_unique<juce::PluginListComponent> (pm.pluginFormatManager, pm.knownPluginList,
                                                                  // Beside the settings, not in the temp folder (which is emptied): a plugin
                                                                  // that crashed a scan must stay known across restarts
                                                                  engine.getPropertyStorage().getAppPrefsFolder().getChildFile ("PluginScanDeadMansPedal"),
                                                                  std::addressof (engine.getPropertyStorage().getPropertiesFile()),
                                                                  true);

        for (auto* page : { audioPage.get(), routingPage.get(), pluginsPage.get(), recordingPage.get(), midiPage.get() })
            addChildComponent (page);

        struct Def { const char* name; Icon icon; Page page; const char* phase; };
        const Def defs[] =
        {
            { "Audio device",        Icon::speaker,    Page::audioDevice, nullptr },
            { "Routing",             Icon::headphones, Page::routing,     nullptr },
            { "Plugins",             Icon::plug,       Page::plugins,     nullptr },
            { "MIDI & Controllers",  Icon::piano,      Page::midi,        nullptr },
            { "Recording",           Icon::mic,        Page::recording,   nullptr },
        };

        for (const auto& d : defs)
        {
            auto* row = rows.add (new PageRow (d.name, d.icon, d.page));
            row->onClick = [this, page = d.page] { showPage (page); };

            if (d.phase != nullptr)
                Planned::mark (*row, d.phase);

            addAndMakeVisible (row);
        }

        doneButton.onClick = [this]
        {
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState (0);   // the window deletes itself
        };
        addAndMakeVisible (doneButton);

        setSize (Theme::setupWidth, Theme::setupHeight);
        showPage (firstPage);
    }

    SetupView::~SetupView() = default;

    juce::Component* SetupView::getPageComponent (Page page) const
    {
        switch (page)
        {
            case Page::audioDevice: return audioPage.get();
            case Page::routing:     return routingPage.get();
            case Page::plugins:     return pluginsPage.get();
            case Page::recording:   return recordingPage.get();
            case Page::midi:        return midiPage.get();
        }

        return nullptr;
    }

    void SetupView::showPage (Page page)
    {
        if (getPageComponent (page) == nullptr)
            return;   // planned pages can't be shown yet

        current = page;

        for (auto p : { Page::audioDevice, Page::routing, Page::plugins, Page::recording, Page::midi })
            if (auto* c = getPageComponent (p))
                c->setVisible (p == page);

        for (auto* row : rows)
            row->setToggleState (row->page == page, juce::dontSendNotification);

        resized();
        repaint();   // the page title
    }

    void SetupView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::laneSeparator);
        g.drawVerticalLine (sidebarArea.getRight(), 0.0f, (float) footerArea.getY());
        g.setColour (Theme::bar);
        g.fillRect (footerArea);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (footerArea.getY(), 0.0f, (float) getWidth());

        // The page's title and what it is for
        if (! titleArea.isEmpty())
        {
            const auto [title, description] = describePage (current);
            auto t = titleArea;
            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::titleFontSize + 2.0f, Theme::Weight::semibold));
            g.drawText (title, t.removeFromTop (26), juce::Justification::centredLeft, true);
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize + 0.5f));
            g.drawFittedText (description, t, juce::Justification::topLeft, 2);
            g.setColour (Theme::laneSeparator);
            g.drawHorizontalLine (titleArea.getBottom() + 6, (float) titleArea.getX(), (float) titleArea.getRight());
        }

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::smallFontSize + 0.5f));
        g.drawText ("Changes apply right away. Playback pauses while the audio device restarts.",
                    footerArea.reduced (18, 0).withTrimmedRight (doneButton.getWidth() + 12), juce::Justification::centredLeft, true);
    }

    void SetupView::paintOverChildren (juce::Graphics& g)
    {
        for (auto* row : rows)
            if (Planned::isPlanned (*row))
                Planned::drawChip (g, row->getProperties()["ezPlannedPhase"].toString(),
                                   { row->getRight() - 8, row->getY() + (row->getHeight() - 14) / 2 });
    }

    void SetupView::resized()
    {
        auto r = getLocalBounds();
        footerArea = r.removeFromBottom (footerHeight);
        doneButton.setBounds (footerArea.reduced (18, 0).removeFromRight (90).withSizeKeepingCentre (90, 30));

        sidebarArea = r.removeFromLeft (sidebarWidth);
        auto list = sidebarArea.reduced (12);

        for (auto* row : rows)
        {
            row->setBounds (list.removeFromTop (rowHeight));
            list.removeFromTop (2);
        }

        auto content = r.reduced (20, 14);
        titleArea = content.removeFromTop (58);
        content.removeFromTop (14);

        if (auto* page = getPageComponent (current))
            page->setBounds (content);
    }

    std::pair<juce::String, juce::String> SetupView::describePage (Page page)
    {
        switch (page)
        {
            case Page::audioDevice: return { "Audio device", "Your sound card or interface: what plays the sound and what you record from. "
                                                             "A smaller buffer means less delay but more work for the computer." };
            case Page::routing:     return { "Routing", "Which inputs feed which tracks, and where the main mix goes." };
            case Page::plugins:     return { "Plugins", "Your VST3 instruments and effects. Click Options to scan for them; each is checked "
                                                        "in a separate process, so a broken plugin can't crash EzDAWsIt." };
            case Page::recording:   return { "Recording", "How tracks record: inputs, levels, timing and takes." };
            case Page::midi:        return { "MIDI & Controllers", "Keyboards, pads and knobs: what EzDAWsIt hears, the clock it sends, and controls you've linked." };
        }

        return {};
    }
}
