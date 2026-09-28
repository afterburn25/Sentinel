# SARA Product Architecture Contract

Status: LOCKED
Date: 2026-09-28
Applies to: `sara-recovery-from-1.0.15` and every descendant/release built from it

## Product mission

SARA — Synthetic Adaptive Response Agent — is an investigator-controlled synthetic-decoy, case, evidence, messaging, supervision, and operational platform.

AI training is a supporting capability. Model Lab is one module inside SARA; it is not the product shell and may never replace, hide, or redefine the main investigative application.

## Permanent main navigation

The primary SARA sidebar is locked to these twelve first-class modules, in this order:

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

This sidebar remains visible while any module is open, including every Model Lab subpage.

## Module boundaries

### Investigative / operational core
- Dashboard
- Cases
- Subjects & Identity
- Simulation Chat
- Personas
- Channels & Messaging
- Supervisor & Approvals
- Evidence
- Audit & Compliance
- Agency Server

### AI development support
Model Lab contains its own internal navigation:
- Overview
- Train
- Datasets
- Personas & LoRAs
- Foundation Forks
- Jobs
- Evaluation
- Deployment

Trainer is not a top-level SARA module. It is the Train workspace inside Model Lab.

### Verification
Verification is not a top-level SARA module. Evidence authentication and audit-chain verification belong under Evidence and Audit & Compliance. The existing Verification page/backend may remain as a sub-workflow and may be opened from Evidence or Audit & Compliance.

## Simulation Chat is protected

Simulation Chat is a primary SARA workflow and must retain:
- synthetic conversation transcript
- send/composer controls
- emoji support
- attachment/media support
- persistent conversations
- previous-conversation loading
- persona selection and persona memory
- response delay/typing behavior
- response rules
- investigator suggestions
- transcript preservation into evidence
- supervisor/approval integration

A Model Lab redesign must not remove, hide, rename away, or repurpose Simulation Chat.

## Case/evidence protection

The following capabilities may not be removed by AI/model/training work:
- case creation/opening and case metadata
- encrypted evidence import
- evidence verification
- audit ledger and integrity verification
- chain-of-custody related records
- transcript preservation
- local/offline encrypted storage

## Architecture invariants

1. Main SARA navigation is global and permanent.
2. Model Lab never owns or replaces the SARA sidebar.
3. Model Lab subpages stay inside Model Lab.
4. Trainer and Verification are sub-workflows, not top-level modules.
5. Simulation Chat remains first-class.
6. Investigative modules remain reachable after every build.
7. UI redesign may move a feature only within its defined module boundary; it may not silently delete it.
8. Startup, splash, installer, model verification, and encrypted local storage remain protected by their existing recovery contracts.
9. Every substantial UI change must be validated against the packaged `SARA.exe`, not only compilation/unit tests.
10. CI must fail if the permanent main navigation contract disappears or if protected modules are no longer dispatched by the application.

## Current recovery rule

No new feature work should override this contract. If a requested feature conflicts with it, the feature must be fitted underneath the appropriate module or the contract must be explicitly revised first.
