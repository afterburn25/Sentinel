# SARA Project State

Last updated: 2026-09-27

## Canonical identity

**SARA — Synthetic Adaptive Response Agent**

The repository was originally named Sentinel and still uses Sentinel in many source paths, namespaces, build targets, and historical assets. SARA is the canonical product name going forward unless explicitly changed.

## Repository

- GitHub: `afterburn25/Sentinel`
- Default branch: `main`
- Active development branch: `sara-1.0.16-trainer-redesign`
- Desktop implementation: native Windows C++20
- Build system: CMake + vcpkg
- Main GUI implementation currently lives in `src/Gui/Main.cpp`
- Windows builds are validated with GitHub Actions.

## Current release baseline

The latest distributed desktop baseline supplied in chat is **SARA 1.0.15**.

The GitHub source still contains older internal version strings such as Sentinel 1.0.7. Version/branding normalization is therefore part of the 1.0.16 work.

## Current development target

**SARA 1.0.16 — Trainer / Model Lab Redesign and Training Architecture**

### Primary goals

1. Redesign Model Lab / Trainer so it is organized, aligned, responsive, and professional.
2. Eliminate overlapping controls, clipped labels, inconsistent spacing, and layout collisions.
3. Create dedicated workspaces for:
   - Train
   - Datasets
   - Personas & LoRAs
   - Foundation Forks
   - Jobs
   - Evaluation
   - Deployment
4. Add selectable training modes:
   - Behavior Tuning
   - Dataset Training
   - Persona LoRA Training
   - Foundation Fork Training
   - Evaluation / Test Mode
5. Support versioned SARA foundation-model forks while preserving the original base model.
6. Support persona-specific LoRA / QLoRA adapters with multiple versions per persona.
7. Automatically load the assigned persona adapter when a persona is selected.
8. Add a conversational trainer where an operator can correct model behavior in natural language.
9. Convert approved conversational corrections into reviewable training records.
10. Preserve a review lifecycle:
    `Captured -> Review -> Approved -> Dataset -> Training Job -> Candidate -> Evaluation -> Deploy`
11. Keep deterministic triggers/rules separate from generative model behavior and execute rules before normal generation.
12. Improve response variation so learned behavior can be expressed differently according to the persona instead of repeating canned responses.
13. Preserve persona behavior dimensions including age-appropriate writing style, intelligence level, slang, grammar quality, typo tendency, emoji tendency, personality, mood, and prior conversation context.
14. Keep live investigation/inference isolated from training operations.

## Runtime resolution target

When a persona such as Samantha is selected, runtime resolution should follow:

`SARA Foundation -> Persona LoRA -> Persona Behavior Profile -> Conversation Memory -> Current Context`

The persona's assigned adapter should load automatically.

## Simulation / chat composer UI requirements

The composer is being polished as part of 1.0.16.

- Emoji control must use a **smiley-face icon**.
- Attachment control must use a **paperclip icon**.
- Both should be icon-only toolbar buttons with consistent size, padding, hover state, borders, alignment, and tooltips.
- Prefer vector-drawn icons so Windows emoji/font differences do not affect appearance.
- Composer controls must visually match the rest of the SARA desktop UI.

## Current GUI source landmarks

At the time this file was created:

- Main page enumeration and native controls: `src/Gui/Main.cpp`
- Simulation / chat UI: approximately lines 1200-1350
- Persona & Policy UI: approximately lines 1878+
- Model Lab UI: approximately lines 2045+
- Main click dispatch: approximately lines 440+
- Icon renderer: approximately lines 780+
- Native control layout: approximately lines 1310+

These line numbers are approximate and will move as the source is refactored.

## UI direction

The Trainer redesign should look like a premium, modern desktop application rather than a utility form.

Target characteristics:

- Clear navigation hierarchy
- Consistent spacing system
- Strong alignment
- Card-based grouping
- Cleaner typography
- Subtle depth and borders
- Polished hover/selected states
- Better visual separation between configuration, training, jobs, and deployment
- No overlapping controls at supported window sizes
- No large unstructured wall of controls
- Consistent form-row heights and label widths
- Dedicated workspace per major function

## Project continuity rule

GitHub is the canonical project memory for SARA.

For every meaningful future discussion or implementation change, update the repository with:
- the decision or requirement,
- the code/configuration change if implemented,
- the roadmap/status if not yet implemented,
- release notes when behavior changes,
- migration/compatibility notes when relevant.

A future development session should be able to reconstruct current state from the repository without depending on chat history.
