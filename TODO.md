# TODO

## Next, in order

1. View toggles: Increase/Decrease/ResetFontSize, Increase/DecreaseSpacing, ToggleFileExtension, ToggleSystemFiles.
2. Recents record the items opened from a list, as the reference's do, not every folder shown.
3. Recursive listing (below).
4. Two-panel and selection commands: OpenInRightSplit/BottomSplit, SelectionCopy/MovePrev/NextPanel, ToggleCursorSelection, OpenContainingPath(InTab), CalcDirsContents, TrashRestoreAll/DeleteAll.
5. Go To index: packed names and a parallel walk (below).
6. Rename inserts and RenamePrev/Next.
7. Command palette parity (below).
8. LayoutsSave/Load (custom layouts for Load On Startup), then ExplorerOpen, ConsoleOpen, ToggleFullscreen.
9. Font pickers, with the system's font families listed on both platforms.
10. Look nits (below).
11. Rebindable text and scroll commands.

## Go To and the drive index

- List several folders at once during the walk. The first walk is limited by listing one folder at a time; parallel slices need their writes to `Index.bin.walk` kept in the order the entries join the index.
- Pack the index's names into one buffer instead of a `String` per entry. A full walk of C: takes about 320 MB, mostly names and their headers.
- The typed letters in a highlighted search row are drawn in the highlight's own blue, so they disappear.

## Recursive listing (Ctrl+E)

- Show rows as the walk finds them, as the reference does, instead of all at once at the end; its first count is the rows listed so far.
- The reference marks it with "[..]" after the folder's name in the tab and the breadcrumbs.
- It shows an item inside a subfolder as "file001.txt › Docs", not "Docs/file001.txt", and lists the folder's own items before those of its subfolders.
- With nothing left after filtering it shows an empty list, without "No items match the filter".

## Look

- Text is 2-4 px wider over a word than the reference's.
- The tab outline is 2 px; the reference's is about 1.5 px.
- The tab close × comes from the icon font and is softer than the reference's.

## Command palette parity

The palette groups its commands as the reference does; groups without commands yet are left out. Still missing there: the reference's star and plus buttons on the highlighted row (add to Quick, add a hotkey), its dimmed rows and darker highlight for commands that cannot run now, its filled star icon, and its smooth scrolling. The reference's command table has 283 entries (names from `HK_` strings in the binary); Filey has 130, 77 of them in the palette.

Already in Filey under another name: SelectionActivate (OpenFocused), SelectionCopy/Cut/Paste, SelectionTrash, SelectionDelete, SelectionDuplicate, SelectionRename, SelectionCopyAsPath, CreateFile, CreateFolder, ToggleExpandFolder, StartFileFilter, StartExpandedSearch, RefreshFolder, MoveFolderBack/Forward/Up, BookmarksToggle, BookmarksClear, RecentsClear, ToggleShowDirectories/Files, ZoomIn/OutContent, View Details/List/Columns/Medium/Large/XL Icons, EditPath, OpenInTab, the Tab commands but those below, TabSearch (TabList), PanelSplitRight/Bottom, PanelSelectNext/Prev/Left/Right/Up/Down, GoToOpen, CommandsOpen, QuickAccessOpen, ContextMenuOpen, ToggleHiddenFiles, ScrollSelectAll, InspectorToggle (TogglePreview), OptionsOpen (OpenOptions), FolderViewToggle, FolderViewsClear, PanelFilterApply2, PanelFilterCancel, GoToMoveBack/Forward/Up. Filey only: ToggleFilterPin, Undo, ToggleDebugTheme, ToggleTransparency, NewWindow.

Missing, by the reference's group:

- Palette: CmdWindowToggleDetails (the button next to close), QuickCommandsOpen (Quick), MenuOpen.
- Folder: ToggleCursorSelection, OpenInRightSplit, OpenInBottomSplit, OpenInWindow, SelectionPasteLink, SelectionCopy/MovePrev/NextPanel, SelectionInspect, SelectionRestore, SelectionCopyAsPathSmart, OpenRelativePath(InTab), OpenContainingPath(InTab), CalcDirsContents, TrashDeleteAll, TrashRestoreAll, FilesPopupOpen/Prev/Next, TypesPopupOpen, PanelHeaderDirInTab, OpenPathOptionsPopup.
- View: ToggleSepFilesFolders, Increase/Decrease/ResetFontSize, Increase/DecreaseSpacing, ToggleFileExtension, ToggleSystemFiles.
- Sort: DateDeleted and OriginalLocation (with a Recycle Bin view), FreeSpace, FileSystem and PercentFull (with a drives view), VolumeUsage and FolderUsage (with the usage columns). Dates sort by whole seconds; the reference keeps finer times, so items saved in the same second can come out in another order.
- Inspector: its text view reads the first 1 MB.
- Options: still missing are the font pickers (`font` and `inspector.font` in Settings.txt take a family's name), Long Path Support, Character Key Action, color scheme editing, custom layouts for Load On Startup, and checkboxes in the icon views. The reference's recents are the items opened from a list, while Filey's are every folder it shows, so Highlight Recents marks more.
- Panels: PanelEvenSiblings, PanelEvenAll.
- Tabs: OpenTabOptionsPopup, TabOpenInWindow, TabCloseAll, TabMenu.
- Go To: GoToSelectInPanel.
- Misc: ExplorerOpen, ConsoleOpen, MinimizeWindow, ToggleFullscreen, CloseWindow, LayoutsSave/Load, ConfigCreate/Open, the Quick Access filter, viewport and group options. Install is the reference's own setup and stays out.
- Rename: InsertIncrementalID, InsertCurrentDate, InsertCreateDate, InsertModifiedDate, RenamePrev, RenameNext.
- Text and Scroll: Filey handles these keys inside its text boxes and lists without commands; the reference lists each (move/extend by letter, word, home, end, delete, case, undo/redo, autocomplete; select/extend/scroll/cursor moves, center camera) so they can be rebound.

## From the reference's roadmap

Shipped there, missing here:

- Tree view: folders as a classic expandable hierarchy.
- Group by: folder items in groups by date, type or size.
- Hierarchical folder views: a locked view that applies to a whole folder tree, not one folder.
- Drag and drop from ZIP archives and browsers (virtual files, `CFSTR_FILEDESCRIPTOR`/`CFSTR_FILECONTENTS`), and drops onto executables and scripts that run them with the files.
- Bookmarks and recents on offline drives kept and shown until the drive is back.
- Network shares listed in the sidebar.

Planned there, worth doing here first:

- Redo, beside the existing Undo.
- Miller columns: each opened folder adds a column to the right. Decided: its own view toggle (a command with a hotkey, saved per panel), outside the zoom ladder, so Ctrl+wheel steps the other views as now; the column right of the shown folder lists the focused folder's contents and stays empty for a file, whose preview stays in the inspector.
- Multiline names in the icon views, and larger thumbnails that use the grid's room.
- Command-line arguments beyond folders (open in a new tab of the running window).
- Virtual folders (Recycle Bin, Control Panel and other shell namespaces) listed like real ones, and MTP devices (phones, cameras) on both platforms.
- Custom Quick Access groups: created, renamed and ordered by the user.
- Rebindable mouse buttons and actions, with the hotkeys.
- Names in any script: check font fallback for CJK, Arabic, Hebrew and emoji on both platforms.
- Indexing the drives from the NTFS file table (Windows) for a faster Go To walk.

## Filey's own

- Git: detect a repository around the shown folder and show in the path bar how far its branch is from the remote (↑ commits to push, ↓ to pull). Read `.git` directly or run `git` in a background job; never block the UI on it.
- A hex view in the inspector for binary files, in the spirit of ImHex (https://github.com/WerWolv/ImHex): offsets, hex bytes and their characters side by side, read in pages from the file so that big files open at once.
- Syntax highlighting, at least basic (keywords, strings, comments, numbers), in the inspector's text view for code files.
- Localization, low priority: the interface in other languages. Until then, keep user-facing strings in few places (Commands.txt labels, the option table) so they can be translated later.

## Checks

- Compare rename, the context menu, drag and drop, the preview pane and the settings page side by side.
