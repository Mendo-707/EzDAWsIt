#!/usr/bin/env bash
# Builds and runs the DSP unit tests (Source/Engine/Dsp.h) natively on Linux: JUCE core + audio_basics only,
# no Tracktion, no GUI. Usage: bash tools/dsp-tests/run-linux.sh [path/to/JUCE/modules]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/../.."
JUCE="${1:-$ROOT/deps/JUCE/modules}"
OUT="${TMPDIR:-/tmp}/ezdawsit-dsp-tests"; mkdir -p "$OUT/Core" "$OUT/Engine"
printf '#pragma once\n#include <juce_core/juce_core.h>\n#include <juce_audio_basics/juce_audio_basics.h>\n' > "$OUT/Core/Common.h"
cp "$ROOT/Source/Engine/Dsp.h" "$OUT/Engine/"
printf '#include <juce_core/juce_core.cpp>\n#include <juce_audio_basics/juce_audio_basics.cpp>\nnamespace juce { const char* juce_compilationDate = __DATE__; const char* juce_compilationTime = __TIME__; }\n' > "$OUT/juce.cpp"
DEFS="-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 -DJUCE_USE_CURL=0"
[ -f "$OUT/juce.o" ] || g++ -std=c++20 -O1 -c -I"$JUCE" $DEFS "$OUT/juce.cpp" -o "$OUT/juce.o"
g++ -std=c++20 -O1 -I"$OUT" -I"$JUCE" $DEFS "$HERE/DspTests.cpp" "$OUT/juce.o" -o "$OUT/dsp-tests" -lpthread -ldl -lz
"$OUT/dsp-tests"
