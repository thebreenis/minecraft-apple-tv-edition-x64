# Builder validation — 2026-11-01

Tested locally on Windows 11 x64 with both supported input archives.

- Self-contained EXE payload self-test: passed, including the bundled Microsoft framework hashes and authored payload hashes.
- EXE `--verify-build` path: completed extraction, checked patches, Apple TV artwork conversion, public Minecoin artwork download, APPX packing, local signing and output generation; exit code 0.
- Unsupported TIPA: rejected with exit code 1 before output creation or installation.
- Reconstructor tests: valid edit, wrong input/output hashes, overlapping edits and archive path traversal checks passed.
- PowerShell scripts parsed without errors.
- Generated Install.ps1 `-CheckOnly`: all output hashes passed.
- GUI inspected at runtime: both input controls, build-only mode, stage/progress/log area and buttons fit without clipping.
- Per-build extracted staging directory was removed after completion.
- No game installed, no account switched, no Xbox session cleared, no purchase submitted during builder validation.

## Comparison with the validated .210 project

The generated Minecraft.Windows.exe, XINPUTATV.dll, resources.pri, manifest and patched UI definitions match the .210 source payload. The packaged launcher was rebuilt from the same Python source without the embedded game icon. All five downloaded Minecoin images match the existing images pixel-for-pixel; PNG encoding may differ.

Intentional packaging differences: locally generated signatures; square tile art reconstructed from the supplied TIPA; omission of the unused old XInputUap.dll; neutral ownership flags in the public catalog seed. Runtime entitlement queries determine the recipient's ownership.

This verifies reconstruction and build-only operation on the development PC. A fresh-PC Build & Install run, missing-SDK automatic setup, and a fresh-account gameplay pass have not been completed. The game-level .210 testing and three achievement unlocks predate the builder. Do not describe those as fresh unlock tests of the generated package.

## Source-only rebuild

The source archive was extracted separately. After correcting the helper script's single-argument array handling, Build-Helpers.ps1 rebuilt the Python helpers and native DLL, and Publish.ps1 produced a self-contained GUI EXE. Its payload self-test passed. The rebuilt native DLL's .text and .data sections match the .210 DLL byte-for-byte. This used the development PC's installed Python/compiler/SDK toolchains; it did not reuse unpublished source files.


## 1.0.1 installer repair (2026-11-09)

- Updated transfer installer: trust matching certificate, install APPX/frameworks, then configure loopback. Errors are saved to phase-specific logs.
- Successful reinstall after uninstall on the development PC: .210 registered with Status Ok, native bridge present, loopback family listed. This used the repaired original .210 transfer package.
- Published builder self-test: passed, version 1.0.1, 170 embedded files and two framework packages verified.
- Full build-only reconstruction from both original inputs: exit 0; generated transfer installer CheckOnly passed in Windows PowerShell 5.1. No game installed by that reconstruction test.
- Unchanged helper binaries reused only after matching every binary against the preceding tracked payload hashes. Frontend rebuilt with .NET 8.
- A separate clean PC installation and fresh-account gameplay remain untested.

## 1.0.2 / .212 account metadata fix

- Affected-PC report: all 11 installation checks passed; Xbox/Store account IDs matched; XUID present; only Gamertag registry value missing. Startup trace confirmed rejection before bridge authentication.
- Removed only the optional display-name requirement. Six regression tests pass, covering missing registry Gamertag, correct/incorrect bridge identity, mismatched Windows accounts, missing stable IDs and existing named accounts.
- Windows PowerShell 5.1 diagnostic collector: 11 checks passed, zero collection errors locally. Structured logging tests covered redaction, rotation, deduplication and logging-failure isolation. Frozen launcher bootstrap test passed without initiating authentication.
- Full .212 build-only reconstruction exited 0. Generated installer CheckOnly passed; manifest revision and embedded launcher hash verified.
- User confirmed .212 works completely on the previously failing account on the affected PC (2026-11-09). This is not a fresh achievement or multiplayer validation.
