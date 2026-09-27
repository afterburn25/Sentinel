# SARA Roadmap

Last updated: 2026-09-27

## 1.0.16 — Trainer / Model Lab Redesign

### UI / UX
- [x] Lock hybrid UI direction: Executive Dashboard + Conversational Trainer + Personas/LoRAs management + Training Pipeline/Command Center.
- [x] Replace the current crowded Model Lab layout with structured hybrid workspaces.
- [x] Add internal navigation for Overview, Train, Datasets, Personas & LoRAs, Foundation Forks, Jobs, Evaluation, Deployment.
- [x] Fix Trainer control overlap/misalignment and enforce a safe minimum window size.
- [x] Establish consistent spacing, sizing, typography, cards, buttons, and field styling.
- [x] Improve SARA Model Lab / Trainer polish and visual consistency using the approved hybrid design.
- [x] Add smiley-face emoji button to chat composer.
- [x] Add paperclip attachment button to chat composer.
- [x] Use vector icons and matching icon-button styling.
- [x] Add hover/pressed states and contextual hover help; native focus behavior retained for edit/dropdown controls.
- [x] Make layouts resilient to supported resizing and enforce a 1280×760 minimum to prevent control collisions.

### Training architecture
- [x] Document approved 1.0.16 training architecture.
- [x] Add Training Mode selector.
- [x] Behavior Tuning mode.
- [x] Dataset Training mode.
- [x] Persona LoRA Training mode.
- [x] Foundation Fork Training mode.
- [x] Evaluation / Test mode.
- [x] Keep original downloaded/base models immutable.
- [x] Add versioned foundation-model fork registry.
- [x] Track foundation parent/version lineage.
- [x] Add foundation rollback; side-by-side comparison UI still pending.
- [x] Preserve immutable original base models.
- [x] Add persistent persona adapter registry.
- [x] Preserve adapter-to-foundation compatibility/version metadata.
- [x] Add adapter training/staging/active/archived states and rollback.
- [x] Support multiple adapter versions per persona with staging/active/archived lifecycle.
- [x] Automatically resolve persona adapter and send foundation/adapter identity to compatible inference runtimes via X-SARA-* headers.
- [x] Add model/persona context bar to Trainer.

### Conversational trainer
- [x] Allow operator to chat with a model in training mode.
- [x] Allow natural-language corrections/instructions in the Conversational Trainer.
- [x] Capture training-session corrections without mutating production weights.
- [x] Capture user input + original model response + correction/target as a persistent training candidate.
- [x] Preserve core correction provenance: input, original response, correction/target, persona, foundation, adapter, source conversation, and review state. Timestamp/reviewer fields remain follow-up work.
- [x] Add correction categories plus timestamp/reviewer metadata with backward-compatible persistence.
- [x] Add review queue (initial in-memory capture/review counters; persistence pending).
- [x] Approve, reject, and edit training candidates in the Datasets review inspector.
- [x] Promote approved examples into persistent versioned dataset snapshots.
- [x] Track source conversation/persona/foundation/adapter provenance for training records; reviewer/timestamp enrichment remains follow-up work.

### Training workflow
- [x] Captured
- [x] Review
- [x] Approved
- [x] Dataset
- [x] Training Job (persistent job records and Jobs workspace implemented; external trainer handoff pending)
- [x] Candidate model registry/evaluation state
- [x] Evaluation
- [x] Deployment
- [x] Rollback

### Persona runtime
- [x] Foundation model resolution.
- [x] Persona LoRA resolution.
- [x] Persona behavior profile.
- [x] Conversation memory.
- [x] Current-context assembly.
- [x] Automatic adapter resolution on persona selection with runtime loading contract via X-SARA-Foundation / X-SARA-Adapter headers.
- [x] Resolve complete runtime stack: Foundation -> LoRA -> Behavior Profile -> Conversation Memory -> Current Context.
- [x] Version-pin foundation IDs, adapter IDs, and dataset snapshots for reproducible jobs.

### Trigger / rule engine
- [x] Ensure deterministic triggers run before normal model generation.
- [x] Support terminal vs continue-after-match rule behavior.
- [x] Prevent terminal trigger matches from falling through into generic generation.
- [x] Show trigger rule name, pattern, priority, and stop/continue behavior in the Train UI.
- [x] Add persistent trigger rule create/edit/delete manager.
- [x] Add numeric trigger priority ordering.
- [x] Show last matched rule in runtime diagnostics; durable per-message audit logging remains follow-up work.
- [x] Add alternate trigger response pools plus persona-shaped variation while preserving rule match intent.

### Response variation
- [x] Avoid one canned trigger phrasing through alternate response pools and shared variation.
- [x] Generate deterministic phrasing variation from response pools and persona style.
- [x] Add reusable trigger phrasing pools; broader runtime randomness controls remain available through model temperature/config.
- [x] Condition output on configured persona behavior/style context.
- [x] Condition writing variation on age as a light style influence while explicit persona settings remain authoritative.
- [x] Condition response variation on intelligence/language level.
- [x] Support slang, grammar quality, typo tendency, emoji tendency, and mood when configured.
- [x] Incorporate persisted conversation memory and recent history into response generation.

### Logging / diagnostics
- [x] Keep persona conversation history in persistent conversation memory and include recent turns in diagnostics export.
- [x] Persist trigger matches to trigger-matches.tsv with conversation/persona/rule/input/response details.
- [x] Persist model/foundation/adapter registries and include resolved identities in runtime requests.
- [x] Persist captured training corrections and approval state; reviewer identity/timestamps remain follow-up work.
- [x] Add Model Lab diagnostics export with runtime stack, persona style, rules, datasets, jobs, evaluation, recent conversation, and training examples.

## Follow-on milestones

### 1.0.17 — Dataset and Adapter Management
- Dataset browser and snapshots
- Adapter comparison
- Training run history
- Dataset lineage
- Import/export

### 1.0.18 — Evaluation Suite
- Persona consistency tests
- Long-context tests
- Trigger regression tests
- Style consistency
- Policy regression
- Candidate comparison reports

### 1.0.19 — Deployment and Rollback
- Approved model packages
- Version locking
- One-click activate
- One-click rollback
- Deployment audit trail

## Longer-term Model Lab roadmap

- Larger reviewed persona datasets
- Regional/style adapters
- Knowledge-ceiling classifier
- Backstory consistency checks
- Agency-policy risk classifier
- Signed/versioned policy packs
- Shadow-learning review
- Model-vs-investigator comparison
- Field-learning adapters
- Adversarial evaluation
- Prompt-injection tests
- Persona-drift tests
- Long-context stress tests
- GGUF package manifests
- llama.cpp local inference contract
- Local/background training service separation


## 1.0.17 — Dataset & Adapter Management

### Dataset management
- [x] Add snapshot creation timestamp and parent lineage.
- [x] Add selectable dataset snapshot browser/details.
- [x] Add dataset export.
- [x] Add dataset import with duplicate-safe IDs.
- [x] Show example count, approval provenance, and lineage.

### Persona adapter management
- [x] Add adapter version selection.
- [x] Add active-vs-selected adapter comparison.
- [x] Show foundation compatibility and adapter lifecycle state/history context.
- [x] Add adapter export metadata.

### Training run history
- [x] Add created/start/completed timestamps to training jobs.
- [x] Preserve completed job history.
- [x] Show dataset/foundation lineage used by each job.
- [x] Add job history/detail inspector.

### UI
- [x] Keep 1.0.17 inside the approved hybrid Model Lab design.
- [x] Preserve no-overlap/minimum-window guarantees.


## 1.0.18.2 — Reusable Persona Profiles

- [x] Restore named reusable persona-profile persistence.
- [x] Persist current persona behavior/style fields.
- [x] Persist locked persona facts.
- [x] Persist randomized response-start delay range per persona.
- [x] Auto-import the current persona when upgrading from the single-profile settings model.
- [x] Show reusable profiles in the hybrid Personas & LoRAs workspace.
- [x] Add New Persona workflow.
- [x] Add persona selection/switching.
- [x] Add persona deletion with at-least-one-profile protection.
- [x] Automatically resolve the selected persona's active LoRA.
- [x] Reset the active conversation when switching personas to prevent context bleed.
- [x] Add regression tests for reusable profile persistence and updates.
- [ ] Validate SARA 1.0.18.2 through Windows build/tests, actual UI screenshots, installer build, and installer screenshot.


## 1.0.18.3 — Reconciled Persona Memory + Channel Core

- [x] Rebase channel work onto the latest 1.0.18.2 reusable-persona head.
- [x] Preserve the restored 1.0.15 visual/UX baseline.
- [x] Scope conversation archive/resume to the selected persona.
- [x] Scope recalled prior-conversation memory to the selected persona.
- [x] Separate stored persona name from full persona summary.
- [x] Restore normalized provider-neutral channel core.
- [x] Restore local SARA channel adapter and operator-approved media queue.
- [x] Restore automation review gates.
- [x] Restore jurisdiction profiles and operating-jurisdiction selection.
- [x] Force review-only behavior when jurisdiction rules are not active/reviewed.
- [x] Renumber combined migrations to 0013 persona scope / 0014 channel core / 0015 jurisdiction rules.
- [x] Make persona-memory isolation tests effective in Release builds.
- [x] Stamp executable, installer, CI artifacts, updater, and runtime as SARA 1.0.18.3.
- [ ] Pass full Windows compile/test/package/screenshot/installer validation for the reconciled branch.
- [ ] Review actual packaged SARA UI and installer screenshots for visual regression before merge.
