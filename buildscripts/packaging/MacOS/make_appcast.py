#!/usr/bin/env python3
"""GlideMix: writes appcast.xml, the Sparkle feed Mac updater apps (Updatest,
Latest, MacUpdater...) read through the app's SUFeedURL, from the GitHub
releases of cristitm76/glidemix-releases.

    buildscripts/packaging/MacOS/make_appcast.py > appcast.xml

Then commit appcast.xml to the root of glidemix-releases (see release-process).
Needs the gh CLI, logged in. Each DMG found in dist/ is signed with the
"glidemix" Sparkle key (keychain): Sparkle installs only signed updates.
"""
import html
from datetime import datetime
from email.utils import format_datetime
import json
import os
import subprocess
import sys

REPO = "cristitm76/glidemix-releases"
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SIGN = os.path.join(ROOT, "thirdparty-mac", "sparkle", "bin", "sign_update")


def signature(dmg_name: str) -> str:
    """sparkle:edSignature="..." length="..." for dist/<dmg_name>, or "" if absent."""
    path = os.path.join(ROOT, "dist", dmg_name)
    if not (os.path.exists(path) and os.path.exists(SIGN)):
        return ""
    return subprocess.check_output([SIGN, "--account", "glidemix", path], text=True).strip()
MIN_MACOS = "13.0"


def bold(text: str) -> str:
    """Escaped text with each **pair** made <b>bold</b>."""
    text = html.escape(text)
    while text.count("**") >= 2:
        text = text.replace("**", "<b>", 1).replace("**", "</b>", 1)
    return text


def notes_html(markdown: str) -> str:
    """The release notes, as simple HTML: **bold** headings and - lists.
    A heading is a line that starts bold; whatever follows the bold part (an
    emoji, "(Mac and Windows)") stays on it -- before, only a line ending in
    ** counted, and the rest showed their ** marks in Sparkle's window."""
    out, in_list = [], False
    for raw in markdown.splitlines():
        line = raw.strip()
        if line.startswith("- "):
            if not in_list:
                out.append("<ul>")
                in_list = True
            out.append(f"<li>{bold(line[2:])}</li>")
            continue
        if in_list:
            out.append("</ul>")
            in_list = False
        if line.startswith("**") and line.count("**") >= 2:
            out.append(f"<h3>{html.escape(line).replace('**', '')}</h3>")
        elif line:
            out.append(f"<p>{bold(line)}</p>")
    if in_list:
        out.append("</ul>")
    return "\n".join(out)


def main() -> None:
    releases = json.loads(subprocess.check_output(
        ["gh", "api", f"repos/{REPO}/releases", "--paginate"]))
    items = []
    for rel in releases:
        if rel.get("draft") or rel.get("prerelease"):
            continue
        dmg = next((a for a in rel["assets"] if a["name"].endswith(".dmg")), None)
        if not dmg:
            continue
        version = rel["tag_name"].lstrip("v")
        items.append(f"""    <item>
      <title>GlideMix {version}</title>
      <pubDate>{format_datetime(datetime.fromisoformat(rel["published_at"].replace("Z", "+00:00")))}</pubDate>
      <link>{rel["html_url"]}</link>
      <sparkle:version>{version}</sparkle:version>
      <sparkle:shortVersionString>{version}</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>{MIN_MACOS}</sparkle:minimumSystemVersion>
      <description><![CDATA[{notes_html(rel.get("body") or "")}]]></description>
      <enclosure url="{dmg["browser_download_url"]}" {signature(dmg["name"]) or f'length="{dmg["size"]}"'} type="application/octet-stream"/>
    </item>""")
    sys.stdout.write(f"""<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>GlideMix</title>
    <link>https://github.com/{REPO}</link>
    <description>GlideMix updates</description>
    <language>en</language>
{chr(10).join(items)}
  </channel>
</rss>
""")


if __name__ == "__main__":
    main()
