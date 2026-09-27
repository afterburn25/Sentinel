# SARA 1.0.18.3 — Reconciled Persona Memory + Channel Core

Date: 2026-09-27

## Purpose

SARA 1.0.18.3 reconciles the restored SARA 1.0.15 visual baseline and reusable 1.0.18.2 persona-profile line with the validated provider-neutral channel/jurisdiction core and the separate persona-memory-scope work.

## Integrated changes

- Persona-scoped conversation archive, resume, and relevant-memory recall.
- Separate stored persona name and full persona summary.
- Normalized channel accounts, conversations, raw events, messages, attachments, memory facts, migrations, and automation decisions.
- SARA local simulation channel adapter.
- Operator-approved text/media bridge.
- Automation decision gates with human review for identity linking, channel migration, meeting arrangement, money/payment, media, and sensitive actions.
- Jurisdiction rule/profile store and operating-jurisdiction selection.
- Review-only fallback when jurisdiction rules are missing, draft, unreviewed, or expired.
- Release-safe regression coverage for persona-memory isolation and channel automation gates.
- SARA 1.0.18.3 executable, CI, installer, updater, manifest, and runtime identity.

## Migration order

1. `0013_conversation_persona_scope.sql`
2. `0014_unified_channel_core.sql`
3. `0015_jurisdiction_rules.sql`

This ordering resolves the migration-number collision that existed between the separate persona-memory and channel-core branches.

## Branch lineage

Current integration branch:

`sara-1.0.18.3-channel-core-reconciled`

It starts from the latest `sara-1.0.18.2-persona-profiles` head.

Superseded intermediate integration branches:

- `sara-1.0.18.3-persona-memory-scope`
- `sara-1.0.18.4-channel-core`

PR #8 / `sara-1.0.18.3-channel-core` remains the source of the previously validated channel/jurisdiction implementation, but its branch predates the final 1.0.18.2 release/documentation commits and therefore is not the clean integration tip.

## Validation status

Pending full Windows compile, unit/integration tests, SQLite dependency verification, package generation, actual packaged UI screenshot capture, SHA-256 verification, Inno Setup installer compilation, actual installer screenshot capture, and artifact upload for the reconciled branch.

No merge into the visual-restoration chain should occur until the reconciled branch passes those gates and its actual screenshots are reviewed for visual regression.
