# SARA 1.0.16 — Synthetic Adaptive Response Agent

SARA is a native Windows C++20 application for local AI simulation, persona management, conversational training, model evaluation, deployment, evidence preservation, and controlled operations workflows.

The repository name and some internal namespaces still use **Sentinel** for compatibility with the original codebase and existing data. The canonical product name is **SARA — Synthetic Adaptive Response Agent**.

## SARA 1.0.16 highlights

### Model Lab / Trainer
- Hybrid Model Lab UI with dedicated **Overview, Train, Datasets, Personas & LoRAs, Foundation Forks, Jobs, Evaluation, and Deployment** workspaces.
- Conversational Trainer with correction/instruction capture.
- Training modes:
  - Behavior Tuning
  - Dataset Training
  - Persona LoRA Training
  - Foundation Fork Training
  - Evaluation / Test
- Review lifecycle:
  `Captured → Review → Approved → Dataset → Training Job → Candidate → Evaluation → Deploy`
- Editable review inspector with approve/reject/edit actions.
- Versioned dataset snapshots and persistent training jobs.
- Model/adapter evaluation, activation, deployment, and rollback.

### Foundation models and persona LoRAs
- Original base models remain immutable.
- Versioned SARA Foundation descendants retain parent lineage.
- Foundation comparison, approval, activation, and rollback.
- Multiple persona adapter / LoRA versions per persona.
- Automatic persona adapter resolution.
- SARA-aware OpenAI-compatible inference runtimes can load the resolved stack using:
  - `X-SARA-Foundation-Id`
  - `X-SARA-Foundation`
  - `X-SARA-Adapter-Id`
  - `X-SARA-Adapter`

### Persona behavior and conversation
- Persistent conversation memory.
- Persona style dimensions for intelligence/language level, slang, grammar quality, typo tendency, emoji tendency, mood, and age-aware writing variation.
- Shared response-variation layer for model and trigger output.
- Smiley emoji composer control and paperclip attachment control.
- Randomized conversational pacing remains configurable.

### Trigger rules
- Persistent deterministic trigger rules execute before normal model generation.
- Terminal rules stop generic fall-through.
- Continue rules may pass through to generation.
- Multiple alternate responses per rule.
- Priority ordering.
- Rule create/edit/delete manager.
- Trigger-match diagnostics and durable trigger-match log.

### Diagnostics and release safety
- Model Lab diagnostics export.
- Persistent registries for foundations, adapters, datasets, jobs, models, and trigger rules.
- SHA-256 package manifests.
- Windows CI validates build, tests, dependency layout, package layout, and installer generation.
- Optional Microsoft Artifact Signing is supported when repository signing secrets are configured.

## Hybrid UI direction

SARA 1.0.16 uses one coherent visual system:

- dark navy / charcoal base
- cyan and electric-blue primary accents
- restrained purple secondary accents
- card-based grouping
- consistent spacing and typography
- hover/pressed feedback
- dedicated workspaces instead of crowded utility forms
- minimum supported window size to prevent overlapping controls

The approved design combines:
- **Executive Dashboard** for Model Lab Overview
- **Conversational Trainer** for Train
- **Management workspace** for Personas & LoRAs
- **Command-center views** for Jobs, Evaluation, and Deployment

## Windows build

The repository uses `.github/workflows/windows-build.yml`.

Each Windows build:
1. configures CMake/vcpkg,
2. builds with MSVC x64,
3. runs CTest,
4. verifies the CLI does not depend on `sqlite3.dll`,
5. creates `SARA-1.0.16-windows-x64`,
6. generates `SHA256SUMS.txt`,
7. builds `SARA-Setup-1.0.16.exe` with Inno Setup,
8. generates an installer SHA-256 file,
9. uploads both portable and installer artifacts.

If Microsoft Artifact Signing secrets are configured, executable and installer signing is performed automatically. Otherwise the workflow records an unsigned-development-build notice.

## Build locally

Prerequisites:
- Visual Studio 2022 or newer with Desktop development with C++
- CMake 3.24+
- vcpkg

Example:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
ctest --preset windows-msvc-release
```

## Run

The GUI build is:

```
SARA.exe
```

The compatibility CLI remains:

```powershell
.\bin\SentinelCli.exe "$env:LOCALAPPDATA\Sentinel" interactive
```

The internal `%LOCALAPPDATA%\Sentinel` data root is intentionally retained so existing cases, evidence, settings, personas, conversation memory, model registries, and other local state survive the product rename.

## Helper scripts

Canonical helpers:
- `Install-SARA.ps1`
- `Uninstall-SARA.ps1`
- `SARAConsole.cmd`

Legacy Sentinel-named helper files remain as compatibility wrappers.

## Project state and roadmap

GitHub is the canonical SARA project memory.

Start future development sessions with:
- `docs/SARA_PROJECT_STATE.md`
- `docs/SARA_ROADMAP.md`
- `docs/DEVELOPMENT_CONTINUITY.md`
- `docs/architecture/SARA_MODEL_LAB_UI_1.0.16.md`
- `docs/architecture/SARA_MODEL_LAB_TRAINING_ARCHITECTURE_1.0.16.md`

## Version

- Product: **SARA 1.0.16**
- Native GUI: `SARA.exe`
- Evidence container: SEV1
- Audit canonicalization: audit-v1
