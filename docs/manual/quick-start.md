# GlideMix Quick Start

*GlideMix Alpha v1.0.0 Early Preview*

GlideMix is a timeline editor for DJ mixes: put tracks on a timeline, let them
lock to the beat, shape the transitions, and export or perform the mix. This
is an **Alpha Early Preview**: things will break, and your reports are what fixes them.

---

## 1. Install

### macOS (Apple Silicon Macs: M1 or newer)

1. Open `GlideMix-Alpha-v1.0.0-Early-Preview.dmg` and drag **GlideMix** into **Applications**.
2. Double-click GlideMix. The preview is not signed with an Apple Developer ID
   yet, so macOS says it cannot verify it: click **Done**.
3. Open **System Settings → Privacy & Security**, scroll down to
   "GlideMix was blocked…" and click **Open Anyway**. Confirm with your
   password. From then on it opens normally.
4. macOS may ask whether GlideMix may use the microphone (only for recording)
   and your Music, Documents or Desktop folders. Allow the folders your music
   is in.

### Windows 10 / 11 (64-bit)

1. Run `GlideMix-Alpha-v1.0.0-Early-Preview-x64-setup.exe`. It installs for your user only, no
   administrator password needed.
2. The preview is not signed yet. If you see **"Windows protected your PC"**,
   click **More info → Run anyway**.
3. **Smart App Control:** on Windows 11 PCs where Smart App Control is on,
   unsigned apps are blocked outright, with no "Run anyway". Check in
   **Windows Security → App & browser control → Smart App Control**. If it is
   on, wait for a signed GlideMix build: turning Smart App Control off cannot
   be undone without reinstalling Windows.
4. **Faster analysis on Windows:** Windows Defender scans every song GlideMix
   reads and can use as much CPU as the analysis itself. In **Windows Security
   → Virus & threat protection → Manage settings → Exclusions**, add the
   GlideMix folder (`%LOCALAPPDATA%\Programs\GlideMix`) and your music
   folders. On laptops, choose the **High performance** power plan. Close
   heavy background helpers (mouse or RGB software) while analysing.
5. **Audio:** GlideMix uses WASAPI with a 100 ms buffer on Windows. If you
   still hear crackles, raise the buffer in **Preferences → Audio settings**.
   The library preview plays on Windows' default output.
6. **Preferences** are in the **Settings** menu (beside **Tools**) and in **File**.

### Uninstall

- macOS: drag GlideMix from Applications to the Bin. Your library lives in
  `~/Library/Application Support/Glidemix`; delete that too for a clean start.
- Windows: **Settings → Apps → Installed apps → GlideMix → Uninstall**. Your
  library lives in `%APPDATA%\Glidemix`.

---

## 2. First start

**Getting started** asks three things: the theme (it follows your system's
light or dark mode unless you pick one), how clips look, and the workspace
layout. Everything can be changed later in **Preferences**. Then pick your
sound card and outputs in **Audio setup** at the top of the window.

---

## 3. The music library

Open it with **Music library** at the top of the window.

**Adding music**
- **Music Collection → +** (or right-click it) adds a folder; every song in it
  and its subfolders is added. You can also drag folders from Finder or
  Explorer onto the library.
- **Computer** browses your disks without adding anything: **Quick Links**
  (Music, Downloads, Desktop, Documents, plus your own with **+**) and
  **Devices** (USB sticks and other drives).
- **rekordbox XML** and **Lexicon DJ**: your playlists from those apps, if you
  have them.

**Playlists**: **+** makes a playlist or a folder. Drag to reorder or to move
a playlist into a folder. Dropping a Finder/Explorer folder tree onto a
playlist folder builds the same tree of playlists.

**Analysis** finds each track's tempo, beat grid, key and phrases:
1. Pick **New** (only tracks never analysed) or **All**.
2. **Select All**, then **Analyze**. The selection is cleared as soon as
   analysis starts, so you can keep working.
3. **Stop Analysis** stops it; finished tracks keep their results.

Analysis runs in the background at low priority, so the app stays responsive.
Phrases come from the **GlideMix Phrase Detector**, which names them Intro,
Up, Down, Chorus, Outro (Verse and Bridge for non-EDM tracks).

**Preview without loading a track**: hover a track's **artwork** and click the
white play button. While it plays, the artwork shows **stop** (left) and
**next phrase** (right). Move the mouse off the artwork and the preview stops,
unless the track is selected; a selected track keeps playing. While a track
previews, click its waveform to jump to that point.

**Find tracks**: the search box, the column headers (click to sort), and the
**Key Wheel** (click keys to show only tracks in those keys; **Reset** shows
all again).

**Smart views**: **Analyze** lists tracks not analysed yet; **History**, the
tracks you have loaded into a project; **Recordings**, your exported mixes.

---

## 4. Making a mix

1. Drag tracks from the library onto the timeline. Each track lands on the
   beat grid with its phrases shown on the clip.
2. **SYNC** keeps tracks locked to the grid (the Snap value, or a bar when
   Snap is off).
3. **MIX NOW** pulls the next track forward so it mixes in at the next bar or
   phrase, with the transition chosen in **Preferences → Transitions**.
4. Overlap two tracks to make a transition. Transitions shape **Volume**,
   **Bass**, **Mids** and **Highs**, and can add DJ effects.
5. **KEYLOCK** on: tempo changes keep the pitch. Off: it plays like a
   turntable.
6. **TEMPO** shows the master tempo lane for tempo changes across the mix.
7. A track more than 8% faster or slower than the mix (say 122 BPM into a
   140 BPM mix) plays at its own tempo: **Follow tracks** turns on and the
   tempo line glides to it across the overlap, with a point where each change
   starts and ends. Drag a point to reshape it; drawing on the line turns
   **Follow tracks** off and keeps your line.

**Perform** plays the mix to the audience on outputs 1-2 (red playhead),
while **Play** becomes your headphone preview on outputs 3-4. Set the outputs
in **Audio setup**.

**Stems**: right-click a clip → **Separate stems** splits it into drums, bass,
vocals and other, in sync under the original. The first time, install the
stems engine from **Preferences → Stems** (a free download of about 1 GB).
On Windows the stems engine is not tested yet.

**Export**: **File → Export** writes your mix as an audio file.

---

## 5. Preferences

Twelve pages, in this order: General, Appearance, Audio settings,
Playback/Recording, Music library, Transitions, Effects, Live broadcasting,
Waveforms, Spectral display, Audio editing, Shortcuts. Every control is
explained in the **GlideMix Preferences Manual** that comes with this kit.

---

## 6. Reporting problems

Tell us what you did, what you expected, and what happened, with a screenshot
if you can, and attach the newest log file:

- macOS: `~/Library/Application Support/Glidemix/Glidemix/logs`
  (in Finder: **Go → Go to Folder…** and paste the path)
- Windows: `%LOCALAPPDATA%\Glidemix\Glidemix\logs`
  (paste it into the Explorer address bar)
