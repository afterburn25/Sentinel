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
