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
- [ ] Preserve immutable original base models.
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
- [ ] Add correction categories and metadata.
- [x] Add review queue (initial in-memory capture/review counters; persistence pending).
- [ ] Approve/reject/edit training candidates.
- [x] Promote approved examples into persistent versioned dataset snapshots.
- [x] Track source conversation/persona/foundation/adapter provenance for training records; reviewer/timestamp enrichment remains follow-up work.

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
- [ ] Condition variation on age-appropriate writing style.
- [x] Condition response variation on intelligence/language level.
- [x] Support slang, grammar quality, typo tendency, emoji tendency, and mood when configured.
- [x] Incorporate persisted conversation memory and recent history into response generation.

### Logging / diagnostics
- [ ] Keep persona message logs.
- [ ] Log rule/trigger matches.
- [x] Persist model/foundation/adapter registries and include resolved identities in runtime requests.
- [x] Persist captured training corrections and approval state; reviewer identity/timestamps remain follow-up work.
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
