#!/usr/bin/env python3
"""Regenerates the About dialog's credits and Legal notices.

Copies every bundled component's own license file, verbatim, into
src/appshell/resources/about/licenses/ and writes components.json (the list the
About dialog shows) plus mixxx_credits.json (Mixxx's own About credits).

Run after adding or removing a dependency, with a configured build directory
(the fetched dependencies in build/_deps carry most of the license files):

    python3 buildscripts/tools/update_about_credits.py [--build build]
"""
import argparse
import hashlib
import html
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HOME = os.path.expanduser("~")
MIXXX_SRC = os.environ.get("MIXXX_SRC", os.path.join(HOME, "Developer", "Parked MixHouse DJ Project"))
OPENKEYSCAN_SRC = os.environ.get("OPENKEYSCAN_SRC", os.path.join(HOME, "Developer", "openkeyscan-analyzer"))
# The Qt SDK doesn't ship its license texts; this is the FSF's LGPL-3.0 text.
LGPL3_TEXT = os.environ.get("LGPL3_TEXT", "/opt/homebrew/Cellar/gmp/6.3.0/COPYING.LESSERv3")

OUT = os.path.join(ROOT, "src", "appshell", "resources", "about")

FORKS = "Built on"
DJ = "DJ analysis"
AUDIO = "Audio engine and file formats"
FRAMEWORK = "Application framework"

# id, group, name, url, copyright, license (SPDX), license files ($BUILD = build dir),
# optional ("header", n) to take the first n lines of a source file instead.
COMPONENTS = [
    ("audacity", FORKS, "Audacity", "https://github.com/audacity/audacity",
     "Copyright © 1999-2026 Audacity Team and Muse Group",
     "GPL-3.0 and GPL-2.0-or-later", ["LICENSE.txt"]),
    ("muse-framework", FORKS, "MuseScore framework", "https://github.com/musescore/MuseScore",
     "Copyright © MuseScore Limited, Muse Group and others",
     "GPL-3.0-only", ["muse/LICENSE.txt"]),
    ("mixxx", FORKS, "Mixxx", "https://github.com/mixxxdj/mixxx",
     "Copyright © 2001-2026 Mixxx Development Team",
     "GPL-2.0-or-later", ["$MIXXX/LICENSE"]),

    ("qm-dsp", DJ, "Queen Mary DSP library", "https://github.com/c4dm/qm-dsp",
     "Copyright © Centre for Digital Music, Queen Mary University of London",
     "GPL-2.0-or-later", ["src/mixhouse/thirdparty/qm-dsp/COPYING"]),
    ("kissfft", DJ, "KISS FFT", "https://github.com/mborgerding/kissfft",
     "Copyright © 2003-2010 Mark Borgerding", "BSD-3-Clause",
     ["src/mixhouse/thirdparty/qm-dsp/ext/kissfft/COPYING"]),
    ("beat-this", DJ, "Beat This! (model)", "https://github.com/CPJKU/beat_this",
     "Copyright © 2024 Institute of Computational Perception, JKU Linz", "MIT",
     ["src/mixhouse/thirdparty/beat_this_model/LICENSE"]),
    ("beat-this-cpp", DJ, "beat_this_cpp", "https://github.com/mosynthkey/beat_this_cpp",
     "Copyright © 2025 Masaki Ono / Melissa Audio", "MIT",
     ["src/mixhouse/thirdparty/beat_this_cpp/LICENSE"]),
    ("onnxruntime", DJ, "ONNX Runtime", "https://github.com/microsoft/onnxruntime",
     "Copyright © Microsoft Corporation", "MIT",
     ["mixhouse_deps/onnxruntime-1.18.0/LICENSE", "mixhouse_deps/onnxruntime-1.18.0/ThirdPartyNotices.txt"]),
    ("r8brain", DJ, "r8brain-free-src", "https://github.com/avaneev/r8brain-free-src",
     "Copyright © 2013-2026 Aleksey Vaneev", "MIT",
     ["src/mixhouse/thirdparty/beat_this_cpp/r8brain/LICENSE"]),
    ("pocketfft", DJ, "pocketfft", "https://github.com/mreineck/pocketfft",
     "Copyright © 2010-2018 Max-Planck-Society", "BSD-3-Clause",
     ["src/mixhouse/thirdparty/beat_this_cpp/pocketfft/LICENSE.md"]),
    ("openkeyscan", DJ, "OpenKeyScan", "https://github.com/rekordcloud/openkeyscan-analyzer",
     "Copyright © 2025 Alexander Sommer", "MIT", ["$OPENKEYSCAN/LICENSE"]),

    ("portaudio", AUDIO, "PortAudio", "https://www.portaudio.com/",
     "Copyright © 1999-2011 Ross Bencina and Phil Burk", "MIT",
     ["$BUILD/_deps/portaudio/licenses/LICENSE.txt"]),
    ("portmixer", AUDIO, "PortMixer", "https://github.com/audacity/audacity/tree/master/lib-src/portmixer",
     "Copyright © 2002, 2006 Dominic Mazzoni and Leland Lucius", "MIT",
     [("thirdparty/portmixer/src/px_mixer.c", 36)]),
    ("libsndfile", AUDIO, "libsndfile", "https://libsndfile.github.io/libsndfile/",
     "Copyright © 1999-2024 Erik de Castro Lopo and contributors", "LGPL-2.1-or-later",
     ["$BUILD/_deps/libsndfile/licenses/COPYING"]),
    ("flac", AUDIO, "FLAC", "https://xiph.org/flac/",
     "Copyright © 2000-2009 Josh Coalson, 2011-2023 Xiph.Org Foundation", "BSD-3-Clause",
     ["$BUILD/_deps/flac/licenses/COPYING.Xiph"]),
    ("ogg", AUDIO, "Ogg", "https://xiph.org/ogg/",
     "Copyright © 2002 Xiph.Org Foundation", "BSD-3-Clause", ["$BUILD/_deps/ogg/licenses/COPYING"]),
    ("vorbis", AUDIO, "Vorbis", "https://xiph.org/vorbis/",
     "Copyright © 2002-2020 Xiph.Org Foundation", "BSD-3-Clause", ["$BUILD/_deps/vorbis/licenses/COPYING"]),
    ("opus", AUDIO, "Opus", "https://opus-codec.org/",
     "Copyright © 2001-2023 Xiph.Org, Skype Limited, Octasic and others", "BSD-3-Clause",
     ["$BUILD/_deps/opus/licenses/COPYING"]),
    ("opusfile", AUDIO, "opusfile", "https://opus-codec.org/",
     "Copyright © 1994-2013 Xiph.Org Foundation and contributors", "BSD-3-Clause",
     ["$BUILD/_deps/opusfile/licenses/COPYING"]),
    ("wavpack", AUDIO, "WavPack", "https://www.wavpack.com/",
     "Copyright © 1998-2024 David Bryant", "BSD-3-Clause", ["$BUILD/_deps/wavpack/licenses/COPYING"]),
    ("lame", AUDIO, "LAME", "https://lame.sourceforge.io/",
     "Copyright © 1999-2017 The LAME Project", "LGPL-2.0-or-later", ["$BUILD/_deps/lame/licenses/COPYING"]),
    ("mpg123", AUDIO, "mpg123", "https://www.mpg123.de/",
     "Copyright © 1995-2024 by Michael Hipp and others", "LGPL-2.1-only", ["$BUILD/_deps/mpg123/licenses/COPYING"]),
    ("twolame", AUDIO, "TwoLAME", "https://www.twolame.org/",
     "Copyright © 2001-2019 Nicholas J. Humfrey, Mike Cheng and others", "LGPL-2.1-or-later",
     ["$BUILD/_deps/twolame/twolame/COPYING"]),
    ("taglib", AUDIO, "TagLib", "https://taglib.org/",
     "Copyright © 2002-2024 Scott Wheeler and contributors", "LGPL-2.1-only OR MPL-1.1",
     ["$BUILD/_deps/taglib/licenses/COPYING.LGPL", "$BUILD/_deps/taglib/licenses/COPYING.MPL"]),
    ("soxr", AUDIO, "libsoxr", "https://sourceforge.net/projects/soxr/",
     "Copyright © 2007-2018 robs@users.sourceforge.net", "LGPL-2.1-or-later",
     ["$BUILD/_deps/soxr/soxr/COPYING.LGPL"]),
    ("soundtouch", AUDIO, "SoundTouch", "https://www.surina.net/soundtouch/",
     "Copyright © Olli Parviainen", "LGPL-2.1-or-later", ["$BUILD/_deps/soundtouch/soundtouch/COPYING.TXT"]),
    ("sbsms", AUDIO, "SBSMS", "https://github.com/claytonotey/libsbsms",
     "Copyright © Clayton Otey", "GPL-2.0-or-later", ["$BUILD/_deps/sbsms/sbsms/COPYING"]),
    ("nyquist", AUDIO, "Nyquist", "https://www.cs.cmu.edu/~music/nyquist/",
     "Copyright © Roger B. Dannenberg; XLISP Copyright © David Michael Betz", "Nyquist / XLISP license",
     ["$BUILD/_deps/nyquist/nyquist/nyquist/license.txt"]),
    ("pffft", AUDIO, "PFFFT", "https://bitbucket.org/jpommier/pffft",
     "Copyright © 2013 Julien Pommier, 2020 Hayati Ayguen; FFTPACK © 2004 UCAR", "BSD-like (FFTPACK)",
     [("$BUILD/_deps/pffft/pffft/pffft.c", 59)]),
    ("loop-tempo-estimator", AUDIO, "loop-tempo-estimator", "https://github.com/audacity/loop-tempo-estimator",
     "Copyright © Muse Group", "GPL-3.0", ["$BUILD/_deps/loop-tempo-estimator/loop-tempo-estimator/LICENSE"]),
    ("tft", AUDIO, "tft (time/frequency transforms)", "https://github.com/audacity/tft",
     "Copyright © Muse Group", "GPL-3.0", ["$BUILD/_deps/tft/tft/LICENSE"]),
    ("vst3sdk", AUDIO, "VST 3 SDK", "https://github.com/steinbergmedia/vst3sdk",
     "VST is a registered trademark of Steinberg Media Technologies GmbH. Copyright © Steinberg Media Technologies GmbH",
     "GPL-3.0 (dual-licensed)", ["$BUILD/_deps/vst3sdk/pluginterfaces/LICENSE.txt"]),

    ("qt", FRAMEWORK, "Qt 6", "https://www.qt.io/",
     "Copyright © The Qt Company Ltd. and other contributors", "LGPL-3.0-only", ["$LGPL3"]),
    ("kddockwidgets", FRAMEWORK, "KDDockWidgets", "https://github.com/KDAB/KDDockWidgets",
     "Copyright © 2018-2021 Klarälvdalens Datakonsult AB", "GPL-2.0-only OR GPL-3.0-only",
     ["muse/framework/dockwindow/thirdparty/KDDockWidgets/LICENSE.txt"]),
    ("wxwidgets", FRAMEWORK, "wxWidgets (wxBase)", "https://wxwidgets.org/",
     "Copyright © 1998-2005 Julian Smart, Robert Roebling et al", "wxWindows Library Licence 3.1",
     ["$BUILD/_deps/wxwidgets/licenses/licence.txt"]),
    ("kors", FRAMEWORK, "kors (async, logger, modularity, msgpack, profiler, rpcqueue)",
     "https://github.com/igorkorsukov", "Copyright © 2020-2025 Igor Korsukov", "MIT",
     ["muse/framework/global/thirdparty/kors_async/LICENSE"]),
    ("sg14", FRAMEWORK, "SG14 inplace_function", "https://github.com/WG21-SG14/SG14",
     "Copyright © the SG14 contributors", "BSL-1.0",
     [("muse/framework/global/thirdparty/sg14/inplace_function.h", 28)]),
    ("sqlite", FRAMEWORK, "SQLite", "https://sqlite.org/", "Public domain", "Public domain",
     [("$BUILD/_deps/sqlite/sqlite/sqlite3.h", 11)]),
    ("pugixml", FRAMEWORK, "pugixml", "https://pugixml.org/",
     "Copyright © 2006-2025 Arseny Kapoulkine", "MIT", ["$BUILD/_deps/pugixml/pugixml/LICENSE.md"]),
    ("rapidjson", FRAMEWORK, "RapidJSON", "https://rapidjson.org/",
     "Copyright © 2015 THL A29 Limited, a Tencent company, and Milo Yip", "MIT",
     ["$BUILD/_deps/rapidjson/rapidjson/license.txt"]),
    ("picojson", FRAMEWORK, "picojson", "https://github.com/kazuho/picojson",
     "Copyright © 2009-2010 Cybozu Labs, 2011-2014 Kazuho Oku", "BSD-2-Clause",
     ["$BUILD/_deps/picojson/picojson/LICENSE"]),
    ("utfcpp", FRAMEWORK, "UTF8-CPP", "https://github.com/nemtrif/utfcpp",
     "Copyright © 2006 Nemanja Trifunovic", "BSL-1.0", ["$BUILD/_deps/utfcpp/utfcpp/LICENSE"]),
    ("expat", FRAMEWORK, "Expat", "https://libexpat.github.io/",
     "Copyright © 1998-2000 Thai Open Source Software Center Ltd and Clark Cooper, 2001-2025 Expat maintainers",
     "MIT", ["$BUILD/_deps/expat/licenses/COPYING"]),
    ("zlib", FRAMEWORK, "zlib", "https://zlib.net/",
     "Copyright © 1995-2022 Jean-loup Gailly and Mark Adler", "Zlib", ["$BUILD/_deps/zlib/licenses/LICENSE"]),
    ("libpng", FRAMEWORK, "libpng", "http://www.libpng.org/pub/png/libpng.html",
     "Copyright © 1995-2025 The PNG Reference Library Authors", "libpng-2.0",
     ["$BUILD/_deps/libpng/licenses/LICENSE"]),
    ("freetype", FRAMEWORK, "FreeType", "https://freetype.org/",
     "Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.",
     "FTL OR GPL-2.0-or-later", ["$BUILD/_deps/freetype/licenses/LICENSE.TXT"]),
    ("tabler-icons", FRAMEWORK, "Tabler Icons", "https://tabler.io/icons",
     "Copyright © 2020-2026 Paweł Kuna", "MIT", ["src/projectscene/resources/toolbaricons/LICENSE"]),
    ("harfbuzz", FRAMEWORK, "HarfBuzz", "https://harfbuzz.github.io/",
     "Copyright © 2010-2022 Google, Inc., 2015-2020 Ebrahim Byagowi and others", "MIT-Modern-Variant",
     ["$BUILD/_deps/harfbuzz/licenses/COPYING"]),
]


def read_text(path, max_lines=None):
    with open(path, "rb") as f:
        text = f.read().decode("utf-8", errors="replace").replace("\r\n", "\n")
    if max_lines is not None:
        text = "\n".join(text.split("\n")[:max_lines]) + "\n"
    return text


def resolve(path, build):
    return (path.replace("$BUILD", build).replace("$MIXXX", MIXXX_SRC)
            .replace("$OPENKEYSCAN", OPENKEYSCAN_SRC).replace("$LGPL3", LGPL3_TEXT))


def mixxx_credits():
    src = read_text(os.path.join(MIXXX_SRC, "src", "dialog", "dlgabout.cpp"))
    lists = {}
    for name, body in re.findall(r"QStringList (\w+);\s*\1\b((?:\s*(?://[^\n]*)?\s*<<\s*\"[^\"]*\")+)\s*;", src, re.S):
        body = re.sub(r"//[^\n]*", "", body)
        lists[name] = [html.unescape(n) for n in re.findall(r"<<\s*\"([^\"]*)\"", body)]
    sections = [
        ("Mixxx Development Team", "thisReleaseDevelopers"),
        ("Mixxx contributors", "recentContributors"),
        ("Mixxx past developers", "pastDevelopers"),
        ("Mixxx past contributors", "pastContributors"),
        ("Mixxx special thanks", "specialThanks"),
    ]
    missing = [key for _, key in sections if not lists.get(key)]
    if missing:
        sys.exit("dlgabout.cpp: missing credit lists " + ", ".join(missing))
    return [{"title": title, "names": lists[key]} for title, key in sections]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default=os.path.join(ROOT, "build"))
    build = os.path.abspath(parser.parse_args().build)

    lic_dir = os.path.join(OUT, "licenses")
    os.makedirs(lic_dir, exist_ok=True)
    for old in os.listdir(lic_dir):
        os.remove(os.path.join(lic_dir, old))

    by_hash = {}
    components = []
    for cid, group, name, url, copyright_, spdx, sources in COMPONENTS:
        files = []
        for i, src in enumerate(sources):
            max_lines = None
            if isinstance(src, tuple):
                src, max_lines = src
            path = src if os.path.isabs(resolve(src, build)) else os.path.join(ROOT, resolve(src, build))
            path = resolve(path, build)
            if not os.path.isfile(path):
                sys.exit(f"{cid}: license file not found: {path}")
            text = read_text(path, max_lines)
            digest = hashlib.sha1(text.encode("utf-8")).hexdigest()
            if digest not in by_hash:
                fname = f"{cid}.txt" if i == 0 else f"{cid}-{i + 1}.txt"
                with open(os.path.join(lic_dir, fname), "w", encoding="utf-8") as f:
                    f.write(text)
                by_hash[digest] = fname
            files.append(by_hash[digest])
        components.append({"id": cid, "group": group, "name": name, "url": url,
                           "copyright": copyright_, "license": spdx, "licenseFiles": files})

    with open(os.path.join(OUT, "components.json"), "w", encoding="utf-8") as f:
        json.dump(components, f, ensure_ascii=False, indent=1)
        f.write("\n")
    with open(os.path.join(OUT, "mixxx_credits.json"), "w", encoding="utf-8") as f:
        json.dump(mixxx_credits(), f, ensure_ascii=False, indent=1)
        f.write("\n")
    print(f"{len(components)} components, {len(by_hash)} license files -> {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
