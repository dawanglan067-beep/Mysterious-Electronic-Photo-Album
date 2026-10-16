# Photo wall and PDF regression checks

Configure with `-DALBUM_BUILD_FEATURE_TESTS=ON`, build, then run
`ctest --test-dir build-msvc --output-on-failure` in your Qt/MSVC environment.
The option defaults to OFF for normal application builds.

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
path and cancelling. Run this executable separately from the offscreen suite.
For manual acceptance, open an album and import a folder: images should be
visible, double-clicking an image must not submit it as an album, and
"选择当前文件夹" must select the directory shown in the address bar.
