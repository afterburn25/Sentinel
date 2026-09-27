# Development Continuity Policy

GitHub is the canonical source of truth for SARA development.

## Required documentation behavior

For every meaningful development conversation:

1. Record new product requirements in `docs/SARA_PROJECT_STATE.md` or the relevant design document.
2. Add unimplemented work to `docs/SARA_ROADMAP.md`.
3. When implemented, update the roadmap checkbox/status and document the implementation.
4. Commit code and documentation together whenever practical.
5. Record release-level behavior changes in release notes/changelog.
6. Record architecture changes in `docs/architecture/`.
7. Preserve compatibility/migration notes for settings, model registries, datasets, personas, and adapters.
8. Do not rely on chat memory as the only record of a decision.

## Session recovery

At the start of a future SARA development session, recover context in this order:

1. `docs/SARA_PROJECT_STATE.md`
2. `docs/SARA_ROADMAP.md`
3. recent Git commits / pull requests
4. relevant files in `docs/architecture/`
5. current source and tests

## Naming

SARA — Synthetic Adaptive Response Agent is the canonical product name.

Historical Sentinel naming may remain in repository paths, namespaces, database folders, or build targets until migration work explicitly replaces it.


## UI verification requirement

For SARA UI work, code review is insufficient by itself.

Before considering a UI change complete:
1. compile/package the real Windows application,
2. launch the packaged EXE in CI,
3. capture the splash and all Model Lab workspaces,
4. upload those PNGs as build artifacts,
5. inspect the actual captures for overlap, clipping, encoding errors, missing assets, wrong versions, and visual regressions,
6. for installer work, launch and capture the compiled SARA Setup wizard too.

Future sessions should use actual-build screenshots as the primary visual source of truth.
