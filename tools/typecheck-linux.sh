#!/usr/bin/env bash
# Fast type-check of every EzDAWsIt .cpp on Linux (no linking, no GUI libs needed).
# Used by AI agents working without Windows. Exit code 0 = everything compiles warning-free.
# Usage: tools/typecheck-linux.sh            (run from the repo root)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/deps"; SHIM="$ROOT/build-linux-check"
JUCE_SHA=$(grep -oP 'JUCE_SHA=\K[0-9a-f]+' "$ROOT/build.bat")
TE_SHA=$(grep -oP 'TE_SHA=\K[0-9a-f]+' "$ROOT/build.bat")

fetch() { # dir url sha
  [[ -f "$1/.ezdawsit-$3" ]] && return 0
  rm -rf "$1" && git init -q "$1" && git -C "$1" remote add origin "$2" \
    && git -C "$1" fetch -q --depth 1 origin "$3" && git -C "$1" checkout -q FETCH_HEAD && touch "$1/.ezdawsit-$3"
}
fetch "$DEPS/JUCE" https://github.com/juce-framework/JUCE.git "$JUCE_SHA" || { echo "FAIL: could not download JUCE"; exit 2; }
fetch "$DEPS/tracktion_engine" https://github.com/Tracktion/tracktion_engine.git "$TE_SHA" || { echo "FAIL: could not download Tracktion"; exit 2; }

# Stand-in for the JuceHeader.h that CMake generates on Windows
MODS="juce_core juce_events juce_data_structures juce_graphics juce_gui_basics juce_gui_extra juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_processors_headless juce_audio_processors juce_audio_utils juce_dsp juce_osc juce_animation juce_javascript juce_midi_ci tracktion_core tracktion_graph tracktion_engine"
mkdir -p "$SHIM"
{ echo "#pragma once"; for m in $MODS; do echo "#define JUCE_MODULE_AVAILABLE_$m 1"; done
  echo "#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1"; for m in $MODS; do echo "#include <$m/$m.h>"; done
  echo 'namespace ProjectInfo { const char* const projectName = "EzDAWsIt"; const char* const companyName = "EzDAW"; const char* const versionString = "1.0.0"; const int versionNumber = 0x10000; }'
} > "$SHIM/JuceHeader.h"

FLAGS="-std=c++20 -fsyntax-only -Wall -Wextra -Wshadow -Wno-unused-parameter -I$SHIM -I$DEPS/JUCE/modules -I$DEPS/tracktion_engine/modules -I$ROOT/Source $(pkg-config --cflags freetype2 2>/dev/null)
 -DDONT_SET_USING_JUCE_NAMESPACE=1 -DJUCE_PLUGINHOST_VST3=1 -DJUCE_MODAL_LOOPS_PERMITTED=1 -DJUCE_STRICT_REFCOUNTEDPOINTER=1
 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DNDEBUG=1 -DJUCE_APPLICATION_NAME_STRING=\"EzDAWsIt\" -DJUCE_APPLICATION_VERSION_STRING=\"1.0.0\""

failed=0
for f in $(find "$ROOT/Source" -name '*.cpp' | sort); do
  extra=""; [[ "$f" == *TracktionExampleBridge* ]] && extra="-I$DEPS/tracktion_engine/examples/common"
  log="$SHIM/$(basename "$f").log"
  if g++ $FLAGS $extra "$f" > "$log" 2>&1; then
    warnings=$(grep -c "$ROOT/Source.*warning" "$log")
    if [[ $warnings -gt 0 ]]; then echo "WARN $(basename "$f"): $warnings warning(s)"; grep "$ROOT/Source.*warning" "$log" | head -5; failed=1
    else echo "OK   $(basename "$f")"; fi
  else
    echo "FAIL $(basename "$f")"; grep -m8 "error" "$log"; failed=1
  fi
done
[[ $failed -eq 0 ]] && echo "RESULT: all files type-check cleanly" || echo "RESULT: PROBLEMS FOUND"
exit $failed
