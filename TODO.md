# TODO

## Next

- Hex view: rows stop at a million (16 MB), since the scroll offset is a float; no selection and copy, no searching for bytes. From raddbg's memory view: brightness that follows the byte's value, zeros at half strength, and a peek bar reading the bytes at the cursor as u8 to u64 and f32/f64.

## Look

- Low priority: in the title bar the new tab plus sits level with the tabs, which hang below the strip's middle, while the tab list's chevron and the window buttons are centered on the whole strip, about 5 pixels higher. Likely fix: the chevron level with the tabs too, in a tab-sized box, as Windows Terminal and Chrome pair them.
- The sidebar's filter (Alt+F) does not mark the letters it found, as the list's filter does.
- The sidebar headings take the Group color brightened by a fixed 1.06; the reference's factor changes with the scheme.

## Missing

- Sorting: the Recycle Bin's Date Deleted and Original Location columns and This PC's File System, Free Space and Percent Full columns cannot be sorted by; the usage columns sort by size but show no arrow. Dates compare whole seconds, so items saved in the same second keep name order.
- This PC's crumb has no folder dropdown.
- Long Path Support in Options (paths already go through `\\?\`; only the option is missing), checkboxes in the icon views, and the palette Misc group's viewport options.
- Virtual folders: Control Panel and the other shell namespaces (the Recycle Bin is listed), and MTP devices (phones, cameras).
- Mouse bindings: only the side buttons bind to commands; the left, middle, right and wheel actions are fixed switches.
- Indexing the drives from the NTFS file table for a faster Go To walk.
- Panel commands: swap panels, reset to the default layout, hide the UI.
- The image view's readout of the hovered pixel's color at high zoom.
- 3D models: easing for the view's turns and zoom (raddbg smooths by `1 - 2^(-rate*dt)`), text USD (`.usda`), text FBX, a .blend's modifiers, and Collada, 3DS and 3MF. Colors not read: PLY's per-face `texcoord` lists, a .blend's object-linked materials, glTF's `KHR_texture_transform`, and transparency other than a picture's cut-out alpha; Blender 3.4 to 4.x files are untested and earlier ones get no colors.
- Archives: encrypted 7z and RAR archives, which libarchive cannot decrypt; an archive inside an archive opens in its program; and the run folder the extracted copies go to (`%TEMP%\Filey\<pid>`) is never removed.
- Syntax highlighting: XML/HTML, CMake and Makefiles (by whole name), and colors from the theme instead of the fixed dark and light ones.
- Media: the waveform is ffmpeg's picture tinted with the accent rather than drawn natively, yt-dlp always gets `--no-playlist`, a cancelled torrent's part files are kept (so that it resumes), no seeding options, and no notice how much smaller a compressed video came out.
- Languages: more built-in ones; counted forms by a rule a language names, for those such as Polish and Arabic that two or three forms do not cover; the build's check for untranslated text misses single words.
- Big text files: the inspector holds 16 MB of one at a time; its filter looks only through the lines held, and its lines are not wrapped.
- Updates: the Apps & features entry keeps the version Filey Setup wrote.
- Context menus: the sidebar's places are built when right-clicked, not ahead as the list's hovered or keyboard item is.

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
- Release flags: `-O3`, `-march=x86-64-v2` and `-v3`, and profile-guided optimization measured within the noise of `-O2` or slower.

## Checks

- Try the update with the first two real releases: the notice and the dot once the newer one is out, Update, the restart with the session, and on Linux too.
- Try network shares against a real SMB server: its shares under Storage with their sizes, `\\server` listing them, Up from a share to the server, and a mapped drive's server.
- Compare rename, drag and drop, the preview pane and the settings page side by side.
