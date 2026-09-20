# Media metadata service regression

The suite builds the QtCore-only media service directly, generates its small
fixtures in temporary files, and requires no audio device, network access,
third-party tag library, or playable recording. `fixtures.h` provides reusable
ID3v1/v2 and native FLAC metadata constructors. Synthetic MPEG frame bytes are
only a format-identification fixture; these tests do not validate audio content.

Run independently from the repository root:

```sh
mkdir -p Code/Tests/Media/build
cd Code/Tests/Media/build
/Users/loren/Qt/5.15.2/clang_64/bin/qmake ../MediaTests.pro
make -j2
./bin/tst_media -o results.txt,txt
```

Use the local Qt 5.15.2 qmake path on other machines. The service tests do not
include or exercise the full application runner or any other team's tests.

Coverage:

- ID3v1/v1.1 Latin-1 fields, track, numeric genre, ID3v2 precedence with retained
  conflicting v1 values, and combined resource limits.
- ID3v2.2, v2.3, v2.4 frame lengths; Chinese and supplementary Unicode in UTF-8,
  UTF-16 with BOM, and UTF-16BE; ordered multivalues; comment languages and user
  text descriptions. UTF-16 COMM/TXXX fixtures cover empty descriptions with
  independently marked text, inherited description byte order, and rejection of
  missing or inconsistent BOMs within the same frame.
- Footer, grouping, data length indicators, unsynchronisation, extended header,
  padding; artwork, compressed/encrypted frames, update and unverified CRC
  markers remain `Partial` and cannot yield a complete equality result.
- Native FLAC STREAMINFO, repeated case-insensitive Vorbis keys, unknown keys,
  Chinese text, and technical fields. Picture blocks remain `Partial`.
- Malformed syncsafe values, frame/block truncation, invalid encodings, lengths,
  counts, flags, field names, duplicate FLAC blocks, and trailing garbage.
- Bounded tag bytes, field bytes, frame/comment/value counts and metadata blocks.
- Unsupported containers and empty/nonmedia input, missing files, field-level
  comparisons, ignored technical differences, and failed/partial completeness.
- Read-only fixtures remain byte-for-byte identical with unchanged modification
  time and owner write permission after repeated load and comparison, including
  corrupt and unsupported input.
- 256 reproducible, seeded truncation and byte-mutation samples assert read-only
  preservation and that rejected input discards all parsed/stale fields. This is
  a bounded regression sample, not exhaustive fuzzing or a performance benchmark.

Verified on 2026-09-20: QtTest 5.15.2 / Qt 5.15.2 x86_64, macOS 26.6;
**378 passed, 0 failed, 0 skipped** (including 256 mutation/truncation data rows).
qmake emitted its existing warning that the
macOS 26.5 SDK is newer than the SDK tested with this Qt release; compilation and
execution nevertheless succeeded. Windows and Linux execution remain unverified.

This suite verifies the service only. It does not claim UI rendering consistency,
playback, waveform, audio equality, metadata writing/round-trip support, other
container support, or complete MED-001 through MED-006 acceptance. The view has
separate tests, and tag editing is outside the current read-only implementation.
