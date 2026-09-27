# SARA Model Lab UI — 1.0.16 Hybrid Design

Status: APPROVED
Date: 2026-09-27
Branch: `sara-1.0.16-trainer-redesign`

## Design decision

The production Model Lab will combine the strongest interaction patterns from the explored concepts rather than copying one mockup.

### 1. Overview = Executive Dashboard
Purpose: answer “what is happening right now?”

Contains:
- current foundation / persona / LoRA / training mode context
- model registry health
- active/recent training jobs
- dataset/training-example counts
- last/best evaluation
- deployment status
- quick actions

### 2. Train = Conversational Trainer
Purpose: make teaching SARA feel like a conversation, not a form.

Contains:
- large conversation transcript
- correction/capture workflow
- review-queue entry action
- persona + LoRA context
- smiley emoji icon button
- paperclip attachment icon button
- Send action
- session/review/job information in a secondary column

### 3. Personas & LoRAs = Management workspace
Purpose: manage reusable personalities and their adapters.

Contains:
- persona table/list
- assigned foundation
- assigned LoRA version
- status (active/training/staging/inactive)
- detail inspector
- adapter history
- train/test/clone/export actions

### 4. Jobs / Evaluation / Deployment = Pipeline command center
Purpose: make the model lifecycle obvious.

Lifecycle:
`Captured -> Review -> Approved -> Dataset -> Training Job -> Candidate -> Evaluation -> Deploy`

Contains:
- job progress
- evaluation scores
- candidate comparisons
- deployment target/status
- version history
- rollback

## Shared visual system

- dark navy / charcoal base
- cyan and electric blue as primary accents
- restrained purple as secondary accent
- rounded cards
- consistent 8/12/16/24px-style spacing rhythm
- clear hierarchy; fewer competing boxes
- compact badges for state
- subtle glow only on active/primary states
- vector-drawn UI icons where possible
- consistent icon button size and hit targets
- no overlapping controls at supported sizes

## Shared Model Lab navigation

`Overview | Train | Datasets | Personas & LoRAs | Foundation Forks | Jobs | Evaluation | Deployment`

## Composer standard

The chat/trainer composer must use:
- smiley-face icon for emoji
- paperclip icon for attachment
- icon-only buttons with tooltip/hover treatment
- consistent sizing/alignment
- Send as the primary action

## Implementation sequence

1. Shared Model Lab shell / overview
2. Composer icon controls and spacing
3. Conversational Trainer workspace
4. Personas & LoRAs workspace
5. Foundation Forks
6. Jobs
7. Evaluation
8. Deployment
9. responsive-size and visual regression polish
