# TODO

## Go To and the drive index

- List several folders at once during the walk. The first walk is limited by listing one folder at a time; parallel slices need their writes to `Index.bin.walk` kept in the order the entries join the index.
- Pack the index's names into one buffer instead of a `String` per entry. A full walk of C: takes about 320 MB, mostly names and their headers.
- The typed letters in a highlighted search row are drawn in the highlight's own blue, so they disappear.

## Look

- Text is 2-4 px wider over a word than the reference's.
- The tab outline is 2 px; the reference's is about 1.5 px.
- The tab close × comes from the icon font and is softer than the reference's.

## Checks

- Compare rename, the context menu, drag and drop, the preview pane and the settings page side by side.
