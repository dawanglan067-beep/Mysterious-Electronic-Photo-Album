# Photo wall and PDF regression checks

Configure with `-DALBUM_BUILD_FEATURE_TESTS=ON`, build, then run
`ctest --test-dir build-msvc --output-on-failure` in your Qt/MSVC environment.
The option defaults to OFF for normal application builds.

Editor history checks cover slider gesture grouping, keyboard shortcuts with slider
focus, filter/reset undo, rotation/mirroring pixel restoration, redo branch removal,
and history isolation on switching/clearing/failed loading.

Module checks cover image processing defaults/inversion/transparency, duplicate
group boundaries, audio format mismatch detection, settings round trips, music
volume/policy persistence and the music settings dialog.

The test executable accepts an optional output directory to retain screenshots
and PDFs. Set `QT_QPA_PLATFORM=offscreen` and `QT_PLUGIN_PATH` to the matching
Qt installation's `plugins` directory when running it directly.

Coverage includes gallery selection order, filtering/deletion, empty albums,
thumbnail cancellation/destruction, tree search/favorites and viewer navigation;
PDF layouts, Chinese captions, portrait/landscape images, cancellation, failed
reads, invalid destinations, atomic replacement and source overwrite protection.

Integration tests compile a tree implementation copy with a separate
`AlbumFeatureTests` QSettings organization, preserving real user preferences.

Manual acceptance: open/import an album, select with Ctrl/Shift, double click
to view, return using the photo-wall toolbar action, edit export captions,
export each layout, and cancel a long export. Export reads saved disk files;
unsaved single-photo adjustments are not included.

Windows native folder browser checks: run `AlbumNativePickerTests.exe` from
the build directory with the matching Qt runtime on PATH and the Windows
platform plugin (do not set `QT_QPA_PLATFORM=offscreen`). This briefly opens
real Windows dialogs and verifies selecting a current folder with a Chinese
path, selecting a highlighted child folder instead of its parent, disabling
confirmation when an image is selected, and cancelling. UI Automation selects
the actual shell items. Run this executable separately from the offscreen suite.
For manual acceptance, open an album and import a folder: images should be
visible, double-clicking an image must not submit it as an album, and
"选择此文件夹" must select the highlighted folder. With no selected item,
"选择当前文件夹" must select the directory shown in the address bar.

Modification-state checks compare edit snapshots to the load baseline independently
of undo indexes. Crop checks cover source-space pixel mapping for rotations/mirrors,
repeated crops, alpha preservation, handle dragging, aspect constraints, zoom/pan,
and confirm/cancel history behavior. crop-dialog.png is retained when an explicit
output directory is supplied. High DPI can be checked with QT_SCALE_FACTOR=2.

Original comparison checks hold/release source pixels after color, geometry and crop
edits; restore zoomed view; preserve history and modification state; and clear the
comparison on source switch, failed load, clear, hide and window deactivation.

Restore-original checks combine crop, rotation, both mirrors, all color sliders
and a filter; verify one history step, original preview pixels, clean modification
state, control synchronization, undo/redo, repeated restore without extra history,
source file preservation, and disabling the action after clear or failed loading.

Crop canvas uses left drag on handles to resize, inside the selection to move it,
and outside the selection to pan. Space + left drag, middle drag and right drag
also pan. Checks cover starting in the shaded area or outside the image, crossing
the selection during a pan, releasing Space first, and recovering after focus loss.
