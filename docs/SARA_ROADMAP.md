# SARA Roadmap

Last updated: 2026-09-27

## 1.0.16 — Trainer / Model Lab Redesign

### UI / UX
- [x] Lock hybrid UI direction: Executive Dashboard + Conversational Trainer + Personas/LoRAs management + Training Pipeline/Command Center.
- [ ] Replace the current crowded Model Lab layout with structured workspaces.
- [x] Add internal navigation for Overview, Train, Datasets, Personas & LoRAs, Foundation Forks, Jobs, Evaluation, Deployment.
- [ ] Fix all overlapping/misaligned Trainer controls.
- [ ] Establish consistent spacing, sizing, typography, cards, buttons, and field styling.
- [ ] Improve overall SARA desktop polish and visual consistency.
- [x] Add smiley-face emoji button to chat composer.
- [x] Add paperclip attachment button to chat composer.
- [x] Use vector icons and matching icon-button styling.
- [ ] Add hover/pressed/focus states and tooltips.
- [ ] Make layouts resilient to supported window resizing.

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
- [ ] Preserve immutable original base models.
- [ ] Add persona adapter registry.
- [ ] Preserve adapter-to-foundation compatibility/version metadata.
- [ ] Add adapter active/staging/training/archived/rollback states.
- [ ] Support multiple LoRA/QLoRA versions per persona.
- [ ] Automatically resolve and load persona adapter.
- [x] Add model/persona context bar to Trainer.

### Conversational trainer
- [x] Allow operator to chat with a model in training mode.
- [ ] Allow natural-language corrections such as “Samantha would say this instead.”
- [ ] Allow runtime-only correction overrides during training sessions.
- [ ] Capture model response + correction as a training candidate.
- [ ] Preserve correction provenance: context, original response, correction, target, persona, foundation, adapter, session, timestamps, review state.
- [ ] Add correction categories and metadata.
- [x] Add review queue (initial in-memory capture/review counters; persistence pending).
- [ ] Approve/reject/edit training candidates.
- [ ] Promote approved examples into versioned datasets.
- [ ] Track provenance for every training record.

### Training workflow
- [ ] Captured
- [ ] Review
- [ ] Approved
- [ ] Dataset
- [x] Training Job (persistent job records and Jobs workspace implemented; external trainer handoff pending)
- [ ] Candidate
- [ ] Evaluation
- [ ] Deployment
- [ ] Rollback

### Persona runtime
- [ ] Foundation model resolution.
- [ ] Persona LoRA resolution.
- [ ] Persona behavior profile.
- [ ] Conversation memory.
- [ ] Current-context assembly.
- [ ] Automatic adapter load on persona selection (UI/runtime resolution displayed; real adapter registry/load still pending).
- [ ] Resolve complete runtime stack: Foundation -> LoRA -> Behavior Profile -> Conversation Memory -> Current Context.
- [ ] Version pinning for reproducibility.

### Trigger / rule engine
- [ ] Ensure deterministic triggers run before normal model generation.
- [ ] Support terminal vs continue-after-match rule behavior.
- [ ] Prevent a matched trigger from falling through into a generic model response unless explicitly configured.
- [ ] Show existing rules/triggers in the UI instead of only displaying a rule count.
- [ ] Add rule inspection/editing.
- [ ] Add priority/order.
- [ ] Add match diagnostics/logging.
- [ ] Add optional generative variation for trigger responses while preserving the trigger's intent.

### Response variation
- [ ] Avoid repeating one canned phrasing.
- [ ] Generate semantically equivalent variants.
- [ ] Add configurable variation/randomness limits and reusable phrasing pools.
- [ ] Condition variation on persona personality.
- [ ] Condition variation on age-appropriate writing style.
- [ ] Condition variation on intelligence/language level.
- [ ] Support slang, grammar imperfections, typos, and emoji tendencies when configured.
- [ ] Incorporate prior conversation context to avoid repetitive questions and replies.

### Logging / diagnostics
- [ ] Keep persona message logs.
- [ ] Log rule/trigger matches.
- [ ] Log selected model/foundation/adapter versions.
- [ ] Log training corrections and review decisions.
- [ ] Add diagnostics export suitable for debugging persona behavior.

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
