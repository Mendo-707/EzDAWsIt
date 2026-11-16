#!/usr/bin/env python3
"""Writes installer/THIRD-PARTY-NOTICES.txt from the licence files in deps/.

Run after changing the JUCE or Tracktion Engine commit in build.bat:
    python3 tools/make-notices.py            (from the repo root, with deps/ downloaded)
Every component that ends up inside EzDAWsIt.exe is listed with its licence text.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEPS = os.environ.get("EZ_DEPS", os.path.join(ROOT, "deps"))
J = "JUCE/modules/"
T = "tracktion_engine/modules/"

def sha(name):
    m = re.search(name + r"=([0-9a-f]+)", open(os.path.join(ROOT, "build.bat")).read())
    return m.group(1) if m else "?"

# (component, licence, file with the licence text; a source file means "its first comment block")
COMPONENTS = [
    ("JUCE (framework)", "GNU AGPL v3 (full text at the end of this file)", None),
    ("Tracktion Engine", "GNU GPL v3 or later (full text in LICENSE.txt)", None),
    ("Steinberg ASIO SDK headers (Windows ASIO audio)", "GNU GPL v3 (Steinberg dual licence)", J + "juce_audio_devices/native/asio/LICENSE.txt"),
    ("Steinberg VST3 SDK (plugin hosting)", "MIT", J + "juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt"),
    ("zlib", "zlib licence", J + "juce_core/zip/zlib/LICENSE"),
    ("libpng", "libpng licence", J + "juce_graphics/image_formats/pnglib/LICENSE"),
    ("libjpeg (Independent JPEG Group)", "IJG licence", J + "juce_graphics/image_formats/jpglib/README"),
    ("libwebp", "BSD 3-clause", J + "juce_graphics/image_formats/libwebp/COPYING"),
    ("HarfBuzz", "Old MIT", J + "juce_graphics/fonts/harfbuzz/COPYING"),
    ("SheenBidi", "Apache 2.0", J + "juce_graphics/unicode/sheenbidi/LICENSE"),
    ("LunaSVG", "MIT", J + "juce_graphics/drawables/lunasvg/LICENSE"),
    ("PlutoVG", "MIT", J + "juce_graphics/drawables/lunasvg/plutovg/LICENSE"),
    ("FLAC", "BSD 3-clause", J + "juce_audio_formats/codecs/flac/Flac Licence.txt"),
    ("Ogg", "BSD 3-clause", J + "juce_audio_formats/codecs/ogg/COPYING"),
    ("Vorbis", "BSD 3-clause", J + "juce_audio_formats/codecs/vorbis/COPYING"),
    ("Opus", "BSD 3-clause", J + "juce_audio_formats/codecs/opus/opus/COPYING"),
    ("opusfile", "BSD 3-clause", J + "juce_audio_formats/codecs/opus/opusfile/COPYING"),
    ("libopusenc", "BSD 3-clause", J + "juce_audio_formats/codecs/opus/libopusenc/COPYING"),
    ("CHOC (in JUCE)", "ISC", J + "juce_javascript/choc/LICENSE.md"),
    ("CHOC (in Tracktion Engine)", "ISC", T + "3rd_party/choc/LICENSE.md"),
    ("crill", "Boost Software Licence 1.0", T + "3rd_party/crill/LICENSE.txt"),
    ("farbot", "MIT", T + "tracktion_graph/3rd_party/farbot/LICENSE"),
    ("libsamplerate", "BSD 2-clause", T + "3rd_party/libsamplerate/samplerate.c"),
    ("magic_enum", "MIT", T + "3rd_party/magic_enum/magic_enum.hpp"),
    ("MPMCQueue (Erik Rigtorp)", "MIT", T + "3rd_party/rigtorp/MPMCQueue.h"),
    ("expected (TartanLlama)", "CC0 1.0", T + "3rd_party/expected/expected.hpp"),
    ("rpmalloc", "Public domain / MIT", T + "3rd_party/rpmalloc/rpmalloc.h"),
    ("Signalsmith Stretch", "MIT", T + "tracktion_engine/3rd_party/signalsmith-stretch/LICENSE.txt"),
    ("Signalsmith Linear", "MIT", T + "tracktion_engine/3rd_party/signalsmith-stretch/signalsmith-linear/LICENSE.txt"),
    ("SoundTouch (time stretching)", "GNU LGPL v2.1 (full text at the end of this file)", T + "tracktion_engine/3rd_party/soundtouch/source/SoundTouch/SoundTouch.cpp"),
    ("Airwindows effects", "MIT", T + "tracktion_engine/3rd_party/airwindows/Console6Buss/Console6BussProc.cpp"),
]

BSD2 = """Copyright (c) {}
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."""

def first_comment(text):
    """The licence block at the top of a source file."""
    m = re.match(r"\s*(/\*.*?\*/)", text, re.S)
    if m:
        return m.group(1)
    lines = []
    for line in text.splitlines():
        if line.strip().startswith("//"):
            lines.append(line)
        elif lines and line.strip():
            break
    return "\n".join(lines)

def licence_text(rel):
    path = os.path.join(DEPS, rel)
    if not os.path.isfile(path):
        sys.exit("missing: " + path)
    text = open(path, encoding="utf-8", errors="replace").read()
    if rel.endswith("jpglib/README"):
        m = re.search(r"(?s)\nLEGAL ISSUES\n.*?(?=\n[A-Z][A-Z ]+\n=+\n|\nREFERENCES\n)", text)
        return (m.group(0) if m else text).strip()
    if os.path.splitext(path)[1] in (".h", ".hpp", ".c", ".cpp"):
        text = first_comment(text)
        if "libsamplerate" in rel:
            text += "\n\n" + BSD2.format("2002-2016, Erik de Castro Lopo <erikd@mega-nerd.com>")
        if "SoundTouch" in rel:
            text = re.sub(r"(?s)^.*?(Copyright)", r"\1", text)   # skip the class description
    return text.strip()

def main():
    rule = "=" * 78
    out = [
        "EzDAWsIt - third-party notices",
        rule,
        "",
        "EzDAWsIt is free software: you can redistribute it and/or modify it under the",
        "terms of the GNU General Public License version 3 (LICENSE.txt, next to this",
        "file). It comes with NO WARRANTY.",
        "",
        "Source code: the complete source of this version is published alongside the",
        "installer as EzDAWsIt-<version>-source.zip. It builds with build.bat, which",
        "downloads these exact versions of its two frameworks:",
        "    JUCE              https://github.com/juce-framework/JUCE  commit " + sha("JUCE_SHA"),
        "    Tracktion Engine  https://github.com/Tracktion/tracktion_engine  commit " + sha("TE_SHA"),
        "",
        "This software is based in part on the work of the Independent JPEG Group.",
        "",
        "EzDAWsIt contains the following software. Each is used under the licence named,",
        "whose text follows.",
        "",
    ]
    for name, lic, _ in COMPONENTS:
        out.append("  - %s: %s" % (name, lic))
    out.append("")
    for name, lic, rel in COMPONENTS:
        if rel is None:
            continue
        out += [rule, name + "  (" + lic + ")", rule, "", licence_text(rel), ""]
    for title, path in (("GNU AFFERO GENERAL PUBLIC LICENSE v3 (JUCE)", os.path.join(DEPS, J + "juce_gui_extra/native/typescript/webview-interop/LICENSE-AGPL.md")),
                        ("GNU LESSER GENERAL PUBLIC LICENSE v2.1 (SoundTouch)", "/usr/share/common-licenses/LGPL-2.1")):
        if not os.path.isfile(path):
            sys.exit("missing: " + path)
        out += [rule, title, rule, "", open(path, encoding="utf-8").read().strip(), ""]
    text = "\r\n".join(out).replace("\r\n", "\n").replace("\n", "\r\n") + "\r\n"
    dest = os.path.join(ROOT, "installer", "THIRD-PARTY-NOTICES.txt")
    open(dest, "w", encoding="utf-8", newline="").write(text)
    print("wrote", dest, len(text), "bytes")

main()
