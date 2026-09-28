# SARA Recovery Checkpoints

This file records validated recovery commits. A checkpoint is not merely a commit that compiled; it is a commit that passed the recovery guards, Windows build/tests, packaged launch, real UI capture, package verification, and installer build.

## Checkpoint 01 — Investigative product shell restored

Validated commit:

`5ce02765cff5b739cb4dd88d1a1c4928f136a821`

GitHub Actions:

- Workflow: Windows Build
- Run: #730
- Run ID: `36401709869`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- no SQLite DLL dependency: PASS
- packaged install: PASS
- packaged SARA launch past splash: PASS
- responsive main window: PASS
- full investigative-shell UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Product architecture proven by this checkpoint:

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

Model Lab remains a contained module with internal tabs and does not replace the main SARA sidebar.

Simulation Chat remains present as a first-class workflow with persistent conversation state, composer, send action, and the recovered synthetic-conversation UI.

Additional fixes included in this checkpoint:

- exact approved SARA logo renders without the white/light plate
- Simulation Chat emoji and attachment controls use stable vector icons rather than broken Unicode glyph rendering
- Dashboard is an operational command hub rather than a Model Lab/training dashboard
- Subjects & Identity is restored to the main product architecture as an investigative module boundary
- Trainer and Verification are no longer top-level navigation modules

Packaged UI artifact captured these real `SARA.exe` screens:

- `01-splash.png`
- `02-dashboard.png`
- `03-cases.png`
- `04-subjects-identity.png`
- `05-simulation-chat.png`
- `06-personas.png`
- `07-channels-messaging.png`
- `08-supervisor-approvals.png`
- `09-evidence.png`
- `10-audit-compliance.png`
- `11-model-lab-overview.png`
- `12-model-lab-train.png`
- `13-model-lab-datasets.png`
- `14-model-lab-personas-loras.png`
- `15-model-lab-foundations.png`
- `16-model-lab-jobs.png`
- `17-model-lab-evaluation.png`
- `18-model-lab-deployment.png`
- `19-agency-server.png`
- `20-settings.png`

Artifact digests from run #730:

- Windows package: `sha256:f9b452d8a96409dd79c5a956e667f4916ad6c674da4ea23d3f7455bdd70a4615`
- Setup artifact: `sha256:9a51460bd3a978da803a17044e694b0406ad0d8c37a3dfe2f02a8c586470d786`
- UI screenshots: `sha256:26ed9b786eabe2d608ad0964d495e24d05370d56b74642a6cedc5ea9b21336fc`

If later work damages the application architecture, this commit is the first validated rollback point after the architecture correction.

## Checkpoint 02 — Subjects & Identity unified and validated

Validated commit:

`cb934c5349e1666b1c64092b7ebaf1cb4a853ada`

GitHub Actions:

- Workflow: Windows Build
- Run: #754
- Run ID: `36413928944`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- no SQLite DLL dependency: PASS
- packaged install: PASS
- packaged SARA launch past splash: PASS
- responsive main window and full UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Subjects & Identity recovery proven by this checkpoint:

- case-scoped subject profiles persist legal-name, alias, username, contact-identifier and investigator-note fields
- identity leads persist source type, source reference, finding, confidence and provenance
- lead verification/rejection is a human review action and does not automatically mark the subject identity confirmed
- explicit subject confirmation remains a separate investigator action
- the recovery-only `investigation_subjects` and `subject_identity_leads` tables are reconciled into the canonical 1.0.15 `subjects` and `subject_identities` tables
- runtime reads/writes now use only the canonical subject/identity schema
- subject deletion uses a transaction so related identity-row cleanup is rolled back if the subject itself cannot be deleted
- subject and identity lifecycle actions are represented in Audit & Compliance
- the packaged `SARA.exe` exposes Subjects & Identity as the third permanent investigative module without replacing or hiding the 1.0.15 shell

Artifact digests from run #754:

- Windows package: `sha256:f8ccd348e20c4a048d6fc5107b3ea815c3b11e7c70d73aeaa6cc911134199c0d`
- Setup artifact: `sha256:b1713ef20a6cd755d9087cd198d9595dac0879a18591897c992f71c02fd1502a`
- UI screenshots: `sha256:647f5718616396c8e039fbaa48523c8fdf2bd22c712ee12741e9d29d79a05bc8`

This is the validated rollback point for the recovered operational shell plus the canonical Subjects & Identity workflow.

## Checkpoint 03 — Channels & Messaging operationalized and validated

Validated commit:

`eb0709b4cd64ae705453aed99a172756caaa9855`

GitHub Actions:

- Workflow: Windows Build
- Run: #759
- Run ID: `36415535221`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- no SQLite DLL dependency: PASS
- packaged install: PASS
- packaged SARA launch past splash: PASS
- responsive main window and full UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Channels & Messaging recovery proven by this checkpoint:

- the main Channels & Messaging page is now an operational module rather than a local-test placeholder
- the existing provider-neutral `ChannelAdapterRegistry` is surfaced in the investigative shell
- the existing local simulation transport is bridged into that registry without duplicating the approval queue
- adapter connection state and declared text/media/automation capabilities are visible
- routing state shows current case, subject, jurisdiction and required human approval
- draft/unapproved jurisdiction profiles are visibly shown as locked rather than ready
- channel readiness is shown for local simulation, SMS/MMS/RCS, Telegram/Discord, Messenger/WhatsApp and assisted channels
- no live third-party transport is enabled by this checkpoint
- third-party adapters remain subordinate to jurisdiction and supervisor gates
- the approved local queue and Simulation Chat / Approvals workflow remain inside the recovered permanent SARA shell
- packaged `07-channels-messaging.png` was inspected for overlap and clipping at the regression-capture window size

Artifact digests from run #759:

- Windows package: `sha256:3e8450b72569c790e3cbf33cbdd43b0d38b98d02bc8d8ddb5c1a4a2723eed8fb`
- Setup artifact: `sha256:66ea84e8d0c695d1714f830d8bc612b3a3d40c3e4d4d21ad9248a1c991e719fe`
- UI screenshots: `sha256:6933fd7302dcf7c73e1d8fea1e50088d4bd7e524747dccc180dc03578a5c907e`

This is the validated rollback point for the investigative shell, canonical Subjects & Identity workflow, and operational Channels & Messaging adapter-readiness workflow.

## Checkpoint 04 — Supervisor, approvals, and investigator takeover validated

Validated commit:

`f9d849e271bf707570eb681836e91dd18a3405d2`

GitHub Actions:

- Workflow: Windows Build
- Run: #761
- Run ID: `36416759650`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- real packaged UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Supervisor & Approvals recovery proven by this checkpoint:

- pending, approved, and rejected approval states are exposed as distinct operator controls
- approve-next and reject-next workflows are separate human actions
- approval ledger keeps the exact reviewed-action hash visible
- investigator takeover state is persisted instead of being a display-only placeholder
- takeover is scoped to an open case and remains off when no case is active
- takeover activation/deactivation is represented in the audit lifecycle
- the permanent SARA shell remains unchanged

Artifact digests from run #761:

- Windows package: `sha256:40509d1d45efb2a330ef4ff598b253b0b509f3efca4e6e29a8d3c6c9f391083b`
- Setup artifact: `sha256:da28b1a90d2d27c0af55f9faab51701350869c4f01032fd5431608c27531de2b`
- UI screenshots: `sha256:338a7178dc17d673134a1b4493725ec52d2b8cc6ceedb30d9d2462b9790c4e53`

This is the validated rollback point for the operational shell, canonical Subjects & Identity, Channels & Messaging, and persistent human takeover/approval controls.

## Checkpoint 05 — Evidence verification and Audit & Compliance truthfulness validated

Validated commit:

`e686dd6dc73155c40828c7c9330ac959aba61b39`

GitHub Actions:

- Workflow: Windows Build
- Run: #764
- Run ID: `36420876336`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- real packaged UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Evidence / verification recovery proven by this checkpoint:

- evidence is no longer displayed as "Verified" merely because it exists
- each evidence item distinguishes never checked, last check passed, and recorded integrity failure
- verification re-reads the current stored bytes rather than trusting a historical label
- verification validates SEV container structure
- verification compares the current container SHA-256 against the imported record
- AES-256-GCM authentication/decryption must succeed
- decrypted plaintext SHA-256 must match the imported original hash
- successful verification records `EvidenceVerified` in the audit ledger
- failed verification records `EvidenceIntegrityFailure` in the audit ledger
- the Verification screen no longer treats the text `INVALID` as containing a valid `VALID` state
- a regression test verifies a good container, corrupts its stored bytes, verifies rejection, and confirms the audit chain remains cryptographically valid
- Audit & Compliance surfaces evidence-integrity alert counts and no longer paints row status green when the global audit chain fails
- the recovered Subjects & Identity block remained intact after the Evidence UI changes

Artifact digests from run #764:

- Windows package: `sha256:c3ba0dd9224dbd226e57b6bb1e962ef6ae10c512d3c8e54686a4e9cfd7eb1569`
- Setup artifact: `sha256:37f69e67d70c853455db508e90cad3fa14c39a1794a297056ee727822f332dcf`
- UI screenshots: `sha256:ecfa09b83009dbd5d5f110f21c3ec7160ac18070cfd94df9002f8890a9c34f76`

This is the validated rollback point for the recovered investigative shell through Evidence verification and Audit & Compliance.

## Checkpoint 06 — Audit-v2 metadata binding validated

Validated commit:

`842ac592a1b39fba241dd5b566dd764274a2a024`

GitHub Actions:

- Workflow: Windows Build
- Run: #768
- Run ID: `36422692168`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- real packaged UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Audit-v2 recovery proven by this checkpoint:

- existing immutable audit-v1 records continue to verify under their original canonical format
- new audit records store a SHA-256 digest of the metadata blob
- new audit-v2 record hashes cryptographically bind that metadata digest
- mixed legacy-v1 and new-v2 chains verify as one continuous chain
- direct SQLite modification of audit-v2 metadata is detected by `VerifyChain()`
- the migration does not rewrite or rehash historical v1 records
- Settings accurately describes new metadata-bound audit records without claiming legacy records were rewritten

Artifact digests from run #768:

- Windows package: `sha256:27e991672b1f5f58020c6af6b0ec6c5f1cb416f8a3ecc40d1b05c8d86f1c8b4f`
- Setup artifact: `sha256:c028663f0563fb93064add09ed9a48aea6cf58b0f36590832b50991dbcec7a09`
- UI screenshots: `sha256:b1441521282fbcce133931720c37f66b57992e14bfe90b41ca8fdff5e282da2b`

This is the validated rollback point for the recovered shell through metadata-bound audit provenance.
