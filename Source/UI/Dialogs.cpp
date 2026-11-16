/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "Dialogs.h"
#include "Theme.h"
#include "ExportView.h"

namespace ez::Dialogs
{
    namespace
    {
        juce::Component::SafePointer<juce::DialogWindow>& setupWindow()
        {
            static juce::Component::SafePointer<juce::DialogWindow> window;
            return window;
        }

        juce::Component::SafePointer<juce::DialogWindow>& exportWindow()
        {
            static juce::Component::SafePointer<juce::DialogWindow> window;
            return window;
        }
    }

    void showExport (Session& session, juce::Component* associatedComponent)
    {
        if (auto* existing = exportWindow().getComponent())
        {
            existing->toFront (true);
            return;
        }

        auto view = std::make_unique<ExportView> (session);

        // Never taller than the screen (the settings scroll instead)
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            view->setSize (juce::jmin (view->getWidth(), (int) display->userBounds.getWidth() - 40),
                           juce::jmin (view->getHeight(), (int) display->userBounds.getHeight() - 80));

        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle                  = "Export song - " + session.getSongName();
        o.dialogBackgroundColour       = Theme::panel;
        o.escapeKeyTriggersCloseButton = false;   // Esc stops a running export first (the view handles it)
        o.useNativeTitleBar            = true;
        o.resizable                    = true;
        o.useBottomRightCornerResizer  = false;
        o.componentToCentreAround      = associatedComponent;
        o.content.setOwned (view.release());

        exportWindow() = o.launchAsync();

        if (auto* window = exportWindow().getComponent())
        {
            window->setResizeLimits (700, 460, 1600, 1200);

            if (auto* content = window->getContentComponent())
                content->grabKeyboardFocus();
        }
    }

    void showSetup (AudioEngine& audioEngine, Session& session, juce::Component* associatedComponent, SetupView::Page page)
    {
        if (auto* existing = setupWindow().getComponent())
        {
            if (auto* view = dynamic_cast<SetupView*> (existing->getContentComponent()))
                view->showPage (page);

            existing->toFront (true);
            return;
        }

        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle                  = "Setup";
        o.dialogBackgroundColour       = Theme::panel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar            = true;
        o.resizable                    = true;
        o.useBottomRightCornerResizer  = false;
        o.componentToCentreAround      = associatedComponent;
        o.content.setOwned (new SetupView (audioEngine, session, page));

        setupWindow() = o.launchAsync();
    }

    void closeAll()
    {
        if (auto* window = exportWindow().getComponent())
            delete window;

        exportWindow() = nullptr;

        if (auto* window = setupWindow().getComponent())
            delete window;   // the modal manager notices and won't delete it again

        setupWindow() = nullptr;
    }
}
