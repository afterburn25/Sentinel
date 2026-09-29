# SARA — Synthetic Adaptive Response Agent

This repository contains the native Windows SARA application and its protected recovery/development line.

## Implemented in this scaffold

- C++20/CMake project layout
- Strong UUID domain identifiers
- SecureBuffer with zeroization
- Windows CNG secure RNG, SHA-256 and AES-256-GCM interfaces/implementation
- Windows DPAPI secret protector
- SQLite database wrapper and migration runner
- Case repository/service baseline
- Hash-chained audit ledger baseline
- SEV1 evidence container writer, authenticated decryptor, and structural verifier
- Evidence import staging/finalization baseline
- SentinelEvidenceVerifier utility
- Initial automated core tests

## Important status

This is an **engineering bootstrap**, not a production-ready evidence system yet. In particular:

- Case fields in migration 0002 are temporarily plaintext while the encrypted field mapper/key manager is completed.
- SEV1 currently buffers payloads and explicitly rejects files over 256 MiB; chunked streaming encryption is the next milestone and is required before large evidence files are supported.
- Evidence persistence and crash-recovery state transitions are not complete yet.
- WinUI 3 shell is not included in this first core drop.

These limitations are intentionally explicit so unfinished security properties are never mistaken for completed ones.

## GitHub Actions build

The repository includes `.github/workflows/windows-build.yml`. Every supported push, pull request, and `v*` release tag builds SARA on GitHub's Windows/MSVC runner, runs CTest, installs the binaries into a package staging directory, generates `SHA256SUMS.txt`, builds the Inno Setup installer, and uploads versioned `SARA-<VERSION>-windows-x64` and `SARA-Setup-<VERSION>` artifacts.

The workflow uses `vcpkg.json` to provide SQLite3 and `CMakePresets.json` for the Windows x64 Release configuration.

On GitHub, open **Actions → Windows Build** to see compiler/test results or manually run the workflow. Successful runs expose the downloadable build under **Artifacts**.

## Windows prerequisites

- Visual Studio 2022+
- Desktop development with C++
- CMake 3.24+
- SQLite3 development package available to CMake

## Configure

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Run bootstrap CLI

```powershell
.\build\Debug\SentinelCli.exe .\sentinel-data
```

## Version

- Current application version: **1.0.16** (canonical value in `VERSION`)
- Protected recovery baseline: **1.0.15**
- Version policy: see `docs/SARA_VERSIONING.md`
- Evidence container: SEV1
- Audit canonicalization: audit-v2 with legacy audit-v1 verification compatibility
