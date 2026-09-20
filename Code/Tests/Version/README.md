# Version parser tests

This suite is independent of the UI and other service test runners. It uses Qt 5.15.2, C++17, QtCore, and QtTest only. The generated PE bytes are never executed or passed to an operating system loader.

`pefixtures.h` provides `PeFixtures::image(bool pe32Plus = false, bool withVersions = true, bool missingFields = false)`. Both PE32 and PE32+ contain three sections, named and ordinal imports, named and ordinal-only exports, and a forwarded export. Version fixtures include resource languages `0409` and `0804`, two StringTables per leaf, arbitrary string fields, fixed version fields, and language/codepage translations. `missingFields` omits selected English strings while preserving other language fields. Offsets and endian-safe mutation helpers are exposed for UI tests and precise malformed-data cases.

The fixture layout follows the Microsoft [PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format), [VS_VERSIONINFO](https://learn.microsoft.com/en-us/windows/win32/menurc/vs-versioninfo), [String](https://learn.microsoft.com/en-us/windows/win32/menurc/string-str), and [Var](https://learn.microsoft.com/en-us/windows/win32/menurc/var-str) documentation. It is structural test data, not a runnable Windows application.

## Run independently

From a dedicated build directory such as `build/VersionTests-night`:

```sh
/Users/loren/Qt/5.15.2/clang_64/bin/qmake ../../Code/Tests/Version/VersionTests.pro
make -j2
codesign -f -s - bin/tst_version # macOS host requirement
./bin/tst_version -o version-tests.txt,txt
```

On other platforms, use the installed Qt 5.15.2 qmake and omit the macOS signing command.

## Recorded verification

The final independent rebuild and test executable ran on macOS 26.6 with Qt 5.15.2 x86_64: **74 passed, 0 failed, 0 skipped**, 391 ms. The report is `build/VersionTests-night/version-tests.txt`. qmake reported that SDK 26.5 is newer than Qt 5.15's tested SDK; compilation and execution succeeded with no project source warnings.

Coverage includes:

- PE32/PE32+ headers, section fields, imports by name and ordinal, named and ordinal-only exports, forwarding, export aliases, and import-address-table fallback.
- Multiple resource languages and StringTables, fixed fields, translations, non-ASCII UTF-16 strings, arbitrary fields, missing fields, named resources, and missing version resources.
- Non-PE byte inputs and regular-file size/time/host metadata; inspecting a read-only file preserves its contents, modification timestamp, and permissions.
- Five configured resource limits and 48 malformed-data cases covering truncated headers/sections, integer overflow, unmapped RVAs, out-of-range counts, import thunks, section-bounded strings, forwarder bounds, resource cycles, version lengths, fixed signatures, and nonzero documented key Padding.
- Separate acceptance cases preserve compatibility for nonzero alignment bytes between adjacent String structures and after the version root. The latter also checks that absent fixed version information remains absent.
- Every truncated prefix from 2 bytes through file-size-minus-one for both fixture formats: 12,284 inputs. A fixed-seed mutation smoke test covers another 512 inputs without nondeterministic corpus dependencies.

Windows/MinGW and Linux builds/runs, Windows loader acceptance, and comparison UI integration are **not verified by this suite's recorded run**. This suite does not claim complete VER-001 through VER-005 product acceptance, semantic version comparison coverage, or batch/export functionality.
