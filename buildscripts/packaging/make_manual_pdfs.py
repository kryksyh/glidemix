#!/usr/bin/env python3
"""GlideMix: the tester manuals (docs/manual/*.md) as PDFs for the test kits.

    uv run --with markdown buildscripts/packaging/make_manual_pdfs.py <out-dir>

Markdown -> HTML (python-markdown) -> PDF (headless Google Chrome).
"""
import pathlib
import subprocess
import sys
import tempfile

import markdown

ROOT = pathlib.Path(__file__).resolve().parents[2]
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
MANUALS = {
    "quick-start.md": "GlideMix Quick Start.pdf",
    "preferences.md": "GlideMix Preferences Manual.pdf",
}
CSS = """
@page { size: A4; margin: 18mm 16mm; }
body { font: 10.5pt/1.5 -apple-system, "Segoe UI", Helvetica, Arial, sans-serif; color: #1d1d1f; }
h1 { font-size: 22pt; color: #6d4aff; margin: 0 0 6pt; }
h2 { font-size: 15pt; margin: 18pt 0 4pt; border-bottom: 1px solid #ddd; padding-bottom: 2pt; }
h3 { font-size: 11.5pt; margin: 12pt 0 2pt; }
code { font: 9.5pt Menlo, Consolas, monospace; background: #f2f2f5; padding: 0 3px; border-radius: 3px; }
hr { border: 0; border-top: 1px solid #e5e5ea; margin: 14pt 0; }
li { margin: 2pt 0; }
h2, h3 { break-after: avoid; }
"""


def main():
    out = pathlib.Path(sys.argv[1]).expanduser()
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for src, pdf in MANUALS.items():
            text = (ROOT / "docs" / "manual" / src).read_text(encoding="utf-8")
            body = markdown.markdown(text, extensions=["extra", "sane_lists"])
            html = pathlib.Path(tmp) / (src + ".html")
            html.write_text(f"<!doctype html><meta charset=utf-8><style>{CSS}</style>{body}", encoding="utf-8")
            subprocess.run([CHROME, "--headless", "--disable-gpu", "--no-pdf-header-footer",
                            f"--print-to-pdf={out / pdf}", html.as_uri()],
                           check=True, capture_output=True)
            print(out / pdf)


if __name__ == "__main__":
    main()
