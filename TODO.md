# TODO

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

Already in Filey under another name: SelectionActivate (OpenFocused), SelectionCopy/Cut/Paste, SelectionTrash, SelectionDelete, SelectionDuplicate, SelectionRename, SelectionCopyAsPath, CreateFile, CreateFolder, ToggleExpandFolder, StartFileFilter, StartExpandedSearch, RefreshFolder, MoveFolderBack/Forward/Up, BookmarksToggle, BookmarksClear, RecentsClear, ToggleShowDirectories/Files, ZoomIn/OutContent, View Details/List/Columns/Medium/Large/XL Icons, EditPath, OpenInTab, the Tab commands but those below, TabSearch (TabList), PanelSplitRight/Bottom, PanelSelectNext/Prev/Left/Right/Up/Down, GoToOpen, CommandsOpen, QuickAccessOpen, ContextMenuOpen, ToggleHiddenFiles, ScrollSelectAll, InspectorToggle (TogglePreview), OptionsOpen (OpenSettings), PanelFilterApply2, PanelFilterCancel, GoToMoveBack/Forward/Up. Filey only: ToggleFilterPin, Undo, ToggleDebugTheme, ToggleTransparency, NewWindow.

Missing, by the reference's group:

- Palette: CmdWindowToggleDetails (the button next to close), QuickCommandsOpen (Quick), MenuOpen.
- Folder: ToggleCursorSelection, OpenInRightSplit, OpenInBottomSplit, OpenInWindow, SelectionPasteLink, SelectionCopy/MovePrev/NextPanel, SelectionInspect, SelectionRestore, SelectionCopyAsPathSmart, OpenRelativePath(InTab), OpenContainingPath(InTab), CalcDirsContents, TrashDeleteAll, TrashRestoreAll, FolderViewToggle, FilesPopupOpen/Prev/Next, TypesPopupOpen, PanelHeaderDirInTab, OpenPathOptionsPopup.
- View: ToggleSepFilesFolders, SizeAllHeaderColumnsFit, HeaderColumnsOpenOptions, Increase/Decrease/ResetFontSize, Increase/DecreaseSpacing, ToggleFileExtension, ToggleSystemFiles, ToggleRecents, ToggleSortRecents.
- Sort: DateDeleted and OriginalLocation (with a Recycle Bin view), FreeSpace, FileSystem and PercentFull (with a drives view), VolumeUsage and FolderUsage (with the usage columns). Dates sort by whole seconds; the reference keeps finer times, so items saved in the same second can come out in another order.
- Inspector: ToggleImageGrid, ToggleImageMinimap, ToggleImageScale, ScaleImageUp/Down. Its text view reads the first 1 MB, has no scroll arrows, and the reference's folder previews list the folder.
- Panels: PanelEvenSiblings, PanelEvenAll.
- Tabs: OpenTabOptionsPopup, TabOpenInWindow, TabCloseAll, TabMenu.
- Go To: GoToSelectInPanel.
- Misc: ExplorerOpen, ConsoleOpen, MinimizeWindow, ToggleFullscreen, CloseWindow, LayoutsSave/Load, FolderViewsClear, ConfigCreate/Open, the Quick Access filter, viewport and group options. Install is the reference's own setup and stays out.
- Rename: InsertIncrementalID, InsertCurrentDate, InsertCreateDate, InsertModifiedDate, RenamePrev, RenameNext.
- Text and Scroll: Filey handles these keys inside its text boxes and lists without commands; the reference lists each (move/extend by letter, word, home, end, delete, case, undo/redo, autocomplete; select/extend/scroll/cursor moves, center camera) so they can be rebound.

## Checks

- Compare rename, the context menu, drag and drop, the preview pane and the settings page side by side.
