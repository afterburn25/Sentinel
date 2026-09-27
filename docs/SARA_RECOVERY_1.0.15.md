# SARA Recovery Lock — 1.0.15 Known-Good Baseline

Date: 2026-09-27

## Recovery rule

SARA 1.0.15 commit `78a2aa3d30eb8cd647d1f4213f5d4e3961a6105d` is the only trusted application baseline for recovery.

Do not port the post-1.0.15 GUI, installer, startup, Model Lab, channel-core, evaluation, deployment, or visual-restoration branches wholesale into this line.

The immutable rescue branch is:

`sara-rescue-1.0.15-known-good`

The active recovery work branch is:

`sara-recovery-from-1.0.15`

## Known-good validation

Original Windows Build run #444 / run ID `36314714530` passed from the exact baseline commit.

Validated:
- exact approved SARA splash PNG
- CMake configure
- MSVC x64 Release build
- tests
- SQLite dependency verification
- package/install layout
- SHA-256 manifest generation
- Inno Setup compilation
- Windows package upload
- SARA Setup upload

Original artifacts:
- `SARA-1.0.15-windows-x64`
- `SARA-Setup-1.0.15`

## Installer behavior that must not regress

The 1.0.15 installer is authoritative.

It must retain:
- Qwen3.5-9B-Q4_K_M.gguf model identity.
- Expected model SHA-256 `03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8`.
- Full SHA-256 verification of an existing model when no valid verification marker exists.
- Verification-marker optimization only after successful model verification.
- Downloaded model SHA-256 verification before installation.
- llama.cpp runtime-version validation.
- GPU detection and CUDA/CPU runtime selection.
- CPU fallback runtime.
- local AI configuration after runtime/model installation.
- upgrade detection and in-place upgrade behavior.
- preservation of existing data and valid model/runtime assets.

A quick installer is acceptable only when the existing model verification marker and runtime marker are valid. Removing the verification path is not acceptable.

## Startup / splash behavior that must not regress

The exact approved splash asset is authoritative:
- `resources/assets/SARA-Splash.png`
- native size 1672x941
- SHA-256 `dc298f498c76975af80aa14fd1e9125ae64746c54181c340f138869a24548cb4`

Required behavior:
- no replacement artwork
- no blurry JPG
- no flashing/blinking
- only the progress bar already drawn in the artwork is animated
- starts empty and fills forward
- seven-second minimum display
- splash remains until main application initialization completes
- direct transition to the initialized main window
- startup failure must never leave an endless splash with no diagnostic path

## Approved hybrid Model Lab visual specification

The hybrid Model Lab is a controlled rebuild on top of 1.0.15, not a replacement application shell.

Visual references from the approved concept set:
- `SARA Model Lab Interface Concepts.png`
- `SARA Model Lab Training Dashboard.png`
- `SARA Model Lab Futuristic Dashboard.png`
- `SARA Model Lab Training Dashboard(1).png`
- `Neon AI Model Lab Dashboard.png`

Required shared visual language:
- dark navy / near-black futuristic shell
- neon cyan / teal / electric-blue primary accents
- restrained purple secondary accents
- rounded premium cards and panels
- controlled glow, not excessive bloom
- strong spacing and alignment
- cleaner typography
- branded SARA left navigation rail
- premium desktop application appearance, not a flat utility form

Required Model Lab navigation:
- Dashboard / Overview
- Train
- Datasets
- Personas & LoRAs
- Foundation Forks
- Jobs
- Evaluation
- Deployment
- Settings
- Resources
- Logs

Required Train workspace:
- top search/status/context area
- Foundation selector
- Persona selector
- assigned LoRA selector/status
- Training Mode selector
- large central Conversational Trainer
- right Training Context / Training Session inspector
- active persona
- training mode
- optional dataset
- review-queue state
- recent examples/captures
- Approve / Edit / Regenerate / Discard actions where applicable
- bottom message input and Send
- pipeline visibility: Capture -> Review -> Dataset -> Train -> Evaluate -> Deploy
- hero/status strip and supporting activity/resource cards where they fit without crowding the trainer

## Recovery sequencing

1. Keep 1.0.15 installer and startup logic unchanged.
2. Reproduce the known-good 1.0.15 package.
3. Add automated regression guards for installer verification and splash startup behavior.
4. Rebuild the approved hybrid Model Lab visually in small isolated commits.
5. Capture an actual packaged-app screenshot after each meaningful UI step.
6. Compare each screenshot to the approved concept set before adding more UI.
7. Only after the UI is approved again, selectively port later backend features one at a time.
8. Every port must pass startup, installer, model verification, tests, packaged UI screenshot, and installer validation before the next feature is added.

## Explicitly rejected recovery strategy

Do not use the post-1.0.15 visual-restoration branches as the new base.
Do not merge the old 1.0.16+ Model Lab wholesale.
Do not recreate the hybrid UI from memory or a generic dashboard interpretation.
Do not replace the 1.0.15 installer with the later simplified installer.
