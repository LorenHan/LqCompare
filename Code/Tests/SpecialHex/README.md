# HEX regression tests

Build only this suite with Qt 5.15.2 and at most two compiler processes:

```sh
mkdir -p /tmp/lqcompare-special-hex-build
cd /tmp/lqcompare-special-hex-build
/Users/loren/Qt/5.15.2/clang_64/bin/qmake /Users/loren/Desktop/Work/LqCompare/Code/Tests/SpecialHex/SpecialHexTests.pro
make -j2
QT_QPA_PLATFORM=offscreen LQCOMPARE_HEX_SCREENSHOT=/tmp/lqcompare-special-hex.png ./bin/tst_specialhex
```

Fixtures are generated in private temporary directories and removed after each
case. The suite builds the HEX service, HEX session/view and the two minimal
session base classes; it does not build picture comparison or run other suites.
The screenshot defaults to the platform temporary directory when
`LQCOMPARE_HEX_SCREENSHOT` is not set.

Coverage includes all 256 byte values, explicit absolute-offset insertion and
deletion examples, empty/asymmetric files, bitmap and 1 MiB scan boundaries,
48 deterministic randomized fixtures checked against an independent region
oracle, nonwrapping navigation including integer extremes, bounded reads,
snapshot isolation from changed/deleted sources, transactional failed loads,
offset parser rejection/overflow, readonly session behavior, failed replacement
and reload state, empty-session opening, keyboard/scroll navigation, 8/16/32/64
row settings, narrow-pane horizontal reveal, and view/session destruction order.

The 100 MiB case loads two files, asserts a 12.5 MiB bitmap, traverses widely
separated differences within five seconds, renders an actual view and jumps to
the final byte. A separate sparse 513 MiB fixture checks early rejection on
either side. Bitmap size and maximum page size are asserted; whole-process RSS
and the exact 512 MiB maximum are not measured. The suite currently has only
been executed on macOS with the offscreen Qt platform; Windows remains unverified.
