/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Session's running export (private to Session: SessionExport.cpp and Session.cpp, whose
    destructor needs the complete type).
*/

#pragma once

#include "Session.h"
#include "LoudnessScan.h"

namespace ez
{
    struct Session::ExportJobs
    {
        te::RenderQueue queue;   // first member destroyed last: jobs hold the queue's results
        std::vector<te::RenderQueue::JobPtr> jobs;
        juce::StringArray names;               // what each job is, for the status line
        juce::StringArray problems;
        bool measureLoudness = false;          // a full mix in an audio format
        bool cancelRequested = false;
        std::unique_ptr<LoudnessScan> scan;
        te::Plugin::Ptr chainToSwitchBackOn;   // "Use the mastering chain" off: switched off for the render

        ~ExportJobs()
        {
            scan.reset();          // stop measuring first
            queue.cancelAll();     // ~RenderQueue cancels too, but say so before the jobs vector goes
        }
    };
}
