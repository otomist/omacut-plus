# Omacut

A dead-simple video **length** trimmer. Open a video, drag the two handles to pick a start and end, preview the clip, and export. On Omarchy, the interface follows your theme's accent color.

It also does **subtitles**: press *C* for the caption lane, drop text at the playhead, and it gets burned into the export.

Built using **Qt Quick (QML)** UI with the Material style — the same Qt stack Quickshell builds on — and **ffmpeg** for the cut. The C++ side compiles to a single executable; the QML is embedded in it via Qt resources.

<img width="3227" height="3227" alt="screenshot-2026-06-23_15-20-40" src="https://github.com/user-attachments/assets/c76047c8-618f-4c1c-91f9-e7024c4f953b" />

## Hotkeys

- *Space*: Start/stop video playback.
- *Left/Right*: Move the playhead by 1 second.
- *Shift+Left/Right*: Move the playhead by 5 seconds.
- *Alt+Left/Right*: Move the playhead by 0.2 seconds.
- *Ctrl+Space*: Move the start of the trim to the playhead.
- *Alt+Space*: Move the end of the trim to the playhead.
- *Z*: Zoom into the trimmed selection for fine tuning (Z again zooms back out).
- *C*: Show or hide the subtitle editor.
- *T*: Add a caption at the playhead.
- *[* / *]*: Move the selected caption's start/end to the playhead.
- *Del*: Delete the selected caption.
- *Ctrl+O*: Open a new file to trim.
- *Ctrl+S*: Export the current trim.
- *Q*: Quit (asks first if the trim hasn't been exported).
- *?*: Show the hotkeys in the app.

## Subtitles

Press *C* (or the **T** button next to Export) for the caption lane under the filmstrip.

- *T*, or **+ Caption**, drops a two-second caption at the playhead; type into the field below the
  lane. Double-clicking an empty part of the lane adds one right there.
- Drag a caption block to move it, or grab an edge to retime it — the same way the trim handles
  work. *[* and *]* pull the selected caption's edges to the playhead.
- **Style** sets the look every caption shares: font, size, bold, text and outline colour, outline
  width, an optional background box, and how far off the bottom the text sits. Sizes are in the
  video's own pixels, so a 720p export shrinks the captions along with the frame.
- The preview over the video is what you get: it's laid out against the same numbers the export
  uses.

Captions are **burned into** the exported video (via ffmpeg and libass), so they survive anywhere
the clip is played. The source file is never touched, and captions that fall outside the trim are
left out of the export.

When the clip has captions, the export dialog also offers a **Subtitles** choice:

- *Burned in + .srt file* (the default) — also writes `<name>.srt` next to the video, holding each
  caption's text and timing. The times are relative to the exported clip, so the file drops straight
  into a player, YouTube or another editor.
- *Burned in only* — just the video.

A failed export never touches an `.srt` that was already there.

## Install

Install via the Omarchy Package Repository via the `omacut` package. It's installed by default in new installations of Omarchy (from Quattro forward).

## Requirements

- `xdg-desktop-portal` and a portal backend for the file picker
- `ffmpeg` and `ffprobe` on your PATH (used at runtime), with libass support for burning in captions

Exports are always written as MP4 files, regardless of the input video's container. The export dialog offers Original/1080p/720p quality — never upscaling, and always preserving the aspect ratio — and, for a clip with captions, whether to write an `.srt` beside it.

## Build

Uses Qt's own build tool, `qmake6` (no cmake needed):

```bash
./bin/build
```

This produces a single `omacut` binary in `build/`.

Requirements:

- A C++17 compiler and Qt6: `qt6-base`, `qt6-declarative` (Qt Quick + Controls),
  `qt6-multimedia`

## Test

```bash
./bin/test
```

## Package

Build and install the local Arch package:

```bash
./bin/install
```

This runs `./bin/build`, then `makepkg -fsi` from `pkgbuild/` so same-version local packages are rebuilt and reinstalled. Extra arguments are passed through to `makepkg`, for example `./bin/install --clean`. The package installs the binary, desktop entry, app icon, and MIT license. Local package outputs such as `pkgbuild/pkg/`, `pkgbuild/src/`, and `*.pkg.tar.*` are ignored.

## License

MIT. See `LICENSE`.
