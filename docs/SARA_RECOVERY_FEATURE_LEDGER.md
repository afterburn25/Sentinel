# SARA Recovery Feature Ledger

Date: 2026-09-27

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
- Recovery validation workflow: RUNNING
- Exact packaged 1.0.15 launch past splash: PENDING
- Actual 1.0.15 UI capture: PENDING
- User visual confirmation of recovered UI: PENDING
- Any post-1.0.15 feature ported: NO
