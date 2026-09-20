# Archive view behavior tests

This standalone Qt Widgets suite uses the ZIP fixtures in `../Archive/fixtures`.
The fixture generator and manifest are maintained by the Archive service suite.

```sh
mkdir -p /tmp/lqcompare-archiveview-build
cd /tmp/lqcompare-archiveview-build
~/Qt/5.15.2/clang_64/bin/qmake /Users/loren/Desktop/Work/LqCompare/Code/Tests/ArchiveView/ArchiveViewTests.pro
make -j2
QT_QPA_PLATFORM=offscreen ./bin/tst_archiveview
```

Set `LQCOMPARE_ARCHIVE_SCREENSHOT_PATH` to an absolute PNG path to capture the
paired metadata view in `opensPairedMetadataReadOnly`.

Verified on macOS with Qt 5.15.2: 15 cases passed, including data-driven failures,
the real CRC collision pair, read-only save enforcement, paths and reload,
preserving a previous comparison after failed replacement/reload, initial-open
recovery, late view construction, close cleanup, filtering and navigation, and
a 50,000-row model with numeric sorting and search.

The view performs bounded synchronous directory/header reads through
`Archive::readZip`; it does not decompress, extract, verify payload CRCs, or
claim byte equality. The flat table displays normalized member paths including
implicit directories. Archive writes, content comparison, recursive nested
archives, extraction, background cancellation, and Windows execution are not
implemented or verified by this suite.
