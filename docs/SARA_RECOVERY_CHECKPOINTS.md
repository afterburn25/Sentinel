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

## Checkpoint 07 — Agency Server offline persistence validated

Validated commit:

`d25c117e531f28fd873ea163ad9e771521f03f66`

GitHub Actions:

- Workflow: Windows Build
- Run: #770
- Run ID: `36423600498`
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

Agency Server recovery proven by this checkpoint:

- Agency Server configuration persists locally across application restarts
- endpoint, agency ID, workstation ID and enabled/disabled configuration state are stored in SQLite
- enabling Agency configuration does not establish or imply a network connection
- persistent sync-queue items survive process restart
- pending work counts are read from durable storage rather than only process memory
- queued audit snapshots remain local until a future authenticated transport explicitly acknowledges them
- the Agency page clearly states transport is inactive and local-first
- the operating-jurisdiction selector remains visible and reference profiles remain DRAFT unless legally reviewed/activated
- no live Agency Server network transport or credentials were introduced by this checkpoint

Artifact digests from run #770:

- Windows package: `sha256:ea5d7b53045b85d01bc0ea5cd34965eef2a783d489e127121e6c6c5c78c2c7f0`
- Setup artifact: `sha256:0fec3c675715853ddbc0542d7d2ece289ea46d62052e07566ddd0e58b627afe5`
- UI screenshots: `sha256:f88184dad6fccd91c151e1b0ffeb128483eddfed0851fed477f35e502fbae3fc`

This is the validated rollback point for the recovered investigative shell through persistent offline Agency Server staging.

## Checkpoint 08 — Settings and protected local-model state validated

Validated commit:

`4764220f9a46fd72913d6e060d05023635389fa6`

GitHub Actions:

- Workflow: Windows Build
- Run: #772
- Run ID: `36440609343`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- real packaged UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Settings recovery proven by this checkpoint:

- Settings reports the currently applied SQLite migration version
- local model/runtime installation state is shown separately from model-connection state
- Settings reads the protected installer `.sha256` verification marker without re-hashing the 5.68 GB model on the UI/startup thread
- the marker is compared against the exact protected 1.0.15 model SHA-256 contract
- missing model, missing marker, mismatched marker, and current marker are distinct states
- AI Diagnostics includes the installer verification-marker state
- the previous clipped multi-sentence Local AI status line was removed from the compact Settings card
- full model verification/download behavior remains owned by the protected 1.0.15 installer and recovery guard
- release-security rows describe evidence integrity, audit-v2, model installer verification, HTTPS update checking, outbound approval, and code-signing state without overstating signing status

Artifact digests from run #772:

- Windows package: `sha256:d3e90369bb25ded92803a3c65b63e5a35215e6aaa7ac7fb3be3b7c9398059e45`
- Setup artifact: `sha256:a0cfe5d10ec4c7eb20223da00939cd6be49bc1c722265234661b646442f52477`
- UI screenshots: `sha256:ca86a9424a497cd7fa5cf60c5a3daeb6a2f900b555a3c0cf53036c86b9425875`

This is the validated rollback point for the full recovered permanent operational shell through Settings.

## Checkpoint 09 — Persona Rules & Learning and deterministic trigger precedence validated

Validated commit:

`dec2a20ab770572e82e5a2bdc882e07c4b1b6f6c`

GitHub Actions:

- Workflow: Windows Build
- Run: #774
- Run ID: `36442718859`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- Persona Rules & Learning architecture guard: PASS
- Windows MSVC x64 Release build: PASS
- response-rule matcher unit tests: PASS
- core/platform/CLI tests: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- dedicated packaged Persona Rules & Learning UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Persona response-rule recovery proven by this checkpoint:

- Personas now contains a first-class internal `Rules & Learning` tab
- saved rules are visible with rule ID, match type, wording mode, priority, trigger and response preview
- rules can be opened back into the editor, enabled/disabled, or deleted
- Smart, Contains and Exact rule creation are all visible in the normal workflow
- exact wording vs persona-voice variation is visible and operator-controlled
- Learning Mode is visible in the same persona-scoped workspace
- Model Lab Overview links into the dedicated Persona rules workspace instead of hiding rules behind a count
- matching precedence is deterministic: explicit priority first, then Exact > Contains > Smart, then score, specificity, and newest-rule tie break
- a newly added equal-priority rule wins a true tie instead of being silently shadowed by the oldest rule
- a 100% Smart match no longer short-circuits a same-priority Exact rule
- unit tests cover punctuation normalization, typo/contraction smart matching, unrelated-input rejection, match-type precedence, rule priority and newest-rule tie behavior
- CI captures `06b-persona-rules-learning.png` from the packaged executable

Artifact digests from run #774:

- Windows package: `sha256:877ce53aacf6eb7d01ae81fa335fca95087697918bf9b5b3507e2755542e26a7`
- Setup artifact: `sha256:1579355ec19dd2119ea6224ebd9be0f22ec8ba1395632161be5bd390730ec2c5`
- UI screenshots: `sha256:3c542edcfa9bf6c744d6f8486ba0a08488ab349e2b947e6b73b8dd4e9f5f0a63`

This is the validated rollback point through visible, deterministic persona response rules.

## Checkpoint 10 — Conversational Trainer persistence and review-before-apply workflow validated

Validated commit:

`c8d0fb6ba902e01880b0ec460c748b79635abc80`

GitHub Actions:

- Workflow: Windows Build
- Run: #781
- Run ID: `36448925218`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- conversational Trainer architecture guard: PASS
- Windows MSVC x64 Release build: PASS
- Trainer dialogue persistence/unit tests: PASS
- core/platform/CLI tests: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- packaged Trainer UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Conversational Trainer recovery proven by this checkpoint:

- Trainer conversations persist in SQLite by persona and training mode
- investigator/trainer and SARA Trainer turns are retained as an actual conversation history
- starting a new session archives the prior active session instead of deleting it
- Behavior Tuning produces a preview payload rather than silently changing the persona
- Behavior previews require an explicit Apply action before profile parameters are saved
- applied preview state persists and is visible in Trainer history
- Dataset/Correction instructions can stage the most recent reviewed Simulation reply
- Persona LoRA and Foundation Fork goals are captured in Trainer conversation before queueing jobs
- Evaluation/Test intent is captured without pretending unsupported Preference/DPO weight training is active
- queued jobs can carry the originating Trainer dialogue session ID
- the existing active model/foundation/LoRA runtime is not modified merely by chatting with the Trainer
- the packaged window keeps the permanent SARA sidebar and Model Lab tab structure intact

Artifact digests from run #781:

- Windows package: `sha256:49d65a58015d72897578229c6890a894a925aefa4b4b39e953ab6e7bf4c73378`
- unsigned-development Setup artifact: `sha256:e38d47fc659b19144f5585b2118069ca7ced2b6a42852665ca547ee7470feb56`
- UI screenshots: `sha256:faae882b11195601706004d75d01cda580f0f88b6d23300c65905661cc6bf977`

This is the validated rollback point through the persistent conversational Trainer workflow.

## Checkpoint 11 — Integrated Model Lab lifecycle and stale-job recovery validated

Validated commit:

`a701d7986abf6a80447dbfca7a1ee8160298ea3c`

GitHub Actions:

- Workflow: Windows Build
- Run: #804
- Run ID: `36461946383`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- Trainer worker Python syntax validation: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- full packaged UI regression capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Integrated recovery proven by this checkpoint:

- approved review exports can be scoped to one persona so one persona's training data cannot leak into another persona's dataset
- Trainer Queue can auto-create reviewed JSONL dataset paths and per-run output paths instead of requiring manual path construction
- the default foundation separates the protected runtime GGUF from the trainable `Qwen/Qwen3.5-9B` source
- isolated Trainer environment setup and explicit PyTorch/CUDA preparation are packaged with SARA
- 9B LoRA/foundation training uses a 4-bit QLoRA profile with CUDA/VRAM preflight
- Foundation Fork training produces a merged native checkpoint and a deployable Q4_K_M GGUF runtime
- Foundation activation history is persisted and supports compare / activate / rollback without modifying the immutable original base
- model approval and deployment are gated by persisted evaluation state and the evaluated runtime stack
- foundation fork versioning follows parent lineage rather than reusing ambiguous version numbers
- persona response-rule match events are persisted and surfaced as hit counts
- response rules support alternate response pools plus terminal-vs-continue flow
- Persona LoRA activation history persists and supports exact-version compare / activate / rollback
- Trainer jobs persist heartbeat state; only stale RUNNING jobs are recoverable instead of treating every interrupted process as abandoned
- worker heartbeat updates protect legitimately running training jobs from false recovery
- the permanent SARA 1.0.15-derived shell, startup, installer contract, splash, and main navigation remain protected

Artifact digests from run #804:

- Windows package: `sha256:f6ebe315755fec4b01c5b3938ada44991b7b1fbf9dffd816b4915da249fdadc2`
- unsigned-development Setup artifact: `sha256:8e0193dbda2dc507734eba8357d4656136c2159f18b3ff1b1b613c660fb8db2c`
- UI screenshots: `sha256:7bb30a184ad73831a5e49265d51d04700feaf0f6e9aa36cf3fb8a46fa1d51421`

This is the current consolidated rollback point for the recovered SARA operational shell and Model Lab lifecycle.

## Checkpoint 12 — Persona continuity, non-repetition, progressive typing, and Trainer stability validated

Validated commit:

`2c129f537c078bb839b9feda1e1c4bc3192ad3f4`

GitHub Actions:

- Workflow: Windows Build
- Run: #815
- Run ID: `36470959719`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- Trainer worker Python syntax validation: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- full packaged UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Behavior / memory recovery validated by this checkpoint:

- persona memory tracks prior asked questions across conversations so SARA can avoid repeating already-answered prompts
- short participant answers preserve the surrounding question/context instead of becoming context-free memory fragments
- durable persona continuity notes can be reused across later conversations for the same persona
- transient/non-durable learning notes are filtered out rather than polluting long-term persona memory
- continuity notes are exposed for investigator review instead of remaining hidden model state
- Simulation typing is progressively rendered rather than appearing as an instantaneous full message
- Win32 background erasing is suppressed behind Direct2D to reduce the visible black/flicker bounce reported during the rollback period
- the Trainer dependency set now requires a stable Transformers build with Qwen3.5 support

Artifact digests from run #815:

- Windows package: `sha256:b791d3b5f8375805d4b506a4b1312a2754b34ee57f1e4bf1f5f7500d2194341f`
- unsigned-development Setup artifact: `sha256:c899e075d0f09bb60352fa1ba2aae08e82e5a4d6154df32bf5b2747490cc7a2f`
- UI screenshots: `sha256:602f51eeabe917cb3f12f9840158bda01c39ce49f230354d593be290be31be86`

This is the validated rollback point for the recovered SARA shell through the newer continuity/typing/Trainer-stability work.

## Checkpoint 13 — Authorized Identity Research workspace validated

Validated commit:

`8f810dfb9aecb28115e6c95a040bf75bc34d1e11`

GitHub Actions:

- Workflow: Windows Build
- Run: #823
- Run ID: `36503119293`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- deterministic packaged Identity Research UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Identity Research recovery proven by this checkpoint:

- research is case-scoped to a selected subject
- supported research intents are Public Records, Social Profile, Username, Contact, and Image Reference
- every queued task requires an authorized provider/source plus a case purpose/legal-basis note
- SARA stores research tasks instead of claiming to perform an unauthorized lookup automatically
- completed findings retain result reference and provenance
- completed research may be promoted only to an **unverified identity lead**
- lead confidence is investigator-supplied and bounded
- lead verification remains a separate human review action
- subject confirmation remains a separate explicit investigator action
- rejected research tasks cannot be promoted later
- research lifecycle actions are represented in Audit & Compliance
- deleting a subject transactionally removes its research tasks and identity records
- the packaged Research workspace visibly states that there are no automatic identity conclusions

Artifact digests from run #823:

- Windows package: `sha256:1721b6b55c05d4faf34bd71fb971fa5c8729e6f8bceb448ffa725f6d8af738ac`
- unsigned-development Setup artifact: `sha256:b64f5882f3984dfe16ee6d4e297865f5126b102846c6469a6fd1c56e2eb852d0`
- UI screenshots: `sha256:43b5fcb5c6028730882f487b3d924505ce12a3f96db7cfbe4543624e128fefec`

This is the validated rollback point through the authorized, provenance-first Identity Research workflow.

## Checkpoint 14 — Identity Research provider registry and adapter boundary validated

Validated commit:

`6bdfa8aabe8ebc5445f661d4917e6c070fa78122`

GitHub Actions:

- Workflow: Windows Build
- Run: #836
- Run ID: `36506809489`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- Identity Research/provider architecture guard: PASS
- Windows MSVC x64 Release build: PASS
- provider-registry and adapter-contract unit tests: PASS
- core/platform/CLI tests: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- deterministic packaged Research + Provider Registry UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Provider architecture proven by this checkpoint:

- Identity Research provider configuration is contained under Subjects & Identity / Research
- provider profiles store provider ID, display name, access mode, supported research types, endpoint/portal hint, credential reference, enabled state, and notes
- provider records store only credential aliases/vault references; no API keys, passwords, cookies, bearer tokens, or session credentials are required in case/provider metadata
- the built-in Manual / Authorized Source provider is enabled and non-executable by design
- API/portal templates ship disabled until explicitly reviewed/configured
- disabled registered providers cannot be used to queue research
- providers cannot be used for research types outside their declared capability mask
- the Provider Registry has explicit Save / Enable / Disable lifecycle and preserves the built-in manual provider
- a provider adapter contract exists separately from provider metadata
- adapter execution readiness requires a loaded adapter that reports configured + supported
- duplicate provider-adapter IDs are rejected
- provider metadata alone never makes a research provider executable
- packaged UI visibly reports `Executable adapters: 0 | metadata only` with no live provider adapters loaded
- the normal Research workspace remains available for investigator-performed authorized sources and still cannot auto-confirm identity

Artifact digests from run #836:

- Windows package: `sha256:6ad250ba8e31a379c890e4519d5b2fd76e02efdb0735aff729c969e04e0f375a`
- unsigned-development Setup artifact: `sha256:fcf06ca61196415640f481f0d5b2fae1da684fc300caf73dcbf47985402cdb43`
- UI screenshots: `sha256:3abe78529a3ab707ec3aa219d20a50c88c5426420c5cd5ccdf0f86da5a96c435`

This is the validated rollback point through the provenance-first Identity Research provider registry and disabled-by-default adapter boundary.

## Checkpoint 15 — Identity Research report preservation validated

Validated commit:

`28a53999bc1356eb253404749b1950631118ab6e`

GitHub Actions:

- Workflow: Windows Build
- Run: #838
- Run ID: `36507360542`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- Identity Research/provider architecture guard: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- full packaged UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Identity Research preservation proven by this checkpoint:

- completed, promoted, or rejected research tasks can be preserved as encrypted case evidence
- the preserved report carries case/subject IDs, research type, provider/source, original query/reference, legal-basis note, status, finding, result reference, provenance, review state, and timestamps
- preservation does not elevate a finding to verified identity or confirmed subject status
- temporary plaintext report material is removed after encrypted evidence import
- preservation receives its own audit event linking the research task to the generated evidence item
- the Evidence workspace refreshes when the preserved research belongs to the currently open case

Artifact digests from run #838:

- Windows package: `sha256:b0f082622c0f6c6e662af859c3e308752520666c2a5fc1bb1889527332484608`
- unsigned-development Setup artifact: `sha256:7dac0e0feb1225e7e41ecab5d67f89fe9a8df525e2ff819b7534e902a28f5487`
- UI screenshots: `sha256:db62627f179322ec8ba09469a0b57a9b7c1bf76bcca45196e4ff62fae6dede84`

This is the validated rollback point through encrypted preservation of authorized Identity Research results.

## Checkpoint 16 — Identity Research provider exchange and portal-assist workflow validated

Validated commit:

`3d6aad7fd79fea446df71a676618eacf15eeda7d`

GitHub Actions:

- Workflow: Windows Build
- Run: #844
- Run ID: `36517557714`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- Identity Research/package architecture guard: PASS
- Windows MSVC x64 Release build: PASS
- research request/result package unit tests: PASS
- HTTPS portal safety tests: PASS
- core/platform/CLI tests: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- deterministic queued Identity Research UI capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Identity Research exchange workflow proven by this checkpoint:

- queued research tasks can export a credential-free `.sara-research-request` package
- request packages contain case/subject/task/provider/type/query/legal-basis context but no credential-reference field, API key, password, bearer token, cookie, or session secret
- export also produces a blank provider result template
- result imports require exact task ID and provider ID matches plus a non-empty finding and provenance
- malformed or mismatched result packages are rejected
- the original imported provider-result file is preserved as encrypted case evidence before the research task is completed
- imported result provenance records the resulting evidence ID
- portal assist appears only for an enabled Portal provider with a safe HTTPS endpoint
- HTTP endpoints and HTTPS URLs with embedded user-info credentials are rejected
- portal launch does not inject the subject/query, credential alias, or secret into the URL
- portal launch receives its own audit event stating query/credentials were not injected
- the disposable packaged-UI fixture includes a queued Portal-provider task so CI captures Export Request / Import Result / Open Portal controls
- research findings remain unverified until the separate lead-review and subject-confirmation workflows

Artifact digests from run #844:

- Windows package: `sha256:fd0f2fc51c89aef67af3a6b27409df535d05926b97813025f8a430f27b5d50af`
- unsigned-development Setup artifact: `sha256:c3fdd05dcbcd0c400fe52ef3f437f4adbc65945b25a040902bb7c5c2575f7bef`
- UI screenshots: `sha256:410887117f78219810673c52857d6ba7b70326417239ecef8fde1399165fbf96`

This is the validated rollback point through credential-free provider exchange, encrypted result preservation, and no-data-injection portal assist.

## Checkpoint 17 — Exact Model Lab evaluation proof binding validated

Validated commit:

`80131dbd83a26962219fc7fbc5aae2af6f0c3b14`

GitHub Actions:

- Workflow: Windows Build
- Run: #865
- Run ID: `36522649886`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- Trainer worker Python syntax validation: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- packaged UI regression capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Evaluation / deployment proof recovery proven by this checkpoint:

- model approval is enforced inside the core ModelRegistry instead of only by GUI button flow
- approval requires a complete passing EvaluationRun for the exact candidate
- approval is bound to the evaluated Foundation Fork and Persona LoRA / adapter IDs
- the exact evaluation-run ID, foundation ID and adapter ID used for approval persist with the registered model
- model activation refuses approved-state records that do not carry persisted evaluation proof
- model rollback refuses to reactivate a previous model that lacks persisted approval proof
- legacy six-field model-registry rows that predate exact approval proof are safely demoted from APPROVED/ACTIVE to CANDIDATE on load so they must be reevaluated
- deployment preparation requires both an approved model and the exact passing EvaluationRun that approved that model
- a different passing evaluation for the same candidate cannot be substituted at deployment time
- deployment packages derive candidate/runtime identity from the validated model/evaluation objects rather than caller-provided strings
- deployment manifests explicitly record `"evaluation_gate": "PASS"`
- regression coverage rejects runtime-stack mismatch, failed required dimensions, unapproved deployment candidates, and mismatched approval-evaluation proof
- CI architecture guards now protect the core evaluation/deployment boundary against later regression
- the protected 1.0.15 startup, splash, installer, permanent application shell and navigation were not replaced

Artifact digests from run #865:

- Windows package: `sha256:3bb8465eac8d070c78178444931959ce2114f5c538df3a025cf2b46daf859167`
- unsigned-development Setup artifact: `sha256:7e70de2f8f32292d3e5a0ec96bf51fa4da2e00be2d667c82a6a6e832dadbc05f`
- UI screenshots: `sha256:0c652b4155fb33694204a033a446f39b2715022ff61073942a4152c14bd0bf32`

This is the validated rollback point for the recovered SARA shell through exact Model Lab approval/deployment evaluation-proof enforcement.

## Checkpoint 18 — Reviewed correction targets and shadow-learning integrity validated

Validated commit:

`9a0444d7c1c09c83b16f89bc89756efe55a5e971`

GitHub Actions:

- Workflow: Windows Build
- Run: #880
- Run ID: `36523940874`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- Trainer worker Python syntax validation: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- packaged UI regression capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Reviewed-correction / shadow-learning integrity proven by this checkpoint:

- Correction mode no longer treats the original model reply as the corrected training target
- the model-adapter contract has a dedicated offline correction-preview operation
- connected local models can turn free-form trainer feedback into a proposed corrected reply without changing live weights
- the deterministic fallback accepts explicit `TARGET:` / quoted replacements and otherwise refuses to invent a semantic rewrite
- correction instructions, proposed target text and correction timestamps persist separately from the original reply
- changing a correction target resets that review item to PENDING and clears prior reviewer / reviewed-time state
- corrected targets pass the simulation-policy review gate before being stored
- the Datasets workspace visibly separates Input, Original Reply and Training Target
- approved JSONL writes the reviewed target as `output` while retaining `original_output`, correction instruction and correction timestamp provenance
- the versioned TrainingData bridge carries correction text and corrected target instead of discarding them
- a newer approved target supersedes older approved targets for the same captured reply in future snapshots
- historical dataset snapshots remain immutable and keep their old example IDs
- the packaged CI fixture visibly exercises the corrected-target Datasets inspector
- Core and Platform regression tests prove correction persistence, forced re-review, persona-scoped export, corrected `output`, preserved original output and fallback exact-target behavior
- no correction action mutates the active model, Foundation Fork, Persona LoRA or deployed runtime automatically

Artifact digests from run #880:

- Windows package: `sha256:a4fca7282a61c7e385a6ead38ff86055f94246e369b4c7da9b165db6cff544b1`
- unsigned-development Setup artifact: `sha256:b75e6f6934ddbbc0f4a7917ee930928d326496cf8ea77a3d9e3a56a43872dde6`
- UI screenshots: `sha256:0890f8f6aa6ef506175fcbb5aafca22620a7926dd09814f54de8b6bb77b2abc4`

This is the validated rollback point for the recovered SARA shell through reviewed correction targets and offline shadow-learning data integrity.

## Checkpoint 19 — Persona LoRA training candidate isolation validated

Validated commit:

`3a9bd0d66a041f98697eff8c483ececcd209554b`

GitHub Actions:

- Workflow: Windows Build
- Run: #887
- Run ID: `36524609266`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- Trainer worker Python syntax validation: PASS
- Persona LoRA candidate-isolation worker self-test: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- packaged UI regression capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Persona LoRA training isolation proven by this checkpoint:

- completing a Persona LoRA training job no longer deactivates the currently active adapter
- the trained GGUF adapter is registered with `active=0`
- training completion therefore cannot silently change the live persona runtime
- the worker explicitly reports the produced artifact as a candidate
- the candidate remains available in Personas & LoRAs history for explicit operator action
- the existing active LoRA remains the resolver result until an investigator deliberately activates another version
- an executable worker self-test seeds an active adapter, registers a newly trained candidate, and proves the active row remains active while the new row remains inactive
- CI architecture guards reject reintroduction of the old worker-side deactivate-and-auto-activate SQL
- Foundation Fork training already remains CANDIDATE on completion and is unchanged by this checkpoint
- manual LoRA/Foundation activation still requires further evaluation-gate hardening; this checkpoint intentionally does not overstate that boundary

Artifact digests from run #887:

- Windows package: `sha256:89c875c67142f1efa84431aba1e24e424dcb18f7eec2ee9fbbb7a0b39a731d4f`
- unsigned-development Setup artifact: `sha256:fc53ccad840d172f38b6c8fd2c58452c24b84ce78f23f6c875c08d949d68d1aa`
- UI screenshots: `sha256:66bd2d1ea758bb9f1e73b7b4eda60f55e38077d296a2bdf62a934867f40dff7b`

This is the validated rollback point for the recovered SARA shell through non-activating Persona LoRA training output.

## Checkpoint 19 — Persona LoRA candidate isolation validated

Validated commit:

`3a9bd0d66a041f98697eff8c483ececcd209554b`

GitHub Actions:

- Workflow: Windows Build
- Run: #887
- Run ID: `36524609266`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- Trainer worker Python syntax validation: PASS
- Persona LoRA candidate-isolation self-test: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- packaged UI regression capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Persona LoRA candidate isolation proven by this checkpoint:

- completing a Persona LoRA training job no longer deactivates the current active adapter
- newly trained LoRA artifacts are registered as inactive candidates
- the active persona runtime remains unchanged when training completes
- explicit Model Lab activation remains required before a new LoRA can become active
- the worker contains an executable SQLite self-test that seeds an existing active adapter, registers a trained candidate, and proves the old adapter remains active while the new adapter remains inactive
- CI runs that self-test on every Windows recovery build
- architecture guards reject reintroduction of trainer-side auto-deactivation or auto-activation
- the protected 1.0.15 startup, splash, installer, application shell and navigation remain unchanged

Artifact digests from run #887:

- Windows package: `sha256:89c875c67142f1efa84431aba1e24e424dcb18f7eec2ee9fbbb7a0b39a731d4f`
- unsigned-development Setup artifact: `sha256:fc53ccad840d172f38b6c8fd2c58452c24b84ce78f23f6c875c08d949d68d1aa`
- UI screenshots: `sha256:66bd2d1ea758bb9f1e73b7b4eda60f55e38077d296a2bdf62a934867f40dff7b`

This is the validated rollback point for the recovered SARA shell through Persona LoRA candidate isolation.

## Checkpoint 20 — Evaluated Foundation and Persona LoRA activation validated

Validated commit:

`6638b78ec3818a02aa68cd06a8da18cdd8b6ffff`

GitHub Actions:

- Workflow: Windows Build
- Run: #906
- Run ID: `36529343424`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- Trainer worker Python syntax validation: PASS
- Persona LoRA candidate-isolation self-test: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- packaged UI regression capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Foundation / Persona LoRA activation proof proven by this checkpoint:

- Foundation Fork records persist the evaluation run that approved them
- Persona LoRA bindings persist the evaluation run that approved them
- existing active 1.0.15 runtime rows receive an explicit legacy-active proof marker during migration rather than being silently invalidated
- newly created Foundation Forks cannot activate without a deployable runtime and passing evaluation proof
- newly bound or trained Persona LoRAs remain inactive candidates
- Persona LoRA approval requires a passing evaluation for the exact binding ID and its Foundation
- Persona LoRA activation requires its Foundation to be the evaluated, approved, currently active Foundation
- rollback refuses unproven Foundation or LoRA versions
- Model Lab can stage a selected Foundation or LoRA in the bundled local runtime for Evaluation/Test without changing its persistent active binding
- staged evaluation records the exact staged Foundation/LoRA IDs and restores the active runtime afterward
- the staged evaluator uses the currently configured SARA local endpoint and the model ID actually discovered from the staged runtime
- Foundation and LoRA workspaces expose Evaluate -> Approve -> Activate progression
- deployment preparation verifies persisted Foundation/LoRA approval proof before packaging a runtime stack
- direct core APIs cannot activate unevaluated or non-deployable model-stack records
- manual LoRA binding now registers a candidate instead of silently replacing the active runtime
- the protected 1.0.15 startup, splash, installer, permanent application shell and navigation remain unchanged

Artifact digests from run #906:

- Windows package: `sha256:b636bbcabc5a479ebff1738de8648b0beb533d85312adf1b644b778f628b4f1f`
- unsigned-development Setup artifact: `sha256:d3fcdd6d7c5cd99ab53878be5b603dc721fa08020fae0cd0c8ff4fd919ef821d`
- UI screenshots: `sha256:dedd46926e73f0992f79b7776fd45ae73b23b7945a14ae05daeff083a6f8720a`

This is the validated rollback point for the recovered SARA shell through staged model-stack evaluation and proof-gated Foundation/Persona-LoRA activation.

## Checkpoint 21 — Automatic sentinel-chat startup validated

Validated commit:

`9c99a9cabe92667ae0f7405d5d8f842958024a48`

GitHub Actions:

- Workflow: Windows Build
- Run: #928
- Run ID: `36532107566`
- Result: **SUCCESS**

Validated gates:

- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- Trainer worker Python syntax validation: PASS
- bundled AI PowerShell startup/configuration syntax validation: PASS
- Persona LoRA candidate-isolation self-test: PASS
- packaged install: PASS
- packaged SARA launch and responsiveness: PASS
- packaged UI regression capture: PASS
- packaged launcher layout: PASS
- SARA Setup.exe build: PASS

Automatic local-model startup proven by this checkpoint:

- the default local model identity is now `sentinel-chat`
- existing local settings are normalized to the stable `sentinel-chat` alias during startup
- SARA starts and loads the already-installed bundled local AI while the splash remains visible
- SARA discovers the local backend and attaches the OpenAI-compatible adapter automatically before normal use
- normal launch no longer requires Browse Models followed by Connect
- active approved Foundation and Persona LoRA files continue to determine the runtime stack behind the stable `sentinel-chat` API alias
- an already-running legacy/persona-specific alias is normalized with one controlled backend restart
- startup performs no model download or repair; missing/incomplete AI components fall back safely and direct the operator to Install / Repair
- local startup is bounded by a shared deadline and a final app-side hard timeout so the old indefinite splash hang cannot return
- failure to load the local backend opens SARA with the built-in fallback instead of leaving the splash stuck
- automatic startup preserves saved temperature and max-token tuning instead of overwriting those values on every launch
- Refresh Models and Reconnect remain available only as recovery/diagnostic controls
- CI rejects reintroduction of the old manual Browse/Connect startup requirement
- CI syntax-checks both bundled AI PowerShell scripts
- the existing approved splash artwork and permanent 1.0.15 application shell remain intact

Model-stack audit provenance also validated in this run:

- Foundation evaluation, approval, activation and rollback have distinct audit action IDs
- Persona LoRA evaluation, approval, activation and rollback have distinct audit action IDs
- lifecycle audit records include relevant Foundation/LoRA/evaluation/persona metadata
- exported Model Lab diagnostics include persisted Foundation and LoRA evaluation-proof IDs

Artifact digests from run #928:

- Windows package: `sha256:e5ad82f58ec2c24a2c7f07a2aad3e2b5f1de8cb5e12797b4590c828a2acd3b38`
- unsigned-development Setup artifact: `sha256:5aedf5b9503919d1a45f45cb03d34b5f575f64291879c21f18585ae963cd8c3f`
- UI screenshots: `sha256:a59293c8c6343c38be89d59143fed8c07df7e8fc49570a294cab7420225a2b2d`

This is the validated rollback point for the recovered SARA shell through automatic `sentinel-chat` startup and audited model-stack lifecycle changes.

## Checkpoint 22 — SARA 1.0.16 versioned release pipeline validated

Validated commit:

`79b71cb99912ece3366a9c287e71bd038780c03a`

GitHub Actions:

- Workflow: Windows Build
- Run: #949
- Run ID: `36540773482`
- Result: **SUCCESS**

Validated release/version behavior:

- repository-root `VERSION` is the canonical shipped version
- current release resolved as **1.0.16**
- CMake reads `VERSION` and exposes it through `SARA_VERSION_STR`
- Windows executable version metadata is generated from the CMake project version
- Inno Setup receives the same version through the version-aware installer helper
- package staging is version-neutral while uploaded artifacts are versioned
- Windows package artifact: `SARA-1.0.16-windows-x64`
- installer artifact: `SARA-Setup-1.0.16-UNSIGNED-DEVELOPMENT`
- release tags are required to match the repository `VERSION`
- CI rejects hard-coded current-package use of the protected 1.0.15 baseline
- `sentinel-chat` deterministic local-model selection regression tests pass
- local startup requires the stable `sentinel-chat` alias and rejects a mismatched local backend
- external/custom model selection can still preserve an explicitly configured available model
- exact 1.0.15 splash/recovery baseline protections remain intact

Validated gates:

- canonical SARA version guard: PASS
- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- bundled AI PowerShell syntax validation: PASS
- Persona LoRA candidate-isolation self-test: PASS
- packaged install: PASS
- packaged SARA launch and recovery UI capture: PASS
- packaged launcher layout: PASS
- version-aware SARA Setup.exe build: PASS

Artifact digests from run #949:

- Windows package: `sha256:0a32dbb2a54a0ccb0b4b29a45dfa711dfff2c8cf9c89e9548e132920dedc3362`
- unsigned-development Setup artifact: `sha256:8bd92d3fff0a616e6d89e28412436f8bb99d0727297e5aa3d5746d15a8a48a77`
- UI screenshots: `sha256:52a01768af2704b16cf8e744eec1fae8f82a94a0bb6ddde0faaa18f139cccf46`

This is the validated SARA **1.0.16** rollback/release point. The protected recovery ancestry remains 1.0.15, while future feature slices advance the application version independently.

## Checkpoint 23 — SARA 1.0.17 automatic local-AI self-recovery validated

Validated commit:

`2f123d048c669320f90b19082e109fd1f0d6e941`

GitHub Actions:

- Workflow: Windows Build
- Run: #960
- Run ID: `36541822456`
- Result: **SUCCESS**

Validated SARA 1.0.17 behavior:

- in-session failures from the bundled loopback `sentinel-chat` model now trigger one bounded automatic recovery attempt
- SARA rewrites/uses the active approved Foundation/LoRA runtime configuration, restarts the bundled backend, reconnects `sentinel-chat`, and retries the failed model operation exactly once
- automatic recovery is shared by Simulation replies, rule voice variation/continuation, proactive follow-ups, investigator suggestions, Persona behavior generation, Trainer behavior previews, and Trainer correction previews
- custom localhost models are not replaced by bundled-model recovery
- external model endpoints never invoke bundled local recovery
- missing bundled prerequisites prevent automatic recovery rather than triggering downloads/repair
- recursive recovery attempts are rejected
- there is no background model polling and no infinite retry loop
- diagnostics expose the number of successful automatic `sentinel-chat` recoveries in the current session
- CI rejects direct GUI `model_->Generate...` calls that bypass the recovery wrapper
- regression tests cover recovery eligibility for loopback, localhost, custom-local, external, missing-prerequisite, and re-entrant cases

Validated gates:

- canonical SARA version guard: PASS
- exact trusted 1.0.15 recovery-baseline guard: PASS
- SARA product-architecture guard: PASS
- exact approved splash verification: PASS
- Windows MSVC x64 Release build: PASS
- core/platform/CLI tests: PASS
- bundled AI PowerShell syntax validation: PASS
- Persona LoRA candidate-isolation self-test: PASS
- packaged install: PASS
- packaged SARA launch and recovery UI capture: PASS
- packaged launcher layout: PASS
- version-aware SARA Setup.exe build: PASS

Artifact digests from run #960:

- Windows package: `sha256:b19437606455d4c17ac6b8876ffa37c849aa9b41714d5da59f88b4e6ab439508`
- unsigned-development Setup artifact: `sha256:d13dafc5115327a9aa91adfc39c59ca971a3f3318d028462ed3d14285207ce5a`
- UI screenshots: `sha256:f0aa3f8d33aee9b0cef9d860330b77683f2e197b2829bd5cf04c1ae231a34d08`

This is the validated SARA **1.0.17** rollback/release point.

