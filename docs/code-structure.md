# Code structure

- ProTreeWidget: album tree, open/import/close lifecycle, selection, favorites,
  filtering, progress dialogs and duplicate-cleanup confirmation. Existing music
  methods forward to MusicController, preserving MainWindow and SlideShowDlg callers.
- MusicController: music state, MCI playback, Qt fallback, volume, playback policies
  and audio-file selection. Owns Qt audio objects; SettingsDirty requests persistence.
- MusicSettingsDialog: modal music settings UI and its scoped connections.
- AudioFileProbe: file-header detection and audio validation without tree UI.
- WorkspaceSettings: QSettings snapshots with existing key names. Callers supply
  the organization so tests can isolate preferences. The tree coordinates restore.
- DuplicatePhotoFinder: dHash, Hamming distance and matching within album groups.
  Returns indexes; file deletion and tree ownership remain in the calling UI.
- PicShow: photo loading, viewport interaction, transformations and studio UI.
- ImageProcessor: filters and color adjustments with explicit QImage parameters.
- HistogramWidget: histogram sampling and drawing independent of PicShow.

New headers and sources are listed in CMake and inherited by feature tests.
See tests/README.md for verification. Local extraction scripts in build-msvc
are build artifacts and are excluded from version control.
