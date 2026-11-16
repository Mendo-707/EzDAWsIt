/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Comparing version numbers ("1.2.0", "v1.3.0-beta.1"), for Help > Check for updates.
*/

#pragma once

#include "Common.h"

namespace ez::Version
{
    /** < 0 if a is older than b, 0 if the same, > 0 if a is newer. A leading "v" is ignored.
        Numbers compare as numbers (1.10 > 1.9). With equal numbers, a release is newer than
        any pre-release of it (1.0.0 > 1.0.0-beta.2), and pre-releases compare by their tag. */
    inline int compare (juce::String a, juce::String b)
    {
        const auto clean = [] (juce::String v) { v = v.trim(); return v.startsWithIgnoreCase ("v") ? v.substring (1) : v; };
        a = clean (a);
        b = clean (b);

        const auto numbersA = juce::StringArray::fromTokens (a.upToFirstOccurrenceOf ("-", false, false), ".", "");
        const auto numbersB = juce::StringArray::fromTokens (b.upToFirstOccurrenceOf ("-", false, false), ".", "");

        for (int i = 0; i < juce::jmax (numbersA.size(), numbersB.size()); ++i)
        {
            const auto na = numbersA[i].getIntValue(), nb = numbersB[i].getIntValue();

            if (na != nb)
                return na < nb ? -1 : 1;
        }

        const auto tagA = a.fromFirstOccurrenceOf ("-", false, false), tagB = b.fromFirstOccurrenceOf ("-", false, false);

        if (tagA == tagB)              return 0;
        if (tagA.isEmpty())            return 1;    // the release beats its betas
        if (tagB.isEmpty())            return -1;
        return tagA.compareNatural (tagB) < 0 ? -1 : 1;
    }
}
