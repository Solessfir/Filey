# TODO

## Next

- Miller columns, when asked.
- LayoutsSave/Load (custom layouts for Load On Startup), then ExplorerOpen.
- Font pickers, with the system's font families listed on both platforms.

## Look

- The path bar's separators are a pixel wider than the reference's and the gaps around them 2 px wider, so later crumbs sit a few pixels to the right; words themselves now measure the same.
- The shown tab's top right corner is a little sharper than the reference's, and its bottom right curve starts a row early.

## Command palette parity

The palette groups its commands as the reference does; groups without commands yet are left out. The star on the highlighted row adds the command to Quick or takes it out, kept in `QuickCommands.txt`. The reference's command table has 283 entries (names from `HK_` strings in the binary); Filey has 277 commands, 200 of them in the palette.

Already in Filey under another name: SelectionActivate (OpenFocused), SelectionCopy/Cut/Paste, SelectionTrash, SelectionDelete, SelectionDuplicate, SelectionRename, SelectionCopyAsPath, CreateFile, CreateFolder, ToggleExpandFolder, StartFileFilter, StartExpandedSearch, RefreshFolder, MoveFolderBack/Forward/Up, BookmarksToggle, BookmarksClear, RecentsClear, ToggleShowDirectories/Files, ZoomIn/OutContent, View Details/List/Columns/Medium/Large/XL Icons, EditPath, OpenInTab, the Tab commands but those below, TabSearch (TabList), PanelSplitRight/Bottom, PanelSelectNext/Prev/Left/Right/Up/Down, GoToOpen, CommandsOpen, QuickAccessOpen, ContextMenuOpen, ToggleHiddenFiles, ScrollSelectAll, the other Scroll commands, the Text commands (text undo is one step, and Uppercase and Lowercase change ASCII letters only), InspectorToggle (TogglePreview), OptionsOpen (OpenOptions), FolderViewToggle, FolderViewsClear, PanelFilterApply2, PanelFilterCancel, GoToMoveBack/Forward/Up, CloseWindow (WindowClose), MenuOpen (MainMenu), MinimizeWindow (WindowMinimize), SelectionInspect, OpenRelativePath(InTab), TabCloseAll, TabMenu, GoToSelectInPanel, ConsoleOpen, ToggleFullscreen, TypesPopupOpen (with its Toggle All and Untoggle All), FilesPopupOpen/Prev/Next (the folder dropdown of a crumb; This PC's crumb has none yet), StartQuickAccessFilter (Alt+F, the sidebar's Filter box; it does not mark the letters it found yet), PanelEvenSiblings, PanelEvenAll, ToggleSepFilesFolders (CycleFolderSort). Filey only: ToggleFilterPin, Undo, ToggleDebugTheme, ToggleTransparency, NewWindow.

Missing, by the reference's group:

- Palette: CmdWindowToggleDetails (the button next to close), QuickCommandsOpen (Quick).
- Folder: OpenInWindow, SelectionPasteLink, SelectionCopyAsPathSmart, PanelHeaderDirInTab, OpenPathOptionsPopup.
- Sort: DateDeleted and OriginalLocation (with a Recycle Bin view), FreeSpace, FileSystem and PercentFull (with a drives view), VolumeUsage and FolderUsage (with the usage columns). Dates sort by whole seconds; the reference keeps finer times, so items saved in the same second can come out in another order.
- Inspector: its text view reads the first 1 MB.
- Options: still missing are the font pickers (`font` and `inspector.font` in Settings.txt take a family's name), Long Path Support, color scheme editing, custom layouts for Load On Startup, and checkboxes in the icon views.
- Tabs: OpenTabOptionsPopup, TabOpenInWindow.
- Misc: ExplorerOpen, LayoutsSave/Load, ConfigCreate/Open, QuickAccessFilterRevert (the filter's Enter and Escape are there), viewport and group options. Install is the reference's own setup and stays out.

## From the reference's roadmap

Shipped there, missing here:

- Tree view: folders as a classic expandable hierarchy.
- Group by: folder items in groups by date, type or size.
- Hierarchical folder views: a locked view that applies to a whole folder tree, not one folder.
- Drag and drop from ZIP archives and browsers (virtual files, `CFSTR_FILEDESCRIPTOR`/`CFSTR_FILECONTENTS`), and drops onto executables and scripts that run them with the files.
- Bookmarks and recents on offline drives kept and shown until the drive is back.
- Network shares listed in the sidebar.

Planned there, worth doing here first:

- Miller columns: each opened folder adds a column to the right. Decided: its own view toggle (a command with a hotkey, saved per panel), outside the zoom ladder, so Ctrl+wheel steps the other views as now; the column right of the shown folder lists the focused folder's contents and stays empty for a file, whose preview stays in the inspector.
- Multiline names in the icon views, and larger thumbnails that use the grid's room.
- Command-line arguments beyond folders (open in a new tab of the running window).
- Virtual folders (Recycle Bin, Control Panel and other shell namespaces) listed like real ones, and MTP devices (phones, cameras) on both platforms.
- Custom Quick Access groups: created, renamed and ordered by the user.
- Rebindable mouse buttons and actions, with the hotkeys.
- Names in any script: check font fallback for CJK, Arabic, Hebrew and emoji on both platforms.
- Indexing the drives from the NTFS file table (Windows) for a faster Go To walk.

## Filey's own

- Git: the path bar shows the branch and the commits to push and pull (HEAD read directly, the counts from `git rev-list` in a job). Still to do: uncommitted changes, file states in the list, and better text and styling for what the path bar shows, which looks rough now.
- 3D model previews: the inspector draws OBJ, STL, PLY, glTF/GLB and binary FBX from a fixed view, flat-shaded and without textures; `.blend` files show the preview Blender embeds, through the system's thumbnailer where Blender is installed. Still to do: an orbit view, the same pictures as thumbnails in the icon views, text FBX, and Collada, 3DS, USD and 3MF.
- Linux install and uninstall through a standalone `.sh` script, like https://github.com/Solessfir/razer-laptop-control's `install.sh`, documented in a README. `./Build.sh install` covers installing only, and the repository has no README yet.
- Context menu editing: a shortcut in an open menu enters an edit mode, with a ghost panel beside the menu. Dragging an item into the panel hides it, and dragging it back shows it again. Items can be reordered in the menu, and a button restores the default. Hidden items and the order are saved with the pins. The sidebar's Places get the same editing.
- Version: the only version is a hard-coded 0.1.0 in `Filey.rc`, and the app never shows it. A GitHub Actions release workflow, run on a pushed `v*` tag, passes the tag to the build through `EXTRA_CFLAGS` (`-DFILEY_VERSION=...`). Local builds fall back to `git describe --tags --always --dirty`. The same define feeds `Filey.rc` (llvm-rc takes `-D`) and the hamburger menu.
- Windows install: Filey copies itself to `%LOCALAPPDATA%` and adds Start menu and desktop shortcuts. Optionally, it also takes over from Explorer through keys under `HKCU\Software\Classes`, as the reference does, with an undo that deletes them:
  - `*\shell` and `Directory\shell` entries that open the item in Filey.
  - `Directory\Background\shell` and `DesktopBackground\shell` entries that open Filey there.
  - The `open` commands of `Directory\shell` and `Drive\shell`, so that folders and drives open in Filey.
  - The `opennewwindow` command of `CLSID\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}`, so that Win+E opens Filey.
- Themes, before release:
  - Drop the debug and fullmoon themes.
  - Ship light and dark defaults, picked on first launch from the system's scheme. `theme.system` exists but is off by default.
  - On Linux, read the xdg-desktop-portal `org.freedesktop.appearance` `color-scheme` setting first. `GTK_THEME` and `gtk-3.0/settings.ini`, which are read now, become fallbacks, and dark stays the default.
  - Add popular schemes: Catppuccin (Mocha and Latte), Nord, Gruvbox, Tokyo Night, One Dark, Dracula, Solarized and Rosé Pine.
  - Matugen on Linux: its colors come in as `color.<field>` lines, which Settings.txt already reads and applies as the file is saved. Matugen renders whole files, so its template has to be the whole Settings.txt; a separate colors file that Settings.txt includes would leave the other settings to the user.
- Context menu lag: the warm-up job runs once a session, for the first item listed, so the handlers of other file types still load on the first right-click. Build the hovered or focused item's real menu ahead of time on a background STA thread, so a right-click finds it ready. The menu has to be invoked on that same thread too.
- Archives as folders: a zip, 7z, rar, tar or cab opens like a folder, its files preview and open from a copy extracted to the run's temp folder, and drags, copies and pastes in and out extract or add in a transfer job; moves copy, leaving the source. Windows reads through the shell's archive folders, Linux through libarchive, and both write with libarchive (Windows ships it as `archiveint.dll`); a RAR or cabinet takes no drop. Still to do: an archive inside an archive opens in its program, a Linux drag to other apps extracts before it starts, RAR can't be written (no free RAR writer exists), the extracted copies are left in the temp folder after the run, and the inspector's text listing of an archive could become this browsable list.
- Hex view (ImHex-like, https://github.com/WerWolv/ImHex): it reads pages as they come into sight, up to a million rows (16 MB at 16 bytes a row), since the scroll offset is a float. Still to do: past that, a selection and copy, and searching for bytes.
- Syntax highlighting: the inspector colors keywords, strings, comments, numbers and headings by extension (Prog_SyntaxForPath). Still to do: XML/HTML, CMake, Makefiles, and colors in the theme and Settings.txt instead of the fixed dark and light ones.
- Localization, low priority: the interface in other languages. Until then, keep user-facing strings in few places (Commands.txt labels, the option table) so they can be translated later.

## From the RAD Debugger

Its source (https://github.com/EpicGamesExt/raddebugger) has these ideas worth taking:

- Inspector images are sampled nearest at every scale (RGBA textures are `GL_NEAREST`), so a photo of up to 4096 px shrunk to fit the inspector likely aliases. raddbg picks nearest or linear per draw. Use mipmaps with trilinear filtering when shrinking, and nearest when enlarging, for pixel inspection.
- Text width: the reference rasterizes through the same DirectWrite GDI interop as raddbg (`src/font_provider/dwrite`), which differs from Filey in two ways:
  - Its advances come from `GetGdiCompatibleGlyphMetrics` in GDI-natural mode, not from design metrics.
  - Gamma 1.5 and enhanced contrast 1.0 are fixed, not the system's.
  - For Segoe UI, GDI-natural advances make words 0-3 px wider at 15 px and 1-5 px narrower at 16.25 px. Measure the reference's label width at a known size first, then try them.
- Hex view, from raddbg's memory view:
  - Byte brightness follows the byte's value, with zeros at half strength, so patterns stand out.
  - A peek bar reads the bytes at the cursor as u8 to u64 and f32/f64.
- 3D previews: raddbg's geo3d view is the template, with an orbit camera (drag turns, wheel zooms, smoothed by `1 - 2^(-rate*dt)`) and a mesh pass with depth composited into the UI. The meshes come from `Program/Models.c`, which draws them on the CPU for the fixed view now.
- Large text files: raddbg's text layer (`src/text`) keeps a map of line offsets and checkpoints of the lexer's state. It shows any line of a huge file and highlights from the nearest checkpoint. Use the same approach to get past the inspector's first 1 MB.
- Glyphs on demand: raddbg rasterizes each codepoint the first time it's drawn, into an atlas keyed by hash (`src/font_cache`). Filey rasterizes fixed ranges at startup. Rasterizing on demand draws any codepoint and starts faster. Font fallback is still needed separately, and raddbg has none.
- Cache eviction: raddbg's artifact cache (`src/artifact_cache`) evicts entries untouched for a while, and serves the stale value while it recomputes, so nothing flashes blank. Filey's icon cache resets wholesale when full.
- Maybe:
  - Per-corner colors in the rect shader, for fades at the edges of scrolled lists such as menus, the sidebar and long tab lists.
  - Linear-light blending into an sRGB framebuffer, but only if the reference does it too, since Filey's colors were tuned against it.
  - Panel commands: swap panels, reset to the default layout and hide the UI.
  - A readout of the hovered pixel's color in the image view at high zoom.

## Checks

- Compare rename, drag and drop, the preview pane and the settings page side by side.
