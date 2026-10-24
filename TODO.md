# TODO

## Missing

- Virtual folders: Control Panel and the other shell namespaces (the Recycle Bin is listed), and MTP devices (phones, cameras).
- 3D models: text USD (`.usda`), text FBX, a .blend's modifiers, and Collada, 3DS and 3MF. Colors not read: PLY's per-face `texcoord` lists, a .blend's object-linked materials, glTF's `KHR_texture_transform`, and transparency other than a picture's cut-out alpha; Blender 3.4 to 4.x files are untested and earlier ones get no colors.
- Archives: encrypted 7z and RAR archives, which libarchive cannot decrypt, and an archive inside an archive opens in its program.
- Low priority: faster drive index without running Filey as an administrator. An Options switch registers, with one UAC prompt, a scheduled task with the highest privileges whose only job is `Filey.exe --build-index` (the NTFS file tables into `Index.bin`); Filey starts it without a prompt when the index is stale and takes the new file when it lands. The switch off removes the task. Filey itself stays unelevated, for drag and drop and so that what it opens is not run as an administrator.
- Languages: more built-in ones; counted forms by a rule a language names, for those such as Polish and Arabic that two or three forms do not cover; the build's check for untranslated text misses single words.

## From the RAD Debugger

- Text width: the reference rasterizes through the same DirectWrite GDI interop as raddbg (`src/font_provider/dwrite`), with advances from `GetGdiCompatibleGlyphMetrics` in GDI-natural mode and gamma 1.5 and enhanced contrast 1.0 fixed; Filey takes design advances and the system's gamma and contrast. For Segoe UI, GDI-natural advances make words 0-3 px wider at 15 px and 1-5 px narrower at 16.25 px. Measure the reference's label width at a known size first.
- Cache eviction: raddbg's artifact cache (`src/artifact_cache`) evicts entries untouched for a while and serves the stale value while it recomputes. Filey's thumbnail atlas clears a whole slot when full, and the type icons stop being added.
- Maybe: per-corner colors in the rect shader, for fades at the edges of scrolled lists; linear-light blending into an sRGB framebuffer, only if the reference does it too.

## Linux

To be checked on Linux:

- Drag and drop from archive managers and browsers, and drops onto executables.
- Asking a server for its shares (`smbclient -L`, or the gvfs mounts in `$XDG_RUNTIME_DIR/gvfs`); mounted CIFS and NFS shares already show as drives.
- Following the theme's light or dark change while Filey runs (the settings portal's `SettingChanged`), and the portal path on a desktop that has it.
- An update replaces only the executable, not the desktop entry and icons.
- Type names ("Folder", "File", "%S file") stay English.
- A drag from an archive to other apps extracts before it starts.

## Not planned

- Writing RAR archives (no free RAR writer exists), and entries in Windows 11's first right-click menu, which shows only a packaged app's `IExplorerCommand`.
- A Direct3D 11 renderer and Office documents' real layout.
- Seeding torrents, and a waveform drawn natively rather than ffmpeg's picture tinted with the accent.
- Binding the left, middle and right buttons and the wheel to commands: only the side buttons bind, as the reference's mouse hotkeys cannot be changed at all.
- Release flags: `-O3`, `-march=x86-64-v2` and `-v3`, and profile-guided optimization measured within the noise of `-O2` or slower.

## Checks

- Try the update with the first two real releases: the notice and the dot once the newer one is out, Update, the restart with the session, and on Linux too.
- Try network shares against a real SMB server: its shares under Storage with their sizes, `\\server` listing them, Up from a share to the server, and a mapped drive's server.
- Compare rename, drag and drop, the preview pane and the settings page side by side.
