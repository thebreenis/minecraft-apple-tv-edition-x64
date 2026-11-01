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

