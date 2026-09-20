# Registry session and view tests

This independent Qt 5.15.2 / C++17 suite exercises `RegistryCompareSession` and
`RegistryCompareView`, linking only the registry service and session base. It does
not run the repository's full test runner and never writes to the registry.

From this directory on the current macOS development machine:

```sh
mkdir -p .build
cd .build
/Users/loren/Qt/5.15.2/clang_64/bin/qmake ../RegistryViewTests.pro
make -j2
QT_QPA_PLATFORM=offscreen \
LQCOMPARE_REGISTRY_SCREENSHOT="$PWD/registry-tree.png" \
./bin/tst_registryview -txt -o test-results.txt,txt
cat test-results.txt
```

For other platforms, use the installed Qt 5.15.2 `qmake` and the matching build
tool; the executable may have the `.exe` suffix. The screenshot environment
variable is optional. Test output and images stay under the ignored `.build/`.

The ten behavior tests cover:

- Empty sessions never enumerate local registry data.
- Real temporary UTF-16 exports with Unicode paths/names load and remain unchanged
  after attempted save operations; even setting the base dirty flag cannot enable save.
- Failed replacement, reload, and read-option changes preserve both prior snapshots,
  source paths, title, status, and comparison results.
- Legacy ANSI fallback is explicit and changing it rereads both sides atomically.
- An injected `MemoryProvider` models unreadable subtrees; the session and tree keep
  them unknown and do not report equality. Live source inputs outside HKCU are rejected
  before provider invocation.
- Native entry buttons follow platform availability and carry its explanation.
- Keys form a hierarchy, values show original types and formatted data, status filters
  retain ancestors, and status sorting works in both directions. Widths include
  collapsed descendant labels.
- Delete instructions remain visibly inert; deleted values are not mislabeled REG_SZ.
- Source controls display failures while preserving the previous result and recover
  on successful comparison.
- Cached view ownership, recreation, session destruction, and closed-session state
  are safe.

## Verification boundary

The macOS run performs real file parsing, session, and offscreen Qt widget tests.
It deliberately constructs the registry view directly for those tests; this does
not change the product's Windows-only session factory/Home availability policy.
The memory provider is a simulation of registry enumeration, not Windows validation.
No test in this suite creates, imports, repairs, exports, or deletes live keys.

Windows native enumeration, permissions, Unicode Win32 API behavior, and the
Windows application entry still require execution on Windows. Large sources use
the service's limits but are read and displayed synchronously; responsiveness and
cancellation for large snapshots are not claimed by this suite.

The development machine uses macOS SDK 26.5; qmake warns that Qt 5.15.2 was tested
with SDK 10.15. The warning is preserved, not suppressed.
