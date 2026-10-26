# Preferences

One section per page of Preferences, in the order they appear in the sidebar:
what each control does, and anything worth knowing before you touch it.

---

## General

Language and where the app keeps its working files.

### Temporary files location

Where unsaved projects and scratch data live while you work. Change it with
the folder button beside the path.

A change does not take effect until the app is restarted, and the app says so
when you pick a new folder. Point it at a fast internal disk: everything you
record and every edit you have not saved yet is written here first.

### Free space

How much room is left on the disk holding the temporary files folder. It
follows that folder, so it changes when you change the folder above.

### FFmpeg library

GlideMix uses FFmpeg to read and write the formats its own decoders do
not cover. The version line shows what it found; the field below shows the
library it is using.

**Locate existing installation** picks a different FFmpeg. If one has already
been found it asks before replacing it, so pressing the button is safe if you
only wanted to look. Point it at `libavformat` — on macOS that is usually
somewhere like `/opt/homebrew/opt/ffmpeg/lib/libavformat.*.dylib`.

If the field reads "FFmpeg library not found", install FFmpeg and use the
button to point at it. Nothing else on this page depends on it.

### Not shown

Some sections hide themselves when they have nothing to offer, so this page
may be shorter than the list below:

- **Language** appears once a translation is actually installed. Right now
  none is — the app ships no compiled translations — so a language list would
  be 68 entries that all do nothing.
- **Automatic updates**, **crash reports** and **usage info** appear only in
  builds wired to an update server and a reporting service. This one is not,
  and sends nothing anywhere.

---

## Appearance

How the app looks: theme, accent colour, the look of clips, and the UI font.

### Theme

**Light** or **Dark**, with a preview of each. **Follow system theme** hands the
choice to macOS and switches with it; picking Light or Dark yourself turns that
off, so you do not have to untick it first.

**Enable high-contrast** swaps to a pair of much starker themes (White and
Black) and adds the **UI colors** section below.

### UI colors

Only shown with high-contrast on. Three colours you can set by hand:

- **Accent color** — selections, active buttons, the Perform and Cue states
- **Text and icons**
- **Border color**

These override the chosen high-contrast theme, so a change here sticks until
you set it back.

### Accent color

Ninety-six shades, in twenty-four columns of four. Whatever you pick runs
through the whole app, including the DJ controls: the Perform and Cue buttons,
active toolbar toggles, sliders and selections all follow it.

A few colours deliberately do **not** follow it, because they mean something
specific and have to stay recognisable: the red of ON AIR and of a clipping
meter, and the marker colours in the timeline.

### Clip style

How a clip is drawn in the timeline.

- **Colorful** — the clip carries its own colour, the one set per track or per
  library entry, and the waveform sits inside it.
- **Classic** — the plain Audacity look: a neutral clip body with a blue
  waveform.

This is separate from the waveform settings under **Waveforms** and **Colors**.

### Font

**Font face** picks the UI typeface; the system font is the default and the
safe choice.

**Body text size** changes the size of text across the whole app. It applies at
once — no restart — so nudge it a point at a time and watch. Larger sizes make
panels taller, which costs you timeline height on a laptop screen.

---

## Audio settings

Everything about where sound comes in and goes out. Changes here wait for
**Apply** or **OK** — **Cancel** undoes the lot, including the controls that
look like they took effect straight away.

### Inputs and outputs

- **Host** — the audio system. Core Audio on macOS; there is nothing else to
  pick.
- **Playback device** — where the mix goes. This is the audience output: the
  one Perform plays to.
- **Recording device** and **Recording channels** — the input, and whether it
  is recorded as mono or stereo.

### Headphone cue

While Perform is playing the mix to the room, Play is your headphone cue, and
this is where that cue goes.

- **Outputs 3-4 of the playback device** — the usual DJ setup. Needs an
  interface with four or more outputs: the room hears 1-2, you hear 3-4, one
  device, no drift.
- **A separate device** — the Mac's own speakers or headphone jack, a second
  interface, a USB dongle. Use this when the interface has only two outputs.

The same setting is on the menu attached to the **Perform** button, which is
where you want it mid-set, without opening a dialog.

### Sample rate

**Default sample rate** and **Default sample format** apply to newly created
tracks. Recording into a track that already exists uses that track's own rate
and format instead, so changing these will not disturb a project you are part
way through.

44100 Hz and 32-bit float suit a DJ set: 44100 matches the files, and 32-bit
float leaves headroom for mixing without clipping in the editor.

### Buffer and latency

**Buffer length** trades latency for safety. Shorter is more responsive and
more likely to crackle on a busy machine; longer is steadier. 20 ms is a
sensible place to start — lengthen it if you hear dropouts.

**Latency compensation** lines recorded audio up with what you were listening
to, so an overdub lands where you played it. Tick **Automatic** to let the app
work it out, or untick it and set the figure yourself. It does not affect
playback or the stream.

### Meter dB range

How far down the meters read — the scale on the header meters and on the track
meters. −60 dB is the default and suits most work. A shallower range like
−36 dB spreads the loud end out, which helps when you are editing something
already near full scale.

The broadcast window's own meter ignores this: it always runs −60 to +12,
because the stream gain can be pushed past 0 and you need to see by how much.

### Not shown

**ASIO** appears on Windows only.

---

## Playback/Recording

Playback quality, and how recording behaves.

### Playback performance

**Playback quality** sets the sample rate converter used when a track's rate
does not match the device's. Best quality is the default and costs very little
on a modern machine; drop it only if an old laptop is struggling.

**Dithering** adds a tiny amount of noise when audio is reduced to a smaller
bit depth, to stop quiet passages developing a gritty edge. None is the
default and is right for a DJ set, where everything stays in 32-bit float
until it leaves the app.

### Recording behaviour

**Turn on input monitoring** lets you hear the input while recording. Leave it
off on a laptop unless you are wearing headphones, or the mic will pick up the
speakers and howl.

**Show mic metering when not recording** keeps the input meter live so you can
set a level before you commit.

### Recording lead-in time

**Lead-in time before recording starts** is a countdown — recording begins
after this many seconds, so you are not scrambling for the first beat.

**Crossfade** is the short blend applied where a new recording meets audio
that was already there, which stops a click at the join.

---

## Music library

Where the library comes from, how its table looks and searches, and where a
dropped file's tempo comes from.

### Music Directories

Folders the library scans for tracks. **Add** a folder, **Relink** one that has
moved, **Remove** one you no longer want scanned.

**Rescan directories on start-up** picks up new files each time the app opens;
**Show scan summary dialog** reports what the scan found.

"No music directories added yet" does not mean the library is empty. Tracks
imported from rekordbox or Lexicon, or added some other way, are in the library
without a watched folder behind them.

### Track Metadata Synchronization

**Synchronize library track metadata from/to file tags.** On, edits you make in
the library (title, artist, key…) are written into the files themselves, and
changes made to the files elsewhere come back in. Off, the library keeps its
own copy and the files are left alone.

### Track Table View

- **Edit metadata after clicking selected track** — click a selected row again
  to edit it in place.
- **Track Double-Click Action** — what a double-click does: load the track into
  the project, add it to the Auto DJ queue (top or bottom), or nothing.
- **Library Row Height**, **Library Font**, **Library Font Size** — how dense
  the table is. Font size 0 uses the app's own size.
- **BPM display precision** — decimal places shown for tempo.
- **Key notation** — how keys are written everywhere: the Key column, the key
  wheel, and the clip menu's Match key and Transpose. **Lancelot** (8A, the
  Camelot wheel), **OpenKey** (1m), **Traditional** (Am), or Lancelot or
  OpenKey with the traditional name after it (8A/Am). Search takes a key in any
  of them: `key:8A`, `key:1m` and `key:Am` all find A minor.
- **Grey out played tracks** — dims tracks you have already played.

### Waveform Overview

The small waveform in the library's Waveform column. **Waveform overview type**
chooses how it is coloured; **Show minute markers** puts a tick every minute.
This is the library table only; the timeline's look is on **Waveforms**.

### Track Search

- **Search-as-you-type timeout** — how long the search waits after you stop
  typing before it runs.
- **Fuzzy BPM range** — how far a `~bpm:` search reaches either side of the
  tempo you type. A plain `bpm:` search matches the tempo exactly.
- Search completions and search-history shortcuts (Up/Down in the search box).

### Settings Directory

**Open Settings Folder** shows the folder holding the library database, other
configuration, logs and track analysis. Only edit those files with the app
closed.

### Audio files that aren't in your library

A track's tempo comes from one of two places, and this section is about the
second.

- **Library tracks** always use the BPM and beat grid the library already has
  — from its own analysis, or imported from rekordbox or Lexicon. Drag one onto
  the timeline or double-click it and it is stretched to the project tempo and
  snapped to the bar. Nothing on this page affects that.
- **Any other audio file** — a sample, a loop, a recording, something from
  Finder — has no tempo until one is detected. That is what these settings
  control.

**Detect their tempo:**

- **Always** — every such file is analysed.
- **Depending on workspace** — only in the workspaces ticked below. Music and DJ
  are on by default.
- **Never** — files come in at their own speed and are not stretched.

**Ask before stretching it to the project tempo.** On, the app tells you the
tempo it found and asks before stretching. Off, it stretches straight away.

The very first file into an empty project is different: it sets the project
tempo instead of being stretched to it.

---

## Transitions

What Mix Now does, and the transitions you can put on an overlap.

### Mix Now

**Mix Now** (on the toolbar) pulls the next track forward so it mixes into the
one that is playing.

- **Lands on** — where the next track's Intro anchor goes: **the next bar**, or
  **the next phrase**, matching phrase to phrase so a drop lands on a drop.
- **Transition** — what Mix Now applies as it pulls the track in. Pick a
  built-in, one of your own, or none to only pull the track without fading.

### Transition controls

- **The transition pill opens** — what the cog on a BEATMATCH / CROSSFADE pill
  opens: **the transition window** (preset, Song out / Song in, EQ knobs,
  volume, Color, lanes, Add effect) or **the DJ mixer**, a 2-channel mixer laid
  out like a Pioneer DJM (CH 1 the song going out, CH 2 the song coming in:
  HI / MID / LOW with KILL, COLOR, channel faders, a crossfader with Smooth /
  Constant / Cut curves, BEAT FX and REC). Both work on the same lanes.

### Transitions

The list shows GlideMix's built-in transitions and, marked **(yours)**, the
ones you have made. Built-ins cannot be changed; **Duplicate** one to make your
own version.

A transition is a length and four lanes, **Volume**, **Bass**, **Mids** and **Highs** (the three bands of the transition EQ, split at 250 Hz and 2.5 kHz). Each lane has:

- **Curve** — **Crossfade** (one falls as the other rises), **Fade in** (only
  the incoming track rises), **Fade out** (only the outgoing one falls), or
  **Swap** (they trade places at one moment).
- **Strength** — how far the curve goes.
- **Crossing point** — where in the transition the two tracks meet.
- **Leave out** — for a bass swap, how many beats the incoming bass is held
  back after the crossing, so that for a moment neither track has the low end.
  That gap is what makes a bass swap sound clean rather than muddy.

Three **Effects** lanes add DJ effects to the transition:

- **Filter** — the outgoing track thins out (high-pass) or goes dull
  (low-pass) across the transition, or the incoming track opens up from thin
  or dull.
- **Noise** — a white-noise **Riser** over the outgoing track up to the drop,
  or a **Downsweep** that falls away over the incoming track after it.
- **Effect out** — the outgoing track's last bar goes into an **Echo** or a
  **Reverb** that rings on after the track is gone.

Each lane is played by the song's own Filter, White Noise, Echo or Reverb DJ
effect, which is added to its track if the track doesn't have one. What the
lane writes is ordinary effect automation, so it can be edited afterwards.
Applying another transition or **Remove transition** puts the effects back
where they do nothing. Built-ins using them: Filter out, Filter in, Muffled
out, Riser + filter, Echo out, Reverb out, Drop + downsweep. Three-band swap
uses the EQ lanes the way DJs do: highs cross over the whole length, the bass
swaps in the middle and the mids a phrase later.

The **Preview** draws exactly the lines that will be written onto the clips.
Solid lines are volume, dashed are bass; the highlighted line is the outgoing
track, white the incoming one.

**Save** and **Delete** take effect when you press them. Your transitions are
kept in their own file, not with the other settings, so the dialog's
**Cancel** does not undo them. **Use for Mix Now** makes the selected one the
Mix Now transition.

### Using a transition on the timeline

Where two tracks overlap, a pill appears on the overlap — **BEATMATCH**, or
**CROSSFADE** once a transition is on it. Click it for a menu of **Crossfade**
(a plain fade that fits the overlap), the built-in transitions, then yours.
The one in use is ticked. **Remove transition** takes it off.

---

## Effects

Everything about the effects you put on a track.


GlideMix adds six mixer beat effects of its own to the Mixxx ones, all
beat-synced and automatable: **Ping Pong** (an echo bouncing left and
right), **Dub Echo** (repeats that get darker and dirtier), **Roll** (loops a
1/16 to 1 beat slice while on), **Trans** (a beat gate), **Sweep** (a
band-pass filter swept in time) and **Tape Stop** (slows the track to a stop;
drawn fast it is a vinyl brake). The transition panel's **+ ADD FX** can put
a vinyl brake, a tape stop or a beat roll at the end of a transition.

### Effect Chain Presets

A chain preset is a saved stack of effects. Save a track's effects as one from
the track's **Add effect → Chain presets** menu, and load one onto a track from
the same menu. Here you manage them: drag to reorder, **Import** or **Export**
a preset file, **Rename**, **Delete**. Pick a preset to see the effects in it.

### Visible Effects

Which effects appear in a track's **Add effect** menu, and in what order. Drag
an effect across, or use the arrow buttons, to hide or show it; drag within a
list to reorder. The list holds every effect that can run live on a track,
Audacity's own and the DJ effects together. Where both have one with the same
name, the DJ one is called **DJ Reverb** / **DJ Compressor**, as in the menu.
Pick an effect to see who made it under **Effect Info**.

### Effect load behavior

When you replace a DJ effect on a track with another DJ effect: **Keep
metaknob position** carries its main knob's setting over to the new effect,
**Reset metaknob to effect default** starts the new one fresh.

### DJ EQ crossover frequencies

Where the bands split in the DJ EQ and isolator effects (Bessel, Linkwitz-Riley,
Biquad): below **Low EQ** is the low band, above **High EQ** the high band, the
rest is mid. Defaults 250 Hz and 2.5 kHz. Changes are heard straight away. Low
always stays below High.

### Effect menu organization

**Group effects** into submenus by type, or **Display effects in one flat
list** in the Add effect menu. (This and the next setting were a separate
Plugins page.)

### Custom VST3 plugins location

Extra folders to look in for VST3 plugins, besides the standard ones. Audio
Units are found by macOS itself.

---

## Stems

Right-click a track and choose **Separate stems**. The song is split into
drums (blue), bass (red), vocals (green) and other (yellow), each on its own
track right under the original and in sync with it. The beat grid, phrases and
anchors come along. The original track is muted, not removed. The whole thing
is one undo step. Separation is done by Demucs (htdemucs), which runs as a
separate helper program. If the helper is not installed the menu item is
greyed out and this page says so.

### Processing

**Run on**: Automatic uses the GPU (Metal) and falls back to the CPU if the
GPU can't be used. The GPU is many times faster: a song takes about 20-30
seconds on an M1 Max. **Quality**: Standard (htdemucs) or High (htdemucs_ft),
which is a little cleaner and about 4 times slower.

### Stems cache

Separated songs are kept, so separating the same song again is instant, even
in another project. Past the size limit (2-50 GB, 10 GB by default) the songs
used longest ago are removed. **Clear cache** empties it. The cache is in
Application Support/Glidemix/Stems.

---

## Live broadcasting

Stream your mix to an internet radio server while you play.

### Going live

1. Get the server details from your host: type, address, port, password, and
   the mount point (Icecast) or stream ID (Shoutcast 2).
2. Fill in **Server**, then **What the listeners see**.
3. Press **Connect** here, or the radio-tower button in the header. The status
   line turns to the accent colour when you are on air. A floating panel shows
   the stream level and a **Gain** fader.
4. Copy the **Listen link** and share it, or open it in a player to check.

### Server

- **Server type** — **Icecast 2** (most hosts), **Icecast 1**, **Shoutcast 2
  (DNAS 2)** or **Shoutcast 1**. Pick what your host runs.
- **Address / Port** — the port you were given. For Shoutcast, enter the
  listener port; the app connects to the source port one above it itself.
- **Mount point** — Icecast only, e.g. `/stream`.
- **Login / Password** — Icecast's source login is usually `source`.
- **Stream ID** and **DJ name** — Shoutcast 2 only. Stream ID is 1 unless the
  host says otherwise.

### What the listeners see

Stream name, description, genre and website, shown by players and directories.
**List the stream in the server's public directory** asks the server to list
it. **Send the playing track's name** updates the "now playing" title as the
mix moves from track to track.

### Audio

- **Broadcast** — **The Perform output** (what the room hears; the normal
  choice) or **The headphone cue** (for a test stream without going live in the
  room).
- **MP3 bitrate** — higher sounds better and needs more upload speed. 128 or
  192 kbps suits most hosts.

If the connection drops, the app reconnects by itself. It stops if the server
refuses the password.

---

## Waveforms

How clips are drawn on the timeline. These settings are saved **with the
workspace**, so each workspace can keep its own look — a DJ workspace in RGB on
black, an editing workspace in the classic style.

**Waveform style**

- **Classic** — the waveform drawn in the clip's own colour.
- **DJ style** — RGB spectral colouring on black: lows red, mids green, highs
  blue, the way DJ software shows it. Kicks and basslines stand out from hats
  and vocals at a glance.

**Lane background** — **Follow the waveform style** (black behind DJ style,
the theme colour behind Classic), or force **Black** or **Theme** either way.

**Draw the bar and beat lines behind the tracks** — the beat grid behind the
lanes.

The View menu has the quick versions: **View → Color waveform** and **View →
Show beat grid** flip the same settings as this page.


### HD colour waveforms

Songs from the library are drawn in colour on the timeline and in the Waveform
Overview column: bass red, mids green, highs blue, each drawn as its own layer,
so a drop comes out red or pink, a breakdown blue and vocals or leads green.
Each song is analysed once in the background the first time it is shown (a few
seconds) and kept in Application Support/Glidemix/Library/Waveforms, about
100 KB per song. Zoomed in, the waveform is measured from the audio on screen,
so it stays sharp down to single samples. The colour waveform can be turned off
with the Colour waveform button. **Stereo color waveform** (Waveforms page) draws a stereo song as two lanes, left and right, each channel analysed apart; the extra analysis happens once per song the first time it is shown.
---

## Spectral display

Settings for the spectrogram — the view that shows a track as frequency over
time instead of a waveform. Turn it on with **View → Toggle spectral view**,
or the button on the playback toolbar.

It is worth knowing for two things. Vocals and leads sit in a band you can
see, so you can find an acapella or a breakdown by eye; and a track ripped at
a low bitrate shows a hard horizontal edge near the top where the encoder threw
the high end away, which is the quickest way to spot a bad file in a library.

### Selection

**Enable spectral selection** lets a selection cover a frequency range as well
as a stretch of time — drag inside the spectrogram to pick a band.

### Scale

- **Scale** — how the frequency axis is spaced. Mel matches how hearing works
  and gives the low end more room, which is usually what you want for music.
- **Max** and **Min frequency** — the range shown. Narrowing it magnifies what
  is left, so if you are hunting for a vocal, setting 200-8000 Hz makes it far
  easier to see.

### Colors

- **Gain** and **Range** — brightness and contrast. Raise gain to bring quiet
  detail up; lower range to throw away the quietest parts and clean the
  picture up.
- **High boost** lifts the top end, which is otherwise quieter and harder to
  read.
- **Scheme** — the colour palette.

### Algorithm

The analysis itself. The defaults are good and most people never need to touch
these.

- **Window size** trades time detail against frequency detail. Smaller is
  sharper in time, so transients and beats are crisper; larger separates
  pitches better. 2048 is a reasonable middle.
- **Window type** — the shape used to taper each analysis block. Hann is the
  usual choice.
- **Zero padding factor** smooths the picture without changing the analysis.

---

## Audio editing

How editing behaves: what an effect applies to, what happens to the gap when
you delete, and where pasted audio lands.

### Effect behavior

**Apply effects to all audio when no selection is made.** On, an effect with
nothing selected treats the whole track as the selection. Off, it does
nothing until you select something.

### Behaviour when deleting a portion of a clip

- **Leave gap** — the audio goes and a silence stays. Everything after it
  keeps its position, which is what you want when a mix is timed to the bar.
- **Close gap (ripple)** — everything after slides left to fill the hole,
  which shortens the track.

Neither is chosen to begin with, and the page says so. The app asks the first
time you delete part of a clip and remembers the answer here; you can also
just pick one now.

With **Close gap** there is a second choice, for what ripples: the clip only,
or the whole track.

### Behaviour when pasting audio

- **Paste overlaps other clips** — pasted audio lands on top of whatever is
  there.
- **Paste pushes other clips** — the clips after the paste point move right to
  make room.

### Asymmetric stereo heights

Whether dragging the centre line of a stereo track resizes one channel against
the other: **Always**, **Depending on workspace**, or **Never**. Never is the
default and keeps the two halves even, which is usually what you want when
reading a waveform for mix points.

### Mono & stereo conversion

**Always convert to mono without prompt.** Off, the app asks before it
collapses a stereo track to mono. Leave it off unless the prompt is getting in
your way.

### Zoom toggle (magnifying glass)

The magnifying-glass tool flips between two zoom levels with one click —
handy for jumping between a whole-track view and a close look at a beat.

**Zoom state 1** and **Zoom state 2** are the two levels it flips between.

If the tool is not on your toolbar, add it with **Customize toolbar** (the
button at the left of the playback controls), or use **View → Zoom**.

---

## Shortcuts

Every action in the app, with its keyboard shortcut. Search by name or by key,
pick an action, then **Define…** to press a new shortcut or **Clear** to remove
it. **Import** and **Export** save and load a whole set; **Reset to default**
puts one back.

Actions whose name alone would not say what they do show a short explanation
after it: **Sync — Lock tracks to the Snap grid…**. A plugin installed in two
formats is listed once for each, as **Opto Comp (AU)** and **Opto Comp
(VST3)**.

---

## Audit status

Every page has been checked and the sidebar put in order (2026-09-26): setup,
then the DJ pages in the order you work, then display, editing and shortcuts.
28 pages became 12. Stems was added later (2026-09-27), after Effects.
