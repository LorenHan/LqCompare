# CLI behavior tests

`CliTests.pro` links QtCore, the comparison services, and the CLI service directly.
Process tests start the test executable itself with `LQCOMPARE_CLI_PROBE_MODE=1`,
which routes its main function into the CLI API before QtTest starts. This probe
uses `QCoreApplication` and cannot create Qt widgets. Process tests remove
DISPLAY/WAYLAND_DISPLAY and set the offscreen platform before invoking it.

Build and run from the repository root; no separate probe build is required:

```sh
mkdir -p build-cli-tests
cd build-cli-tests
~/Qt/5.15.2/clang_64/bin/qmake ../Code/Tests/Cli/CliTests.pro
make -j2
./bin/tst_cli
```

`LQCOMPARE_CLI_PROBE` can override the executable location for manual checks. The
optional standalone probe project is `../CliProbe/Standalone/CliProbe.pro`; it
shares the probe entry function and sits outside the automatic QtTest project
discovery pattern.

Coverage includes native and named paths; Windows/Posix option disambiguation;
aliases, missing/duplicate/conflicting values; generated help; text comparison
rules and encoding errors; recursive directory comparisons; TXT/CSV/JSON/HTML
reports; machine-output escaping; explicit logging; output collisions with
inputs, directory trees, symlinks and Unix hard links; real process exit codes
0/1/2/3; and script execution with relative paths, JSON reports and line-numbered
errors. Exit code 4 is checked through the public error-result API because it is
reserved for internal exceptions rather than normal user inputs.

The process probe configures a dedicated INI settings root when the test supplies
`LQCOMPARE_CLI_TEST_CONFIG_HOME`. The settings/input preservation test snapshots
all fixture files before invoking CLI options and compares bytes afterward.

The suite does not certify application-level single-instance IPC, GUI read-only
presentation, `--wait` completion, Windows wildcard expansion, or native Windows
process behavior. Those need their owning module/application tests. Comprehensive
script grammar and command behavior coverage remains in the Script test suite.
