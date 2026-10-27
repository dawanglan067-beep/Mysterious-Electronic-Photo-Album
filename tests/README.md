# Photo wall and PDF regression checks

Configure with `-DALBUM_BUILD_FEATURE_TESTS=ON`, build, then run
`ctest --test-dir build-msvc --output-on-failure` in your Qt/MSVC environment.
The option defaults to OFF for normal application builds.

Material saving checks cover numbered PNG paths, two independent crop regions,
color/alpha pixels, cancel, declined replacement, failed writing, source and history
preservation, optional album notifications, integrated album context and photo-wall
refresh, path deduplication, external file copying with collision avoidance,
navigation links and reopening the album. An explicit output directory retains
material-save-dialog.png. The suite also runs at QT_SCALE_FACTOR=2.
Manual acceptance: crop a photo, click "另存为素材…", browse to a new location,
save with and without "同时加入当前相册", then undo the crop and save another
region. External files added to an album are copied into its "素材" directory.
Saving inside the album is discovered on a later rescan even if not registered now.

Photo/material deletion checks move only newly created temporary fixture files to
the actual OS recycle bin. They cover album containment, missing/unregistered
targets, symbolic links, external file preservation (including Windows hard links),
confirmation and dirty-editor cancellation, cancelled rescue exports, Windows file
locks that prevent recycling, full-resolution rescue export, rejection of rescue
paths scheduled for the same batch deletion, and partial batch failures. Integrated
checks exercise the tree's actual right-click action and the wall selection button,
favorites/list/cache refresh, navigation repair, clearing the last photo, and reopening
the album without rediscovering deleted files. Both regular and QT_SCALE_FACTOR=2
offscreen runs pass. An explicit output directory retains material-delete-confirmation.png.

Manual deletion acceptance: select wall photos and click "删除所选（回收站）", or
right-click a tree photo and choose "删除图片／素材（移到回收站）". Check the paths
before confirming. With an edited target, try cancel, export-copy-then-delete, and
continue-without-export. Cancel or export failure must stop deletion; recycling
failure must preserve the photo, row and edit history. On success, reopen the album
and verify absence, then restore via Windows Recycle Bin and reopen to rediscover.
For external materials, verify only the album copy disappears. Test files on each
supported storage device; unavailable recycling must never fall back to permanent
deletion. Ctrl+Z restores image edits, not recycled files. This delete-specific
prompt does not implement the planned general switch/close reminders or successful
export-state tracking (A5).

Folder management checks cover automatic hiding of a genuinely empty material
directory while retaining it on disk; hidden/non-image files preventing false empty
classification; actual tree context menus for independent hide, root restore and
folder recycling; preservation of dirty edits during hiding and cancellation;
registering an existing material restoring its hidden folder; rejecting overlapping
opened albums and external aliases; locked-child recycle failure; rejecting rescue
exports inside the directory; full edited rescue export; subtree removal, reopening
and material-directory recreation; and recycling an empty directory independently.
Screenshots: material-folder-menu.png and material-folder-delete-confirmation.png.

Manual folder acceptance: delete the last material and verify the empty material
node disappears while its empty physical directory remains. At the album root,
choose "显示已隐藏文件夹（含空素材目录）" to show it again. A directory's own
context menu provides "隐藏文件夹（本次运行）" and "删除文件夹（移到回收站）…".
Hiding preserves files and current edits and is reset on application restart;
search/favorites filters still apply. Explicitly showing empty folders keeps them
available during the current run. Recycling a folder includes hidden/non-image
files and subdirectories; inspect the confirmation, cancel it, test dirty-editor
choices, then confirm on test data and verify reopening does not recreate it.
Restore the directory via Windows Recycle Bin and reopen to check recovery. Saving
another material should recreate or unhide the folder without closing its album.

Image export checks cover full-resolution document rendering beyond the drag-preview
limit, crop/rotation/mirrors/colors, PNG pixel and alpha preservation, JPEG quality
and opaque backgrounds, BMP, replacing existing outputs, source path aliases,
invalid formats/options/images/destinations and preservation of existing files.
Manual export acceptance: select PNG/JPEG/BMP in the actual save dialog, omit an
extension, cancel file/quality selection, confirm replacement of an existing output,
and attempt to export onto the source. JPEG transparent areas use white in the UI.
Successful export preserves content modification state; export-state tracking and
leave reminders belong to the next stage.

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
