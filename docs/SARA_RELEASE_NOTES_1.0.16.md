# SARA 1.0.16 Release Notes

Release date: 2026-09-27

## SARA — Synthetic Adaptive Response Agent

SARA 1.0.16 is the first release in this repository to fully adopt the SARA product identity and the integrated Model Lab / Trainer architecture.

The original repository and selected internal namespaces/data paths retain the historical Sentinel name for compatibility.

## Model Lab / Trainer

### Hybrid UI
SARA 1.0.16 implements the approved hybrid Model Lab design:

- Executive Dashboard-style Overview
- Conversational Trainer-style Train workspace
- Management workspace for Personas & LoRAs
- Command-center workspaces for Jobs, Evaluation, and Deployment
- Datasets and Foundation Forks as dedicated first-class workspaces

Visual direction:
- dark navy / charcoal base
- cyan and electric-blue primary accents
- restrained purple secondary accents
- card-based grouping
- consistent spacing and typography
- hover/pressed feedback
- contextual help
- minimum supported window size to prevent overlap

### Training modes
- Behavior Tuning
- Dataset Training
- Persona LoRA Training
- Foundation Fork Training
- Evaluation / Test

### Conversational Trainer
- direct conversation with SARA inside Train
- correction/instruction field
- correction categories
- persistent capture metadata
- Capture for Review
- editable review target
- Approve / Reject
- human-reviewed training lifecycle

Canonical lifecycle:

`Captured → Review → Approved → Dataset → Training Job → Candidate → Evaluation → Deploy`

## Training data and datasets

Training records now persist:

- persona
- foundation ID
- adapter ID
- source conversation
- input
- original response
- operator correction
- target response
- correction category
- UTC capture time
- reviewer
- review state

Approved records can be frozen into versioned dataset snapshots for reproducible jobs.

## Foundation Forks

- original/downloaded base models are preserved as immutable references
- SARA Foundation descendants are versioned
- parent lineage is retained
- selected vs parent comparison
- approval
- activation
- previous-version rollback

Examples:
- SARA Foundation 1.0
- SARA Foundation 1.1
- SARA Foundation Experimental

## Persona LoRAs / adapters

- persistent persona adapter registry
- multiple adapter versions per persona
- staging / active / archived lifecycle
- adapter rollback
- adapter-to-foundation linkage
- automatic persona adapter resolution

Runtime resolution:

`SARA Foundation → Persona LoRA → Behavior Profile → Conversation Memory → Current Context`

### OpenAI-compatible automatic loading contract

SARA sends optional request metadata:

- `X-SARA-Foundation-Id`
- `X-SARA-Foundation`
- `X-SARA-Adapter-Id`
- `X-SARA-Adapter`

A SARA-aware local inference service can use these values to activate the correct foundation/LoRA automatically. Generic OpenAI-compatible servers may ignore the additional headers.

## Trigger rules

SARA 1.0.16 adds a persistent deterministic trigger engine.

- rules execute before normal model generation
- terminal matches prevent generic fall-through
- continue matches may proceed into generation
- priority ordering
- multiple alternate responses
- create / edit / delete manager
- rule diagnostics
- last-match visibility
- durable trigger-match TSV log

This fixes the prior behavior where a new/matched trigger could be ignored and SARA would continue with a generic model response.

## Persona response variation

New persistent persona style dimensions:

- intelligence / language level
- slang level
- grammar quality
- typo tendency
- emoji tendency
- mood
- age-aware style influence

The shared variation layer applies to both trigger responses and normal model responses.

The goal is semantic consistency without repeating one canned sentence.

## Conversation memory and diagnostics

- existing persistent conversation memory remains part of runtime context
- diagnostics export includes current runtime stack
- persona style settings
- trigger rules
- last trigger match
- datasets
- training jobs
- evaluation state
- recent persona conversation
- recent training examples

## Composer polish

- emoji button is a vector smiley-face icon
- attachment button is a vector paperclip icon
- consistent icon sizing/alignment
- hover and pressed states
- contextual hover help

## Evaluation and Deployment

- candidate model registry
- evaluation score
- persona consistency gate
- policy gate
- activation
- deployment status
- rollback
- production version pinning

## Windows release packaging

Primary GUI executable:

`SARA.exe`

Portable package:

`SARA-1.0.16-windows-x64`

Installer:

`SARA-Setup-1.0.16.exe`

Canonical helper scripts:
- `Install-SARA.ps1`
- `Uninstall-SARA.ps1`
- `SARAConsole.cmd`

Legacy Sentinel-named helpers remain as compatibility wrappers.

## Data compatibility

The local data root remains:

`%LOCALAPPDATA%\Sentinel`

This is intentional. It protects existing:

- cases
- evidence
- keys
- simulation settings
- personas
- conversation memory
- model registries
- foundation/adapters
- training data
- trigger rules

Renaming the internal data root is not part of 1.0.16.

## Build validation

The first validated portable 1.0.16 baseline passed:

- MSVC x64 Release compile
- CTest
- SQLite DLL dependency verification
- package install
- SHA-256 manifest generation
- launcher-layout verification
- artifact upload

The canonical workflow also supports generation of the Inno Setup installer and installer SHA-256 manifest.

## Code signing

Microsoft Artifact Signing is supported by CI.

When the required repository secrets are absent, CI intentionally produces an unsigned development build and records that state. Windows SmartScreen may therefore warn for unsigned builds.

## Canonical project memory

Future sessions should recover state from:

- `docs/SARA_PROJECT_STATE.md`
- `docs/SARA_ROADMAP.md`
- `docs/DEVELOPMENT_CONTINUITY.md`
- `docs/architecture/SARA_MODEL_LAB_UI_1.0.16.md`
- `docs/architecture/SARA_MODEL_LAB_TRAINING_ARCHITECTURE_1.0.16.md`
- this release-notes document
