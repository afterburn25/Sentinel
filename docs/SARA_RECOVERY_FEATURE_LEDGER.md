# SARA Recovery Feature Ledger

Date: 2026-09-28

## Purpose

This file is the authoritative inventory for recovering SARA after the post-1.0.15 branch split.

The goal is to eliminate uncertainty about what exists, what was lost, what was added later, and what may be restored.

## Canonical recovery branches

### Immutable known-good source

- Branch: `sara-rescue-1.0.15-known-good`
- Commit: `78a2aa3d30eb8cd647d1f4213f5d4e3961a6105d`
- Status: TRUSTED / DO NOT MODIFY

### Only active recovery branch

- Branch: `sara-recovery-from-1.0.15`
- Base: exact trusted 1.0.15 commit above
- Status: ACTIVE RECOVERY
- Rule: do not create another feature branch during recovery. Restore and validate one feature at a time here.

All other branches are historical reference only until a specific change is manually reviewed and selectively ported.

## Confirmed ancestry failure

Git comparison against the real 1.0.15 tip shows that later development did not continue from the actual finished 1.0.15 application.

Comparison with `main`:
- `main` is only 2 commits ahead of its merge base with the trusted 1.0.15 line.
- `main` is 297 commits behind the trusted 1.0.15 line.
- Therefore `main` does not contain 297 commits that were present in the finished 1.0.15 application.

This means the later 1.0.16/1.0.17 merge line was not a clean continuation of the finished 1.0.15 code. It was built from the older `main` lineage and therefore could compile successfully while still omitting large amounts of the working 1.0.15 application.

Observed comparisons from the trusted 1.0.15 tip:
- `main`: diverged, +2 / -297 commits relative to 1.0.15
- `sara-1.0.16-trainer-redesign`: diverged, +101 / -297
- `sara-1.0.17-dataset-adapter-management`: diverged, +20 / -297
- `sara-1.0.18-evaluation-suite`: diverged, +38 / -297
- `sara-visual-restoration-1.0.15-baseline`: diverged, +84 / -297
- `sara-1.0.18.2-persona-profiles`: diverged, +103 / -297
- `sara-1.0.18.3-persona-memory-scope`: diverged, +96 / -297
- `sara-1.0.18.3-channel-core`: diverged, +116 / -297
- `sara-1.0.19-deployment-rollback`: diverged, +48 / -297

None of those branches may be treated as a replacement baseline.

## Trusted 1.0.15 capabilities already present

The following are evidenced directly in the real 1.0.15 commit history and therefore must not be re-created from the later branches unless a specific bug is proven.

### Installer / runtime
- model verification and verification-marker handling
- visible AI/model and LoRA upgrade checks
- downloaded model verification before install
- llama.cpp/runtime validation
- GPU/runtime selection with CPU fallback
- upgrade/preserved-data behavior
- compiled application version reporting
- Windows packaging and installer release flow

### Startup / UI
- approved SARA splash/startup implementation
- existing 1.0.15 application shell and navigation
- native full-height chat composer behavior
- centered native edit controls
- emoji and paperclip composer controls
- DirectWrite color emoji support
- persona screen tabbed professional layout

### Conversation / persona
- persisted conversations and persona continuity
- persisted persona self-memory / learned continuity
- persistent response rules
- rule-first handling before normal model generation
- smart rule matching and rule-test diagnostics
- varied rule phrasing across conversations
- persona gallery and persistent image attachments
- sent/received image thumbnails
- duplicate-image avoidance

### Model Lab / trainer
- dedicated Trainer navigation page
- training mode selector
- trainer persistence/state
- foundation model forks
- persona LoRA bindings
- runtime foundation/LoRA resolution
- LoRA runtime restart support
- trainer jobs
- external trainer launcher
- LoRA/foundation worker scripts
- queued LoRA/foundation training job launch

These features are part of the trusted 1.0.15 line and are not "missing later features."

## Post-1.0.15 work: quarantine inventory

The items below are useful work that exists in later branches, but they are NOT currently part of the recovery branch and must be manually ported only after the exact 1.0.15 application is confirmed working.

### 1.0.16 trainer redesign — QUARANTINED
Source: `sara-1.0.16-trainer-redesign`

Candidate backend/features:
- reviewed correction lifecycle: Captured -> Review -> Approved/Rejected
- editable training-example review inspector
- versioned dataset snapshots
- persistent training-job history
- expanded foundation lineage/comparison/activation/rollback
- expanded persona adapter registry/version lifecycle/rollback
- X-SARA foundation/adapter runtime headers
- deterministic trigger priorities and terminal/continue behavior
- alternate trigger response pools
- trigger-match logging
- expanded persona response-style variation
- Model Lab diagnostics export
- evaluation/deployment command-center concepts

Do NOT port its whole GUI, installer, startup, or application shell.

### 1.0.17 dataset and adapter management — QUARANTINED
Source: `sara-1.0.17-dataset-adapter-management`

Candidate features:
- dataset parent lineage and creation timestamps
- dataset snapshot browser/details
- dataset import/export
- duplicate-safe imported IDs
- adapter version selection
- selected-vs-active adapter comparison
- adapter metadata export
- training job created/start/completed timestamps
- training-run history with dataset/foundation lineage

### 1.0.18 evaluation suite — QUARANTINED
Source: `sara-1.0.18-evaluation-suite`

Candidate features:
- persistent evaluation-run registry
- candidate/foundation/adapter identity per evaluation
- persona/policy/style/memory/trigger/diversity dimensions
- named evaluation test cases and case-level results
- long-context memory test
- trigger regression tests
- prior-run/per-dimension regression detection
- evaluation reports and candidate comparison export

### Reusable persona profiles — QUARANTINED
Source: `sara-1.0.18.2-persona-profiles`

Candidate features:
- SQLite reusable persona profile library
- save/load/list/delete profiles
- clean New Persona workflow
- per-profile delay settings
- automatic assigned-LoRA loading
- persona-switch conversation reset
- profile list in Personas & LoRAs

### Persona memory isolation — QUARANTINED
Source: `sara-1.0.18.3-persona-memory-scope`

Candidate features:
- list conversations only for the selected persona
- resume prior conversation only for the selected persona
- recall memory only from the selected persona
- separate persona name from full persona summary
- cross-persona memory-leak regression test

### Channel core / jurisdiction gates — QUARANTINED
Source: `sara-1.0.18.3-channel-core`

Candidate features:
- provider-neutral channel accounts/conversations/events/messages/attachments
- adapter registry
- local SARA simulation adapter
- operator-approved media queue
- automation decision engine
- human-review gates for sensitive operational actions
- jurisdiction profile store
- operating-jurisdiction selection
- review-only fallback when jurisdiction profiles are not active/reviewed

### Deployment and rollback — QUARANTINED
Source: `sara-1.0.19-deployment-rollback`

Candidate features:
- persistent deployment/runtime package registry
- version locks
- activation and rollback
- exported package manifests
- full runtime-stack pinning
- deployment audit lifecycle
- full-stack deployment command center

### Visual-restoration branch — REFERENCE ONLY
Source: `sara-visual-restoration-1.0.15-baseline`

Do not use this branch as code source for the application shell, startup, splash, or installer.

It was an attempted reconstruction after the ancestry failure. The real 1.0.15 source is authoritative.

It may be consulted only for:
- screenshot tooling
- UI comparison notes
- historical documentation

## Branches that are not allowed as merge sources

Do not wholesale merge:
- `main`
- `sara-1.0.16-trainer-redesign`
- `sara-1.0.17-dataset-adapter-management`
- `sara-1.0.18-evaluation-suite`
- `sara-visual-restoration-1.0.15-baseline`
- `sara-1.0.18.2-persona-profiles`
- `sara-1.0.18.3-persona-memory-scope`
- `sara-1.0.18.3-channel-core`
- `sara-1.0.18.4-channel-core`
- `sara-1.0.18.3-channel-core-reconciled`
- `sara-1.0.19-deployment-rollback`
- validation branches

They remain only so no work is lost and individual commits/files can be inspected.

## Recovery workflow

Every future feature restoration must follow this exact sequence:

1. Start from the current `sara-recovery-from-1.0.15` head.
2. Identify exactly one feature from this ledger.
3. Compare its later implementation against the 1.0.15 implementation first.
4. Port only the minimum backend/source changes needed.
5. Do not replace startup, splash, installer, application shell, or approved UI.
6. Build Windows Release.
7. Run tests.
8. Package SARA.
9. Launch the packaged executable and prove it gets past splash.
10. Capture the actual application UI.
11. Validate installer/model-verification behavior.
12. Update this ledger to RESTORED only after all validation passes.
13. Only then begin the next feature.

## Current recovery status

- Trusted baseline identified: YES
- Exact 1.0.15 baseline locked: YES
- Later PRs closed/quarantined: YES
- Single active recovery branch: YES
- Recovery validation workflow: ACTIVE AND PASSING ON VALIDATED CHECKPOINTS
- Exact packaged 1.0.15 launch past splash: PASS
- Responsive packaged main window after splash: PASS
- Actual packaged UI capture: PASS
- Hybrid Model Lab shell/workspaces: RESTORED IN STAGES
- Persona memory isolation recovery: RESTORED / VALIDATED
- Subjects & Identity workflow: RESTORED / VALIDATED (Windows Build #754)
- Canonical subject / identity schema reconciliation: PASS
- Channels & Messaging operational adapter view: RESTORED / VALIDATED (Windows Build #759)
- Supervisor & Approvals takeover controls: RESTORED / VALIDATED (Windows Build #761)
- Evidence verification and Audit & Compliance: RESTORED / VALIDATED (Windows Build #764)
- Audit-v2 metadata binding: RESTORED / VALIDATED (Windows Build #768)
- Agency Server offline persistence: RESTORED / VALIDATED (Windows Build #770)
- Settings protected model/schema state: RESTORED / VALIDATED (Windows Build #772)
- Persona Rules & Learning visibility and deterministic trigger precedence: RESTORED / VALIDATED (Windows Build #774)
- Conversational Trainer persistence / review-before-apply: RESTORED / VALIDATED (Windows Build #781)
- Integrated Model Lab lifecycle / stale-job recovery: RESTORED / VALIDATED (Windows Build #804)
- Persona continuity / non-repetition / progressive typing: RESTORED / VALIDATED (Windows Build #815)
- Authorized Identity Research workspace / provenance-first task queue: RESTORED / VALIDATED (Windows Build #823)
- Identity Research provider registry / adapter boundary: RESTORED / VALIDATED (Windows Build #836)
- Identity Research encrypted report preservation: RESTORED / VALIDATED (Windows Build #838)
- Identity Research provider request/result exchange / portal assist: RESTORED / VALIDATED (Windows Build #844)
- Exact Model Lab approval/deployment evaluation-proof binding: RESTORED / VALIDATED (Windows Build #865)
- Reviewed correction targets / shadow-learning data integrity: RESTORED / VALIDATED (Windows Build #880)
- Persona LoRA training candidate isolation: RESTORED / VALIDATED (Windows Build #887)
- Foundation / Persona LoRA staged evaluation and activation proof: RESTORED / VALIDATED (Windows Build #906)
- Automatic sentinel-chat startup: RESTORED / VALIDATED (Windows Build #928)
- Canonical application versioning: RESTORED / VALIDATED (SARA 1.0.16 / Windows Build #949)
- Deterministic sentinel-chat model selection: RESTORED / VALIDATED (SARA 1.0.16 / Windows Build #949)
- Automatic sentinel-chat in-session self-recovery: RESTORED / VALIDATED (SARA 1.0.17 / Windows Build #960)
- Response-rule edit/paging management: RESTORED / VALIDATED (SARA 1.0.18 / Windows Build #980)
- Automatic sentinel-chat in-session self-recovery: RESTORED / VALIDATED (SARA 1.0.17 / Windows Build #960)
- Foundation / Persona LoRA audit provenance: RESTORED / VALIDATED (Windows Build #928)
- Wholesale post-1.0.15 branch merges: NONE


## Verified baseline source inventory correction — 2026-09-28

Direct reads from immutable commit `78a2aa3d30eb8cd647d1f4213f5d4e3961a6105d` confirm that several capabilities previously listed below as later-only/quarantined are already physically present in the trusted 1.0.15 source tree.

Present in the trusted 1.0.15 commit:
- provider-neutral channel core source/header
- jurisdiction rule store source/header
- reusable SQLite persona profile store
- persona profile migration
- foundation / persona-LoRA / trainer store
- foundation / persona-LoRA / trainer migration
- persistent persona conversation logs
- conversation memory store
- training review store and review lifecycle
- smart persona response-rule persistence
- trainer worker launch path

Therefore those baseline implementations must be preserved and improved in place. Later branches may still contain additional revisions, but they are not the origin of these capabilities and must not be wholesale merged.

### Recovery changes now implemented on top of 1.0.15

- [x] startup no longer hangs indefinitely behind the splash
- [x] packaged main window is required to become responsive before CI passes
- [x] exact 1.0.15 installer/model verification contract remains guarded
- [x] exact approved SARA logo asset is used in the recovered application shell
- [x] dedicated hybrid Model Lab navigation shell restored
- [x] Model Lab Overview rebuilt against real 1.0.15 state
- [x] Train workspace rebuilt against real trainer controls/backend
- [x] Datasets workspace built against the real training-review store
- [x] Personas & LoRAs workspace built against real persona profiles and LoRA bindings
- [x] Foundation Forks workspace built against real foundation lineage
- [x] Jobs workspace built against real trainer job records
- [x] Evaluation workspace built against the trusted model registry/evaluator
- [x] Deployment workspace built against trusted approve/activate/rollback registry behavior
- [x] persona archive/resume/relevant-memory recall isolated by selected persona
- [x] archived conversations now preserve a separate full persona summary
- [x] persona switches move to that persona's own conversation scope
- [x] case-scoped Subjects & Identity workflow with provenance-backed leads
- [x] explicit human lead review and separate subject confirmation state
- [x] recovery-only subject tables reconciled into canonical `subjects` / `subject_identities` storage
- [x] provider-neutral channel adapter registry surfaced in Channels & Messaging
- [x] local SARA simulation transport bridged into the canonical adapter registry
- [x] channel readiness matrix and human-approved routing gate added without enabling third-party transports
- [x] draft jurisdiction profiles render as auto-send locked rather than operationally ready
- [x] persistent investigator takeover and separate approve/reject supervisor controls
- [x] evidence current-byte verification with audited PASS/FAIL integrity state
- [x] audit-v2 metadata digest binding while preserving legacy audit-v1 verification
- [x] Agency Server configuration and sync queue persist locally without enabling network transport
- [x] Settings surfaces applied schema and protected local-model verification-marker state without blocking startup
- [x] saved persona response rules visible and manageable inside Personas / Rules & Learning
- [x] deterministic response-rule precedence with Exact > Contains > Smart and newest-rule true-tie behavior
- [x] persistent persona/mode-scoped Trainer conversations with preview-before-apply Behavior tuning
- [x] Trainer intent captured for Dataset, Persona LoRA, Foundation Fork and Evaluation/Test modes without live weight mutation
- [x] persona-scoped approved training export prevents cross-persona dataset leakage
- [x] automatic reviewed-dataset and per-run output path preparation
- [x] isolated Trainer environment with explicit CUDA QLoRA preparation
- [x] deployable Foundation Fork GGUF generation plus activation/rollback history
- [x] evaluation/runtime-stack gating for approval and deployment
- [x] core model approval persists exact passing evaluation / foundation / adapter proof
- [x] model activation and rollback reject unproven legacy approvals
- [x] deployment preparation requires the exact evaluation proof used to approve the model
- [x] correction instructions generate separate reviewable training targets instead of relabeling original replies
- [x] corrected targets reset to PENDING when edited and only approved targets export as supervised JSONL output
- [x] versioned datasets preserve correction provenance and supersede stale approved targets for future snapshots
- [x] completed Persona LoRA training registers an inactive candidate and cannot replace the active adapter automatically
- [x] CI executes a Persona LoRA candidate-isolation self-test against SQLite worker behavior
- [x] staged Foundation/LoRA evaluation loads candidate runtime files without changing persistent active bindings
- [x] Foundation and Persona LoRA activation require persisted passing evaluation proof
- [x] LoRA activation requires its evaluated Foundation to be the active Foundation
- [x] sentinel-chat automatically starts and connects during normal SARA loading; Browse/Connect is not required
- [x] automatic local-AI startup is bounded and falls back safely rather than hanging behind the splash
- [x] active Foundation/LoRA stacks retain the stable sentinel-chat API alias
- [x] automatic startup preserves saved model tuning values
- [x] Foundation/LoRA evaluate/approve/activate/rollback lifecycle changes are recorded in Audit & Compliance
- [x] newly trained Persona LoRAs are registered inactive and cannot silently replace the live adapter
- [x] CI worker self-test proves the existing active LoRA survives candidate registration unchanged
- [x] rule hit ledger, alternate response pools and terminal/continue behavior
- [x] Persona LoRA exact-version activation/compare/rollback history
- [x] trainer worker heartbeat and stale-job-only recovery
- [x] cross-conversation prior-question suppression and short-answer context preservation
- [x] durable persona continuity notes reused across conversations and exposed for review
- [x] progressive human-like typing render plus Direct2D flicker suppression
- [x] stable Qwen3.5-capable Trainer dependency floor
- [x] case-scoped authorized Identity Research queue with provenance, confidence and separate lead promotion/review
- [x] research findings cannot automatically confirm a subject identity
- [x] provider metadata / credential-reference registry with disabled-by-default API/portal templates
- [x] separate provider-adapter execution contract; metadata alone cannot enable automatic research
- [x] research reports can be preserved as encrypted case evidence with provenance and non-confirmation notice
- [x] credential-free provider request/result packages with strict task/provider/provenance matching
- [x] imported provider result packages preserved as encrypted evidence before task completion
- [x] HTTPS-only provider portal assist with no case/query/credential injection

### Still genuinely later-only / not yet restored

- persistent multi-run Evaluation Suite registry and detailed named-case reports
- dataset snapshot lineage/import/export beyond the trusted approved-JSONL review store
- richer adapter version-management metadata/export beyond existing persona LoRA bindings
- persistent deployment package registry/version locks/full-stack package manifests
- later deployment audit lifecycle extensions
- later Model Lab diagnostic/export refinements not already present in 1.0.15

Those remain candidates for selective porting only after their individual implementations are reviewed against the recovered code.


## Architecture correction checkpoint — 2026-09-28

The product hierarchy is now locked by `docs/SARA_PRODUCT_ARCHITECTURE.md`.

Validated rollback point:
- commit `5ce02765cff5b739cb4dd88d1a1c4928f136a821`
- Windows Build #730 / run `36401709869`
- result: PASS
- full details: `docs/SARA_RECOVERY_CHECKPOINTS.md`

The permanent SARA main navigation is now:
1. Dashboard
2. Cases
3. Subjects & Identity
4. Simulation Chat
5. Personas
6. Channels & Messaging
7. Supervisor & Approvals
8. Evidence
9. Audit & Compliance
10. Model Lab
11. Agency Server
12. Settings

Model Lab is subordinate and retains only internal Model Lab navigation. Trainer and Verification are sub-workflows, not permanent top-level application modules.

### Selectively restored later-only capabilities now present on the recovery line

The following were manually reviewed and selectively restored without wholesale branch merges:

- [x] persistent multidimensional Evaluation Suite and evaluation-run registry
- [x] named evaluation cases, dimension scores, regression deltas and report/comparison export
- [x] versioned dataset snapshots with lineage
- [x] dataset snapshot import/export with duplicate-safe IDs
- [x] persona LoRA version selection/reactivation by exact binding ID
- [x] persona LoRA metadata JSON export
- [x] trainer job created/start/completion/error history surfaced from the existing worker
- [x] deployment package registry
- [x] deployment version locks
- [x] deployment manifest export
- [x] deployment package activation and rollback
- [x] deployment lifecycle audit action IDs
- [x] Model Lab diagnostics export
- [x] persona-scoped archive/resume/relevant-memory recall
- [x] full packaged screenshot regression across the permanent investigative shell and Model Lab subpages

No later application shell, startup implementation, splash implementation, or installer implementation was wholesale merged.

### Current architectural recovery status

- trusted 1.0.15 immutable source: PROTECTED
- single active recovery branch: YES
- product mission contract in repo: YES
- permanent navigation contract enforced in CI: YES
- Simulation Chat first-class and captured from packaged EXE: YES
- Cases/Evidence/Audit operational modules captured from packaged EXE: YES
- Model Lab prevented from replacing main sidebar: YES
- transparent approved logo without white plate: YES
- packaged Windows build/tests/startup/installer checkpoint: PASS
- Subjects & Identity canonical persistence and packaged UI checkpoint: PASS — run #754 / commit `cb934c5349e1666b1c64092b7ebaf1cb4a853ada`
- Channels & Messaging adapter-registry and packaged UI checkpoint: PASS — run #759 / commit `eb0709b4cd64ae705453aed99a172756caaa9855`
- Supervisor takeover checkpoint: PASS — run #761 / commit `f9d849e271bf707570eb681836e91dd18a3405d2`
- Evidence/Audit checkpoint: PASS — run #764 / commit `e686dd6dc73155c40828c7c9330ac959aba61b39`
- Audit-v2 metadata checkpoint: PASS — run #768 / commit `842ac592a1b39fba241dd5b566dd764274a2a024`
- Agency Server offline persistence checkpoint: PASS — run #770 / commit `d25c117e531f28fd873ea163ad9e771521f03f66`
- Settings checkpoint: PASS — run #772 / commit `4764220f9a46fd72913d6e060d05023635389fa6`
- Persona Rules & Learning checkpoint: PASS — run #774 / commit `dec2a20ab770572e82e5a2bdc882e07c4b1b6f6c`
- Conversational Trainer checkpoint: PASS — run #781 / commit `c8d0fb6ba902e01880b0ec460c748b79635abc80`
- Integrated Model Lab lifecycle checkpoint: PASS — run #804 / commit `a701d7986abf6a80447dbfca7a1ee8160298ea3c`
- Persona continuity / typing checkpoint: PASS — run #815 / commit `2c129f537c078bb839b9feda1e1c4bc3192ad3f4`
- Identity Research checkpoint: PASS — run #823 / commit `8f810dfb9aecb28115e6c95a040bf75bc34d1e11`
- Identity Research provider registry checkpoint: PASS — run #836 / commit `6bdfa8aabe8ebc5445f661d4917e6c070fa78122`
- Identity Research evidence-preservation checkpoint: PASS — run #838 / commit `28a53999bc1356eb253404749b1950631118ab6e`
- Identity Research exchange checkpoint: PASS — run #844 / commit `3d6aad7fd79fea446df71a676618eacf15eeda7d`
