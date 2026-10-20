# TODO

## Next

- Release speed, squeezed as far as it goes, each step measured with the `profile` build on a large folder before it stays: profile-guided optimization (a training run with `-fprofile-generate`, then `-fprofile-use`, in a release script), a CPU baseline (`-march=x86-64-v2`, and whether `x86-64-v3` with AVX2 is worth leaving older CPUs behind), `-O3` against `-O2`, and `-Wl,/opt:ref,/opt:icf` for size.
- Names from the reference: none of its own names stay anywhere, in the code, comments, docs or the git history. That includes the built-in schemes now named after its six (`sirius`, `pollux`, `eridanus`, `regulus`, `altair`, and the full moon one that was named after its maker), which get names of Filey's own; the history is rewritten to drop them, and the rewritten history may be force-pushed over GitHub's.
- Text fields: every box that takes typing (the filters, the palette, Go To, rename, the Save Layout box and the rest) takes the terminal's editing keys: Ctrl+W deletes back to the previous whitespace and Alt+Backspace the word before the caret, Alt+D the word after it, Ctrl+U to the line's start and Ctrl+K to its end; Ctrl+A and Ctrl+E go to the start and end, Ctrl+B and Ctrl+F move a character back and forward, and Alt+B and Alt+F a word; Ctrl+D deletes the character under the caret and Ctrl+H the one before; Ctrl+T swaps the two characters at the caret and Alt+T the two words; Alt+U, Alt+L and Alt+C uppercase, lowercase and capitalize the rest of the word. Where one of these is already a hotkey, such as Ctrl+T for a new tab or Ctrl+F for the filter, the field takes it while it has the keyboard (Ctrl+C and Ctrl+V keep copy and paste). Which field keys win over the hotkeys is settled by trying them together with the user first, before it is committed as done.
- Font picker: each family's name should show in that family, as a preview.
- Tab in the lists, to discuss: Tab and Shift+Tab switch panels, as the reference does, but stepping through a panel's items with them was wanted too. Trying Tab for items took away the only cycling key for panels (F6, Shift+F6 and Ctrl+F6 move and copy to the neighbor panel, Ctrl+Tab switches tabs, Ctrl+Alt and the arrows focus a panel by direction). Settle which keys do what, such as Ctrl+Tab for panels with tabs on Ctrl+PageDown and Ctrl+PageUp, or Ctrl+` for panels.


## Look

- The path bar's separators are a pixel wider than the reference's and the gaps around them 2 px wider, so later crumbs sit a few pixels to the right; words themselves now measure the same.
- The shown tab's top right corner is a little sharper than the reference's, and its bottom right curve starts a row early.

## Command palette parity

The palette groups its commands as the reference does; groups without commands yet are left out. The star on the highlighted row adds the command to Quick or takes it out, kept in `QuickCommands.conf`. The reference's command table has 283 entries (names from `HK_` strings in the binary); Filey has 277 commands, 200 of them in the palette.

Already in Filey under another name: SelectionActivate (OpenFocused), SelectionCopy/Cut/Paste, SelectionTrash, SelectionDelete, SelectionDuplicate, SelectionRename, SelectionCopyAsPath, CreateFile, CreateFolder, ToggleExpandFolder, StartFileFilter, StartExpandedSearch, RefreshFolder, MoveFolderBack/Forward/Up, BookmarksToggle, BookmarksClear, RecentsClear, ToggleShowDirectories/Files, ZoomIn/OutContent, View Details/List/Columns/Medium/Large/XL Icons, EditPath, OpenInTab, the Tab commands but those below, TabSearch (TabList), PanelSplitRight/Bottom, PanelSelectNext/Prev/Left/Right/Up/Down, GoToOpen, CommandsOpen, QuickAccessOpen, ContextMenuOpen, ToggleHiddenFiles, ScrollSelectAll, the other Scroll commands, the Text commands (text undo is one step, and Uppercase and Lowercase change ASCII letters only), InspectorToggle (TogglePreview), OptionsOpen (OpenOptions), FolderViewToggle, FolderViewsClear, PanelFilterApply2, PanelFilterCancel, GoToMoveBack/Forward/Up, CloseWindow (WindowClose), MenuOpen (MainMenu), MinimizeWindow (WindowMinimize), SelectionInspect, OpenRelativePath(InTab), TabCloseAll, TabMenu, GoToSelectInPanel, ConsoleOpen, ToggleFullscreen, TypesPopupOpen (with its Toggle All and Untoggle All), LayoutsSave/Load (kept in `Layouts.conf`, with a window line before a session's, and named by Load On Startup), ExplorerOpen (Open File Explorer), FilesPopupOpen/Prev/Next (the folder dropdown of a crumb; This PC's crumb has none yet), StartQuickAccessFilter (Alt+F, the sidebar's Filter box; it does not mark the letters it found yet), PanelEvenSiblings, PanelEvenAll, ToggleSepFilesFolders (CycleFolderSort). Filey only: ToggleFilterPin, Undo, ToggleDebugTheme, ToggleTransparency, NewWindow.

Missing, by the reference's group:

- Palette: CmdWindowToggleDetails (the button next to close), QuickCommandsOpen (Quick).
- Folder: OpenInWindow, SelectionPasteLink, SelectionCopyAsPathSmart, PanelHeaderDirInTab, OpenPathOptionsPopup.
- Sort: DateDeleted and OriginalLocation (with a Recycle Bin view), FreeSpace, FileSystem and PercentFull (with a drives view), VolumeUsage and FolderUsage (with the usage columns). Dates sort by whole seconds; the reference keeps finer times, so items saved in the same second can come out in another order.
- Inspector: its text view reads the first 1 MB.
- Options: the Font and Inspector Font rows pick from the installed families (DirectWrite's system collection, fontconfig on Linux). The Color Scheme choice opens the reference's dropdown of its six schemes (measured from it, all of their 33 color groups) after Filey's default, and the custom ones (`Schemes.conf`) under a line, each forked or a custom one deleted from its row; a custom scheme's second row opens its color groups, each a hex field and a swatch that opens the picker; with System Light / Dark Scheme on, a scheme for each mode. Still to do there: the reference's picker, which opens with its top left at the pointer, kept inside the window, about 586 by 394 logical pixels: a "<Group> color" title, a saturation and brightness square with a hue strip beside it, a preview swatch, H, S, B, R, G and B number fields each with its greyed maximum after a slash, a # field, and Ok and Cancel; editing a group's hex digits in place; and the sidebar headings' color, which the reference draws from the Group color by a factor that changes with the scheme (Filey takes 1.06, as the default and Full Moon schemes show). Still missing are Long Path Support and checkboxes in the icon views.
- Tabs: OpenTabOptionsPopup, TabOpenInWindow.
- Misc: ConfigCreate/Open, QuickAccessFilterRevert (the filter's Enter and Escape are there), viewport and group options. Install is the reference's own setup and stays out.

## From the reference's roadmap

Shipped there, missing here:

- Tree view: folders as a classic expandable hierarchy.
- Group by: folder items in groups by date, type or size.
- Hierarchical folder views: a locked view that applies to a whole folder tree, not one folder.
- Drag and drop from ZIP archives and browsers (virtual files, `CFSTR_FILEDESCRIPTOR`/`CFSTR_FILECONTENTS`), and drops onto executables and scripts that run them with the files.
- Bookmarks and recents on offline drives kept and shown until the drive is back.
- Network shares listed in the sidebar.

Planned there, worth doing here first:

- Multiline names in the icon views, and larger thumbnails that use the grid's room.
- Command-line arguments beyond folders (open in a new tab of the running window).
- Virtual folders (Recycle Bin, Control Panel and other shell namespaces) listed like real ones, and MTP devices (phones, cameras) on both platforms.
- Custom Quick Access groups: created, renamed and ordered by the user.
- Rebindable mouse buttons and actions, with the hotkeys.
- Names in any script: check font fallback for CJK, Arabic, Hebrew and emoji on both platforms.
- Indexing the drives from the NTFS file table (Windows) for a faster Go To walk.

## Filey's own

- Miller columns: Ctrl+Shift+7 (Toggle Miller Columns, also in the view menu) turns them on per panel, kept in the session. The shown folder's parents stand left of its list, as many as fit at 240 px a column, each with the folder on the way lit, and the focused folder's items right of it, empty for a file; a press in a side column goes to its folder with the item selected, a double press opens it, and Left and Right go up and into folders. Still to do: the side columns have no context menu, drags or rename, they list again only when they come back into view rather than following a watch, and the middle column shows names only.
- Git: the path bar shows the branch and the commits to push and pull (HEAD read directly, the counts from `git rev-list` in a job). Still to do: uncommitted changes, file states in the list, and better text and styling for what the path bar shows, which looks rough now. A folder of repositories, such as one holding every clone, could show each one's state in its row: the branch, uncommitted changes, and the commits to push and pull.
- 3D model previews: the inspector draws OBJ, STL, PLY, glTF/GLB and binary FBX from a fixed view, flat-shaded and without textures; `.blend` files show the preview Blender embeds, through the system's thumbnailer where Blender is installed. Still to do: an orbit view, the same pictures as thumbnails in the icon views, text FBX, and Collada, 3DS, USD and 3MF.
- Linux install and uninstall through a standalone `.sh` script, like https://github.com/Solessfir/razer-laptop-control's `install.sh`, documented in a README. `./Build.sh install` covers installing only, and the repository has no README yet.
- Context menu editing: a shortcut in an open menu enters an edit mode, with a ghost panel beside the menu. Dragging an item into the panel hides it, and dragging it back shows it again. Items can be reordered in the menu, and a button restores the default. Hidden items and the order are saved with the pins. The sidebar's Places get the same editing.
- Version: the only version is a hard-coded 0.1.0 in `Filey.rc`, and the app never shows it. A GitHub Actions release workflow, run on a pushed `v*` tag, passes the tag to the build through `EXTRA_CFLAGS` (`-DFILEY_VERSION=...`). Local builds fall back to `git describe --tags --always --dirty`. The same define feeds `Filey.rc` (llvm-rc takes `-D`) and the hamburger menu.
- Update check, once the repository is public: a background job asks `https://api.github.com/repos/Solessfir/Filey/releases/latest` at startup (at most once a day, the last check kept in the cache folder) and compares its `tag_name` with `FILEY_VERSION`. A newer release lights a "new version available" dot on the hamburger button, and the menu item opens the release page; Filey Setup then offers to download and install it. No network without a release to compare, and a setting turns the check off.
- Windows install: the main menu's Filey Setup page copies the running Filey to `%LOCALAPPDATA%\Filey` (the settings are in `%APPDATA%\Filey`), adds a Start menu shortcut, and on request a desktop shortcut, the folder on the user's PATH, and Set as default (the `open` commands of `Directory` and `Drive` and Win+E's `opennewwindow` under `HKCU\Software\Classes`); Modify changes those, and Uninstall takes away only what the install made. Still to do there: an entry in Apps & features (`HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Filey`, which starts Filey with `--uninstall`), and the right-click entries, the switch in the empty second row:
  - `*\shell` and `Directory\shell` entries that open the item in Filey; a file passed on the command line opens its folder with it selected.
  - `Directory\Background\shell` and `DesktopBackground\shell` entries that open Filey there (`%V`).
- Themes, before release:
  - Drop the debug and fullmoon themes.
  - Ship light and dark defaults, picked on first launch from the system's scheme. `theme.system` exists but is off by default.
  - On Linux, read the xdg-desktop-portal `org.freedesktop.appearance` `color-scheme` setting first. `GTK_THEME` and `gtk-3.0/settings.ini`, which are read now, become fallbacks, and dark stays the default.
  - Add popular schemes: Catppuccin (Mocha and Latte), Nord, Gruvbox, Tokyo Night, One Dark, Dracula, Solarized and Rosé Pine.
  - Matugen on Linux: its colors come in as `color.<field>` lines, which Settings.conf already reads and applies as the file is saved. Matugen renders whole files, so its template has to be the whole Settings.conf; a separate colors file that Settings.conf includes would leave the other settings to the user.
- Context menu lag: the warm-up job runs once a session, for the first item listed, so the handlers of other file types still load on the first right-click. Build the hovered or focused item's real menu ahead of time on a background STA thread, so a right-click finds it ready. The menu has to be invoked on that same thread too.
- Archives as folders: a zip, 7z, rar, tar or cab opens like a folder, its files preview and open from a copy extracted to the run's temp folder, and drags, copies and pastes in and out extract or add in a transfer job; moves copy, leaving the source. Both platforms read and write them with libarchive (Windows ships it as `archiveint.dll`), with the transfer cards' progress, pause and cancel, and Windows drags out through the shell's archive folders; a RAR or cabinet takes no drop. Opening a file from one shows a card while it is taken out, and the inspector shows how far its preview is taken out. Still to do: a password prompt for encrypted archives, which fail to open now, an archive inside an archive opens in its program, a Linux drag to other apps extracts before it starts, RAR can't be written (no free RAR writer exists), and the extracted copies are left in the temp folder after the run. The inspector lists an archive as it does a folder, under what it holds in all and how much smaller it packs that.
- Hex view (ImHex-like, https://github.com/WerWolv/ImHex): it reads pages as they come into sight, up to a million rows (16 MB at 16 bytes a row), since the scroll offset is a float. Still to do: past that, a selection and copy, and searching for bytes.
- Syntax highlighting: the inspector colors keywords, strings, comments, numbers and headings by extension (Prog_SyntaxForPath). Still to do: XML/HTML, CMake, Makefiles, and colors in the theme and Settings.conf instead of the fixed dark and light ones.
- Localization, low priority: the interface in other languages. Until then, keep user-facing strings in few places (Commands.txt labels, the option table) so they can be translated later.

## From the RAD Debugger

Its source (https://github.com/EpicGamesExt/raddebugger) has these ideas worth taking:

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
