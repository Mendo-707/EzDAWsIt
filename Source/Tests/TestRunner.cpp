/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "TestRunner.h"

namespace ez::Tests
{
    namespace
    {
        class ReportingRunner : public juce::UnitTestRunner
        {
        public:
            juce::StringArray lines;

            void logMessage (const juce::String& message) override
            {
                lines.add (message);
                juce::Logger::writeToLog (message);
            }
        };
    }

    int runAll (const juce::File& resultsFile)
    {
        ReportingRunner runner;
        runner.setAssertOnFailure (false);
        runner.runTestsInCategory (category);

        int passes = 0, failures = 0;

        for (int i = 0; i < runner.getNumResults(); ++i)
        {
            if (auto* result = runner.getResult (i))
            {
                passes   += result->passes;
                failures += result->failures;
            }
        }

        if (passes == 0)
            ++failures;   // no tests ran at all: never report that as a pass

        runner.lines.add ({});
        runner.lines.add ("RESULT: " + juce::String (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED")
                          + " (" + juce::String (passes) + " passed, " + juce::String (failures) + " failed)");

        resultsFile.replaceWithText (runner.lines.joinIntoString ("\n"));
        return failures;
    }
}
