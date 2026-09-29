# SARA Changelog

## 1.0.16 — Current recovery feature release

Built on the protected 1.0.15 recovery baseline.

### Local AI startup
- Automatically starts and connects the installed local runtime as `sentinel-chat` during normal SARA loading.
- Removes Browse Models -> Connect as a required startup workflow.
- Keeps Refresh Models / Reconnect as diagnostic recovery controls.
- Uses a bounded startup deadline and safely falls back instead of hanging behind the splash.
- Preserves saved model tuning during automatic startup.
- Centralizes and regression-tests deterministic `sentinel-chat` model selection.

### Model Lab and training
- Enforces exact passing evaluation proof for model approval and deployment.
- Persists the exact evaluation/Foundation/adapter proof used to approve a model.
- Makes reviewed Trainer corrections separate supervised targets rather than relabeling the original reply.
- Forces corrected targets back to PENDING when edited.
- Keeps completed Persona LoRA training outputs inactive until explicit review/evaluation/activation.
- Adds staged Foundation and LoRA evaluation before activation.
- Requires the evaluated Foundation to be active before its LoRA can activate.
- Adds model-stack evaluation/approval/activation/rollback audit provenance.

### Release engineering
- Introduces repository-root `VERSION` as the canonical shipped application version.
- CMake, Windows executable metadata, package artifacts, and installer builds now derive their version from that release value.
- Keeps 1.0.15 references only where they identify the immutable recovery baseline.
- Adds CI guards for version consistency and a version-aware installer build helper.

## 1.0.15 — Protected recovery baseline

The trusted rollback baseline used to reconstruct and selectively improve SARA without wholesale post-baseline merges.
