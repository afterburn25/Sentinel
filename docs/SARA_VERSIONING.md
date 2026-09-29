# SARA Versioning

SARA uses semantic-style `MAJOR.MINOR.PATCH` application versions.

## Canonical source

The repository-root `VERSION` file is the canonical shipped application version.

CMake reads `VERSION`, the Windows executable version resource is generated from it, GitHub Actions names package artifacts from it, and the Inno Setup build receives the same version through the version-aware installer helper.

## Recovery baseline

- `1.0.15` is the protected recovery baseline.
- The branch name `sara-recovery-from-1.0.15` continues to identify that ancestry and does **not** mean current builds must remain version 1.0.15.
- Baseline guards, checkpoint references, immutable commit references, and recovery-tool filenames may continue to say 1.0.15 where they specifically refer to that baseline.

## Feature-release rule

For the current recovery/development line:

- Increment **PATCH** for each validated shipped feature slice or meaningful user-visible behavior change.
- Increment **MINOR** for a larger coordinated milestone that changes several major modules or establishes a new product phase.
- Increment **MAJOR** only for an intentional compatibility/product-generation break.

Examples:

- 1.0.15 — protected recovery baseline
- 1.0.16 — automatic `sentinel-chat` startup / current versioned recovery release
- 1.0.17 — next validated feature slice
- 1.1.0 — future larger milestone

A version bump should happen before the feature slice is treated as the new distributable release checkpoint.
