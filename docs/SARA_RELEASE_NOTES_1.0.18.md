# SARA 1.0.18 Release Notes

Release date: 2026-09-27

## SARA 1.0.18 — Evaluation Suite

SARA 1.0.18 extends the merged 1.0.17 Dataset & Adapter Management baseline with a persistent, repeatable multidimensional model-evaluation framework inside the existing hybrid Model Lab UI.

## Evaluation dimensions

Every evaluation run can score:

- **Persona Consistency**
- **Policy Compliance**
- **Style Consistency**
- **Long-Context / Memory Recall**
- **Trigger Regression**
- **Response Diversity**

The overall score is derived from these dimension results.

## Named evaluation test cases

Evaluation is no longer only a single generic prompt.

1.0.18 adds named, extensible test cases with persisted case-level results.

Each case can preserve:
- case ID
- human-readable name
- evaluation dimension
- generated response
- score
- pass/review state
- detailed result text
- expected facts
- forbidden phrases
- minimum history depth

Current built-in cases include:
- persona identity
- persona location
- persona occupation
- neutral policy response
- multiple style prompts
- long-context code-word recall

Configured persona facts are inserted into the relevant factual test expectations at runtime.

## Long-context evaluation

The memory-recall test now exercises a conversation containing at least 24 history turns before asking for the remembered fact.

This is designed to catch regressions where short-context chat still works but longer conversation memory degrades.

## Trigger regression

Configured trigger rules are checked for:
- enabled-rule coverage
- correct rule identity / priority resolution
- terminal vs continue behavior
- usable response output when a response pool is configured

Trigger regression contributes its own evaluation dimension.

## Response diversity

The suite normalizes multiple generated responses and measures how many remain meaningfully distinct.

This helps detect a candidate that collapses into repetitive/canned phrasing.

## Style evaluation

The style dimension evaluates response behavior against configured persona settings including:
- grammar quality
- emoji tendency
- intelligence/language level
- response length tendencies

Named style cases are combined with the aggregate style score.

## Factual matching

Named factual cases support natural paraphrase.

Exact configured-fact matches receive full credit. Multi-word facts can receive partial credit when a response preserves a meaningful portion of the expected fact, reducing false regressions caused by normal conversational paraphrase.

## Persistent evaluation runs

Evaluation runs are stored persistently with:
- run ID
- UTC creation time
- candidate ID/name
- foundation ID/name
- persona-adapter ID/name
- overall score
- previous overall score
- regression delta
- dimension results
- named case results
- warnings

The Evaluation page can reopen prior runs after restart.

## Regression detection

1.0.18 compares each candidate with its previous run.

Regression detection includes:
- overall score delta
- per-dimension delta
- warnings for meaningful dimension drops

This prevents a major regression in one area from being hidden by improvements elsewhere.

## Hybrid Evaluation command center

The approved SARA hybrid UI remains the controlling visual system.

The Evaluation workspace now includes:
- six dimension score cards
- pass/review status
- run timestamp
- overall score
- previous-run delta
- named-case pass count
- persistent recent-run history
- candidate comparison selection
- key dimension deltas
- warnings/regression display

## Reports

### Single-run report

Operators can export a complete evaluation-run report containing:
- runtime stack
- overall score
- previous score/delta
- all dimensions
- warnings
- named test cases
- case responses
- case details

### Candidate comparison report

Operators can select another evaluated candidate/run and export a side-by-side report with:
- overall scores
- overall delta
- all six dimension scores
- per-dimension deltas
- foundation/adapter runtime identity

## Deployment isolation

Evaluation remains read/evaluate-only.

Running the suite:
- does **not** approve a candidate
- does **not** activate a candidate
- does **not** deploy a candidate
- does **not** change production weights

Approval and deployment remain explicit operator actions.

## Version / packaging

SARA 1.0.18 has independent version identity across:
- CMake project version
- Windows executable metadata
- visible GUI version
- diagnostics
- inference runtime user agent
- updater user agent
- update manifest
- portable CI artifact
- Inno Setup installer

Portable package:
`SARA-1.0.18-windows-x64`

Installer:
`SARA-Setup-1.0.18.exe`

## Validated build

GitHub Actions run **#524** / run ID `36323329898` validated implementation commit:

`d4199ffbda9803db3ca4d2fe246e98d59540fe44`

Passed:
- CMake configure
- MSVC x64 Release build
- CTest
- SQLite DLL dependency check
- install/package
- package SHA-256 manifest generation
- package SHA-256 manifest verification
- launcher-layout verification
- Inno Setup installer compilation
- installer SHA-256 generation
- portable artifact upload
- installer artifact upload

Signing steps were skipped because Microsoft Artifact Signing secrets are not configured. These are unsigned development artifacts.

### Portable artifact

- Artifact ID: `10933201804`
- Artifact ZIP SHA-256: `34ab53efdc3651d02ef2bdae893f6eb0954071f3162ff48c4652acd2c0cd2c00`
- `SARA.exe` SHA-256: `c4ea53fa7fbc90692d6b7413e3e975d1a8416828b53e2d3340115717ccee1e1b`
- 22 manifest entries independently verified

### Installer artifact

- Artifact ID: `10933401512`
- Artifact ZIP SHA-256: `ad64836bd7fc9a94e42ca379b53b5a382d5dc043070593d4b4338efd257c9d0f`
- Installer EXE SHA-256: `67caa997fda660db4a9ed587da1b71b7819145a7aa8363a7bb4df93fb9b23a79`
- Included installer checksum file independently verified

## Canonical project memory

Future sessions should start with:
- `docs/SARA_PROJECT_STATE.md`
- `docs/SARA_ROADMAP.md`
- `docs/SARA_RELEASE_NOTES_1.0.18.md`
- `docs/DEVELOPMENT_CONTINUITY.md`
