# SARA Project State

Last updated: 2026-09-27

## Canonical identity

**SARA — Synthetic Adaptive Response Agent**

The repository was originally named Sentinel and still uses Sentinel in many source paths, namespaces, build targets, and historical assets. SARA is the canonical product name going forward unless explicitly changed.

## Repository

- GitHub: `afterburn25/Sentinel`
- Default branch: `main`
- Active development branch: `sara-1.0.17-dataset-adapter-management`
- Desktop implementation: native Windows C++20
- Build system: CMake + vcpkg
- Main GUI implementation currently lives in `src/Gui/Main.cpp`
- Windows builds are validated with GitHub Actions.

## Current release baseline

The latest distributed desktop baseline supplied in chat is **SARA 1.0.15**.

The GitHub source still contains older internal version strings such as Sentinel 1.0.7. Version/branding normalization is therefore part of the 1.0.16 work.

## Current development target

**SARA 1.0.17 — Dataset & Adapter Management**

SARA 1.0.16 is merged into `main` and is the validated baseline. 1.0.17 extends the Model Lab without changing the approved hybrid UI system.

### Primary goals

1. Redesign Model Lab / Trainer so it is organized, aligned, responsive, and professional.
2. Eliminate overlapping controls, clipped labels, inconsistent spacing, and layout collisions.
3. Create dedicated workspaces for:
   - Train
   - Datasets
   - Personas & LoRAs
   - Foundation Forks
   - Jobs
   - Evaluation
   - Deployment
4. Add selectable training modes:
   - Behavior Tuning
   - Dataset Training
   - Persona LoRA Training
   - Foundation Fork Training
   - Evaluation / Test Mode
5. Support versioned SARA foundation-model forks while preserving the original base model.
6. Support persona-specific LoRA / QLoRA adapters with multiple versions per persona.
7. Automatically load the assigned persona adapter when a persona is selected.
8. Add a conversational trainer where an operator can correct model behavior in natural language.
9. Convert approved conversational corrections into reviewable training records.
10. Preserve a review lifecycle:
    `Captured -> Review -> Approved -> Dataset -> Training Job -> Candidate -> Evaluation -> Deploy`
11. Keep deterministic triggers/rules separate from generative model behavior and execute rules before normal generation.
12. Improve response variation so learned behavior can be expressed differently according to the persona instead of repeating canned responses.
13. Preserve persona behavior dimensions including age-appropriate writing style, intelligence level, slang, grammar quality, typo tendency, emoji tendency, personality, mood, and prior conversation context.
14. Keep live investigation/inference isolated from training operations.

## Runtime resolution target

When a persona such as Samantha is selected, runtime resolution should follow:

`SARA Foundation -> Persona LoRA -> Persona Behavior Profile -> Conversation Memory -> Current Context`

The persona's assigned adapter should load automatically.

## Simulation / chat composer UI requirements

The composer is being polished as part of 1.0.16.

- Emoji control must use a **smiley-face icon**.
- Attachment control must use a **paperclip icon**.
- Both should be icon-only toolbar buttons with consistent size, padding, hover state, borders, alignment, and tooltips.
- Prefer vector-drawn icons so Windows emoji/font differences do not affect appearance.
- Composer controls must visually match the rest of the SARA desktop UI.

## Current GUI source landmarks

At the time this file was created:

- Main page enumeration and native controls: `src/Gui/Main.cpp`
- Simulation / chat UI: approximately lines 1200-1350
- Persona & Policy UI: approximately lines 1878+
- Model Lab UI: approximately lines 2045+
- Main click dispatch: approximately lines 440+
- Icon renderer: approximately lines 780+
- Native control layout: approximately lines 1310+

These line numbers are approximate and will move as the source is refactored.

## UI direction

The approved 1.0.16 direction is a **hybrid UI** combining the strongest parts of the explored concepts:

- **Executive Dashboard** for the main Model Lab overview and at-a-glance health/status.
- **Conversational Trainer** for the Train workspace, with a large central chat/correction flow.
- **Personas & LoRAs Management** patterns for persona tables, adapter assignment, version/status management, and detail inspection.
- **Training Pipeline / Command Center** patterns for Jobs, Evaluation, Deployment, version history, and rollback.

This is not a single mockup copied verbatim. The production UI should use one coherent visual system while each workspace uses the interaction pattern best suited to its job.

The Trainer redesign should look like a premium, modern desktop application rather than a utility form.

Target characteristics:

- Clear navigation hierarchy
- Consistent spacing system
- Strong alignment
- Card-based grouping
- Cleaner typography
- Subtle depth and borders
- Polished hover/selected states
- Better visual separation between configuration, training, jobs, and deployment
- No overlapping controls at supported window sizes
- No large unstructured wall of controls
- Consistent form-row heights and label widths
- Dedicated workspace per major function

## Project continuity rule

GitHub is the canonical project memory for SARA.

For every meaningful future discussion or implementation change, update the repository with:
- the decision or requirement,
- the code/configuration change if implemented,
- the roadmap/status if not yet implemented,
- release notes when behavior changes,
- migration/compatibility notes when relevant.

A future development session should be able to reconstruct current state from the repository without depending on chat history.


## Approved Model Lab information architecture

- **Overview** — executive control center with model/persona context, metrics, recent jobs, evaluation summary, and deployment status.
- **Train** — conversational trainer first; large chat/correction workspace with review/capture controls.
- **Datasets** — dataset browser, snapshots, quality, lineage, import/export.
- **Personas & LoRAs** — persona list/table, adapter versions, foundation assignment, status, quick inspector.
- **Foundation Forks** — immutable base models plus versioned SARA forks.
- **Jobs** — active/recent training jobs, progress, resource use, logs.
- **Evaluation** — candidate comparison, quality metrics, regression results.
- **Deployment** — approved packages, active version, rollout, rollback, version history.

Shared top context should expose **Foundation / Persona / Assigned LoRA / Training Mode** where relevant.

The selected visual language is dark navy/charcoal with cyan/electric-blue primary accents and restrained purple secondary accents. It should be polished and futuristic without becoming visually noisy.


## Approved training architecture

The integrated Model Lab / Trainer must implement the architecture defined in:
`docs/architecture/SARA_MODEL_LAB_TRAINING_ARCHITECTURE_1.0.16.md`

Key requirements:

- Training modes: Behavior Tuning, Dataset Training, Persona LoRA Training, Foundation Fork Training, Evaluation/Test.
- Immutable original/base foundation models with versioned SARA Foundation descendants and rollback/comparison.
- Multiple versioned LoRA/QLoRA adapters per persona; selecting a persona automatically resolves its assigned adapter.
- Conversational corrections can affect the current training session immediately but never silently change production weights.
- Canonical review path: `Captured -> Review -> Approved -> Dataset -> Training Job -> Candidate -> Evaluation -> Deploy`.
- Runtime stack: `SARA Foundation -> Persona LoRA -> Persona Behavior Profile -> Conversation Memory -> Current Context`.
- Deterministic triggers/rules execute before normal generation and remain separate from model weights.
- Response variation must preserve persona/trigger intent while varying phrasing according to age, intelligence/language level, slang, grammar, typo tendency, emoji use, personality, mood, and conversation history.


## Hybrid UI implementation rule

**Hybrid UI is the controlling design system for all remaining 1.0.16 work.**

Backend architecture changes must not regress the interface into generic utility forms. Every new feature must be surfaced through the approved hybrid pattern:

- Executive Dashboard for Overview
- Conversational Trainer for Train
- Management workspace for Personas & LoRAs
- Command-center styling for Jobs, Evaluation, and Deployment
- Shared dark navy/charcoal visual language with cyan/electric-blue primary accents and restrained purple secondary accents
- Shared Model Lab navigation and context bar
- Consistent cards, spacing, typography, iconography, badges, and state treatment


## 1.0.16 implementation snapshot

The active development branch now implements the approved hybrid design and core Model Lab state model:

- Functional Overview / Train / Datasets / Personas & LoRAs / Foundation Forks / Jobs / Evaluation / Deployment workspaces.
- Conversational Trainer with correction/instruction capture.
- Persistent reviewed training examples and immutable dataset snapshots.
- Persistent training-job records.
- Persistent versioned foundation forks with immutable base, lineage, comparison, activation, and rollback.
- Persistent persona adapter/LoRA registry with multi-version lifecycle and rollback.
- Automatic persona adapter resolution.
- OpenAI-compatible runtime contract sends resolved `X-SARA-Foundation-Id`, `X-SARA-Foundation`, `X-SARA-Adapter-Id`, and `X-SARA-Adapter` headers so compatible local inference services can automatically load the selected LoRA.
- Persistent trigger rules with create/edit/delete, priority, terminal/continue behavior, alternate response pools, and trigger-first execution.
- Persona response variation dimensions: intelligence/language level, slang, grammar quality, typo tendency, emoji tendency, and mood.
- Hybrid UI interaction polish: vector smiley/paperclip composer controls, hover/pressed treatment, contextual help, aligned dropdowns, and minimum-window protection.
- Visible product/package branding normalized to SARA 1.0.16 while retaining internal Sentinel namespace/storage compatibility where changing it could break existing data.


## Validated 1.0.16 release build

Final release validation completed on 2026-09-27 from branch `sara-1.0.16-trainer-redesign`.

- GitHub Actions run: **#511**
- Workflow run ID: `36321502659`
- Validated commit: `befccf9a1481afc7cd8e9ffd620893e5ea934ed1`
- Windows MSVC x64 Release build: PASS
- Unit/integration tests: PASS
- SQLite DLL dependency check: PASS
- Install/package step: PASS
- SHA-256 manifest generation: PASS
- SHA-256 manifest verification: PASS
- Packaged launcher layout: PASS
- Inno Setup installer compilation: PASS
- Installer SHA-256 generation: PASS
- Portable artifact upload: PASS
- Installer artifact upload: PASS

### Portable package

- Artifact name: `SARA-1.0.16-windows-x64`
- Artifact ID: `10932775896`
- Artifact ZIP SHA-256: `84c50af15a6c317ce71a1dd17638cb533f0639dc8c50614bc02317ce22eede80`
- `SARA.exe` SHA-256: `55ef77594cb9e3bd858bb10fe89cd18ae4e41221e9456c6af100f407d8172780`
- Portable `SHA256SUMS.txt`: 22 entries verified successfully.
- Manifest paths use normalized forward slashes.

### Installer package

- Artifact name: `SARA-1.0.16-Installer-Package`
- Artifact ID: `10932164823`
- Artifact ZIP SHA-256: `ca5e7088cb55df7a145c3991a441a10ccdc1c3bcccdae5cb167543488c4238d3`
- Installer: `SARA-Setup-1.0.16.exe`
- Installer EXE SHA-256: `66a102715cc248b831d75ac7052be4d8beb0a44054b475981a6541b2a47094b2`
- Included installer checksum file matches the EXE exactly.

### Signing status

Microsoft Artifact Signing is wired into CI, but repository signing secrets are not configured. The release artifacts are therefore **unsigned development builds** and the workflow records that status explicitly.


## 1.0.17 active goals

- Versioned dataset lineage with parent snapshot relationships and timestamps.
- Dataset browser/selection details in the hybrid Datasets workspace.
- Dataset import/export for portable reviewed training sets.
- Persona adapter comparison and version history.
- Richer training-run history including creation/start/completion timestamps.
- Preserve all 1.0.16 runtime and release compatibility.
