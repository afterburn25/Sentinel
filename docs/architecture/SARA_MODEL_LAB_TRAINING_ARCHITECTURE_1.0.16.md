# SARA Model Lab / Trainer — Training Architecture 1.0.16

Status: APPROVED
Date: 2026-09-27
Branch: `sara-1.0.16-trainer-redesign`

## 1. Training Mode selector

The integrated Model Lab / Trainer must provide these selectable modes:

- **Behavior Tuning**
- **Dataset Training**
- **Persona LoRA Training**
- **Foundation Fork Training**
- **Evaluation / Test Mode**

The selected mode must be visible in the shared Model Lab context area and must determine which training controls, dataset sources, validation steps, and deployment actions are available.

## 2. SARA Foundation Forks

Base/downloaded foundation models are immutable reference assets.

Requirements:

- Never overwrite the original downloaded/base model.
- Create versioned **SARA Foundation** descendants from a base model.
- Example naming:
  - `SARA Foundation 1.0`
  - `SARA Foundation 1.1`
  - `SARA Foundation Experimental`
- Track each fork's parent model and parent version.
- Support side-by-side comparison.
- Support rollback to a prior approved foundation fork.
- Preserve version metadata for reproducibility.
- A foundation fork may later receive persona-specific adapters, but persona adapters do not replace the fork itself.

## 3. Persona LoRAs

Each persona can own one or more LoRA / QLoRA adapters.

Examples:

- `Samantha.lora`
- `Nikki.lora`

Requirements:

- Multiple versions per persona.
- Maintain adapter status and history.
- Selecting a persona automatically resolves its assigned adapter.
- The adapter augments the selected SARA foundation model instead of replacing it.
- Support active, staging, training, archived, and rollback states.
- Preserve the exact foundation/version the adapter was trained against.
- Runtime must reject or warn about incompatible adapter/foundation combinations.

## 4. Conversational Trainer

The operator can talk directly to the model and correct its behavior using natural language.

Examples:

- “Don’t answer like that.”
- “Samantha would say this instead.”
- “Use shorter messages.”
- “She uses more slang.”
- “Don’t ask that question again.”
- “When someone says X, respond more like Y.”

Corrections may affect the active training session immediately as a runtime behavior override, but must not silently modify production weights.

Corrections can optionally be promoted into reviewed training examples for future LoRA / QLoRA training.

Each captured correction should preserve:

- original user/context message
- original model response
- operator correction/instruction
- corrected target response when provided
- persona
- foundation model/version
- assigned adapter/version
- training mode
- timestamp
- source conversation/session
- review status
- reviewer/approver metadata
- dataset destination
- provenance/version history

## 5. Training data review lifecycle

No conversational correction or captured example may silently alter production weights.

Canonical lifecycle:

`Captured -> Review -> Approved -> Dataset -> Training Job -> Candidate -> Evaluation -> Deploy`

Rules:

- Captured examples remain non-production until reviewed.
- Reviewers may approve, reject, edit, relabel, or defer.
- Approved examples are promoted into versioned datasets.
- Training jobs consume explicit dataset snapshots.
- Training produces versioned candidates.
- Candidates require evaluation before deployment.
- Deployment must preserve rollback information.
- Every transition should be audit logged.

## 6. Automatic runtime model resolution

When a persona is selected, SARA automatically resolves the complete runtime stack.

Example for Samantha:

`SARA Foundation -> Samantha LoRA -> Samantha Behavior Profile -> Conversation Memory -> Current Context`

Resolution order:

1. Selected SARA Foundation and exact version
2. Persona's assigned LoRA / QLoRA and exact version
3. Persona behavior profile
4. Persisted conversation memory
5. Current session/context
6. Deterministic rules/triggers before generative response

Selecting a persona should load everything belonging to that persona automatically.

## 7. Triggers / rules remain separate

Deterministic triggers are not model weights and must stay separate from LoRA/foundation training.

Execution requirements:

1. Evaluate deterministic triggers/rules first.
2. If a rule matches and is configured as terminal, do not fall through to generic generation.
3. If a rule is configured to continue, pass its result/context explicitly into generation.
4. Show rule matches in diagnostics/logging.
5. A newly created rule must be available immediately after save/reload.
6. UI must show actual rules and their priority/order, not only a rule count.

This specifically addresses the current failure mode where a matched/newly added trigger could be ignored and SARA would continue with a generic model response.

## 8. Response variation

Trigger responses and learned persona behavior must not collapse into one canned sentence.

SARA should produce multiple semantically equivalent responses shaped by:

- persona age
- intelligence/language level
- slang preference
- grammar quality
- typo tendency
- emoji tendency
- personality
- mood
- writing style
- established conversation history
- prior questions/answers to avoid repetition

Variation rules:

- Preserve the underlying trigger or learned intent.
- Avoid asking the same personal question repeatedly when the answer is already known.
- Keep variation persona-consistent.
- Permit configurable randomness/temperature limits.
- Support reusable phrasing pools plus generative variation.
- Log which rule/behavior source influenced the generated response.

## 9. Separation of training and live inference

Live inference and active investigations must remain isolated from training operations.

- Training jobs run in a separate service/process/machine as configured.
- Production model/adapter versions remain pinned until explicitly deployed.
- Runtime corrections in training mode must not mutate production weights.
- Candidate models/adapters remain non-production until approved and deployed.

## 10. UI implications

The Model Lab / Trainer UI should expose:

- Training Mode selector
- Foundation selector and fork/version
- Persona selector
- Assigned LoRA/version
- current runtime-resolution summary
- correction capture controls
- review queue
- dataset promotion controls
- training job status
- candidate evaluation
- deployment/rollback status
- trigger/rule diagnostics
- response-variation settings where appropriate


## 11. Inference adapter loading contract

Persona selection resolves the exact foundation and active persona adapter before generation.

For OpenAI-compatible inference, SARA sends optional request headers:

- `X-SARA-Foundation-Id`
- `X-SARA-Foundation`
- `X-SARA-Adapter-Id`
- `X-SARA-Adapter`

A SARA-aware local inference service should use these values to activate/load the requested foundation and LoRA before fulfilling the chat completion. Generic OpenAI-compatible servers may safely ignore these headers.

The resolved foundation/adapter identity is also included in model context for consistency. This keeps SARA independent of a single runtime vendor while providing a concrete automatic-LoRA-loading contract.
