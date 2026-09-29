param()

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$mainPath = Join-Path $repo "src\Gui\Main.cpp"
$memoryPath = Join-Path $repo "src\Simulation\ConversationMemory.cpp"
$fallbackModelPath = Join-Path $repo "src\Simulation\RuleBasedTestModel.cpp"
$modelRegistryPath = Join-Path $repo "src\Simulation\ModelRegistry.cpp"
$deploymentRegistryPath = Join-Path $repo "src\Simulation\DeploymentRegistry.cpp"
$trainingReviewPath = Join-Path $repo "src\Simulation\TrainingReviewStore.cpp"
$modelAdapterPath = Join-Path $repo "include\Sentinel\Simulation\IModelAdapter.hpp"
$openAiModelPath = Join-Path $repo "src\Simulation\OpenAICompatibleModel.cpp"
$correctionMigrationPath = Join-Path $repo "migrations\0038_training_review_correction_targets.sql"
$contractPath = Join-Path $repo "docs\SARA_PRODUCT_ARCHITECTURE.md"

if (-not (Test-Path $contractPath)) {
    throw "SARA product architecture contract is missing: $contractPath"
}
if (-not (Test-Path $mainPath)) {
    throw "SARA GUI source is missing: $mainPath"
}
if (-not (Test-Path $memoryPath)) {
    throw "SARA conversation-memory source is missing: $memoryPath"
}
if (-not (Test-Path $fallbackModelPath)) {
    throw "SARA fallback-model source is missing: $fallbackModelPath"
}
if (-not (Test-Path $modelRegistryPath)) {
    throw "SARA model-registry source is missing: $modelRegistryPath"
}
if (-not (Test-Path $deploymentRegistryPath)) {
    throw "SARA deployment-registry source is missing: $deploymentRegistryPath"
}
if (-not (Test-Path $trainingReviewPath)) {
    throw "SARA training-review source is missing: $trainingReviewPath"
}
if (-not (Test-Path $modelAdapterPath)) {
    throw "SARA model-adapter contract is missing: $modelAdapterPath"
}
if (-not (Test-Path $openAiModelPath)) {
    throw "SARA OpenAI-compatible model source is missing: $openAiModelPath"
}
if (-not (Test-Path $correctionMigrationPath)) {
    throw "SARA correction-target migration is missing: $correctionMigrationPath"
}

$src = Get-Content $mainPath -Raw
$memorySrc = Get-Content $memoryPath -Raw
$fallbackModelSrc = Get-Content $fallbackModelPath -Raw
$modelRegistrySrc = Get-Content $modelRegistryPath -Raw
$deploymentRegistrySrc = Get-Content $deploymentRegistryPath -Raw
$trainingReviewSrc = Get-Content $trainingReviewPath -Raw
$modelAdapterSrc = Get-Content $modelAdapterPath -Raw
$openAiModelSrc = Get-Content $openAiModelPath -Raw
$correctionMigrationSrc = Get-Content $correctionMigrationPath -Raw

$begin = $src.IndexOf("// SARA_PRODUCT_CONTRACT_MAIN_NAV_BEGIN")
$end = $src.IndexOf("// SARA_PRODUCT_CONTRACT_MAIN_NAV_END")
if ($begin -lt 0 -or $end -le $begin) {
    throw "Permanent SARA main-navigation contract markers are missing."
}
$sidebarNav = $src.Substring($begin, $end - $begin)

$labelStart = $src.IndexOf("const wchar_t* MainNavLabel(int index) const")
$labelEnd = $src.IndexOf("IconKind MainNavIcon(int index) const", $labelStart)
if ($labelStart -lt 0 -or $labelEnd -le $labelStart) {
    throw "Permanent SARA navigation label definition is missing."
}
$navLabels = $src.Substring($labelStart, $labelEnd - $labelStart)

$required = @(
    "Dashboard",
    "Cases",
    "Subjects & Identity",
    "Simulation Chat",
    "Personas",
    "Channels & Messaging",
    "Supervisor & Approvals",
    "Evidence",
    "Audit & Compliance",
    "Model Lab",
    "Agency Server",
    "Settings"
)

$last = -1
foreach ($label in $required) {
    $token = 'L"' + $label + '"'
    $pos = $navLabels.IndexOf($token)
    if ($pos -lt 0) {
        throw "Required permanent SARA navigation item is missing: $label"
    }
    if ($pos -le $last) {
        throw "Permanent SARA navigation order changed near: $label"
    }
    $last = $pos
}

$forbiddenTopLevel = @(
    'L"Trainer"',
    'L"Verification"',
    'L"Datasets"',
    'L"Foundation Forks"',
    'L"Jobs"',
    'L"Evaluation"',
    'L"Deployment"',
    'L"Identity Research"'
)
foreach ($token in $forbiddenTopLevel) {
    if ($navLabels.Contains($token)) {
        throw "Sub-workflow was promoted into permanent SARA navigation: $token"
    }
}

if ($sidebarNav.Contains("page_==Page::ModelLab") -or
    $sidebarNav.Contains("MODEL LAB Recovery") -or
    $sidebarNav.Contains("Back to SARA")) {
    throw "Model Lab-specific sidebar takeover detected in the permanent navigation block."
}

$requiredDispatch = @(
    "case Page::Dashboard: DrawDashboard",
    "case Page::Cases: DrawCases",
    "case Page::Subjects: DrawSubjects",
    "case Page::Simulation: DrawSimulation",
    "case Page::Persona: DrawPersona",
    "case Page::Messaging: DrawMessaging",
    "case Page::Supervisor: DrawSupervisor",
    "case Page::Evidence: DrawEvidence",
    "case Page::Audit: DrawAudit",
    "case Page::ModelLab: DrawModelLab",
    "case Page::Agency: DrawAgency",
    "case Page::Settings: DrawSettings"
)
foreach ($token in $requiredDispatch) {
    if (-not $src.Contains($token)) {
        throw "Protected SARA module is no longer dispatched: $token"
    }
}

$requiredWorkflowNames = @(
    'PageTitle(L"Simulation Chat"',
    'PageTitle(L"Personas"',
    'PageTitle(L"Channels & Messaging"',
    'PageTitle(L"Supervisor & Approvals"',
    'PageTitle(L"Audit & Compliance"'
)
foreach ($token in $requiredWorkflowNames) {
    if (-not $src.Contains($token)) {
        throw "Protected product workflow/title is missing: $token"
    }
}

$typingWorkflowTokens = @(
    'simTypingStartedTick_',
    'simTypingDurationMs_',
    'SetTimer(hwnd_,kSimReplyTimer,40',
    'live+=L"|"'
)
foreach ($token in $typingWorkflowTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected progressive Simulation typing workflow is missing: $token"
    }
}

$learningReviewTokens = @(
    'learning_notes_toggle',
    'learning_note_delete:',
    'PersonaLearnedNotes(',
    'Learned Continuity'
)
foreach ($token in $learningReviewTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected Learning Mode review workflow is missing: $token"
    }
}

$learningContinuityMainTokens = @(
    'RecallLearnedPersonaNotes(',
    'learnedNotes'
)
foreach ($token in $learningContinuityMainTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected Learning Mode continuity wiring is missing: $token"
    }
}
$learningContinuityMemoryTokens = @(
    'ConversationMemoryStore::RecallLearnedPersonaNotes',
    'Benign continuity notes previously established'
)
foreach ($token in $learningContinuityMemoryTokens) {
    if (-not $memorySrc.Contains($token)) {
        throw "Protected Learning Mode continuity memory is missing: $token"
    }
}

$memoryWorkflowMainTokens = @(
    'RecallQuestionHistory(',
    'questionHistory'
)
foreach ($token in $memoryWorkflowMainTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected conversation-memory anti-repeat wiring is missing: $token"
    }
}
$memoryWorkflowStoreTokens = @(
    'ConversationMemoryStore::RecallQuestionHistory',
    'Questions this same persona already asked'
)
foreach ($token in $memoryWorkflowStoreTokens) {
    if (-not $memorySrc.Contains($token)) {
        throw "Protected conversation-memory anti-repeat store is missing: $token"
    }
}
if (-not $fallbackModelSrc.Contains('GenerateSyntheticInitiative')) {
    throw "Protected fallback initiative generator is missing."
}

$identityResearchTokens = @(
    'Page::IdentityResearch',
    'L"Subjects & Identity / Research"',
    'identity_research_open',
    'research_promote',
    'IdentityResearchQueued',
    'PromoteResearchToLead(',
    'ListResearchProviders(false)',
    'SelectedIdentityResearchProvider()',
    'researchProviderCombo_',
    'research_provider_manager',
    'research_provider_save',
    'research_preserve',
    'research_export_request',
    'research_import_result',
    'research_open_portal',
    'IsSafeResearchPortalUrl(',
    'imported_result_evidence=',
    'IdentityResearchRequestPackage',
    'LoadResearchResultPackage(',
    'BuildResearchReport(',
    'IdentityResearchPreserved',
    'researchProviderCredentialEdit_',
    'L"Credential alias / vault reference"',
    'L"NO AUTOMATIC IDENTITY CONCLUSIONS"'
)
foreach ($token in $identityResearchTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected Identity Research workflow is missing: $token"
    }
}

$personaWorkflowTokens = @(
    'L"Rules & Learning"',
    'L"persona_tab_rules"',
    'PersonaResponseRules(50)',
    'responseRuleMatches.Append(',
    'CountForRule(',
    'rule_terminal_toggle',
    'SelectResponseRuleVariant(',
    'rule_toggle:',
    'rule_delete:'
)
foreach ($token in $personaWorkflowTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected Persona Rules & Learning workflow is missing: $token"
    }
}

$evaluationGateTokens = @(
    'EvaluationPassedApprovalGate(',
    'EvaluationMatchesCurrentRuntime(',
    'Run Evaluation / Test before approving this model',
    'reevaluate before preparing deployment'
)
foreach ($token in $evaluationGateTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected evaluation/deployment gate is missing: $token"
    }
}

$loraWorkflowTokens = @(
    'persona_lora_activate',
    'persona_lora_rollback',
    'persona_lora_compare',
    'PreviousPersonaLora(',
    'RollbackPersonaLora('
)
foreach ($token in $loraWorkflowTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected Persona LoRA lifecycle is missing: $token"
    }
}

$foundationWorkflowTokens = @(
    'foundation_activate_selected',
    'foundation_rollback',
    'foundation_compare',
    'RollbackFoundation()',
    'runtimeGgufPath.empty()'
)
foreach ($token in $foundationWorkflowTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected Foundation Fork lifecycle is missing: $token"
    }
}

$trainerWorkflowTokens = @(
    'L"Trainer Conversation"',
    'L"Conversation History"',
    'trainer_apply_instruction',
    'trainer_apply_preview',
    'trainer_new_session',
    'trainer_setup_env',
    'ExportApprovedJsonlForPersona',
    'L"Behavior Tuning"',
    'L"Evaluation / Test"',
    'EnsureDialogueSession('
)
foreach ($token in $trainerWorkflowTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected conversational Trainer workflow is missing: $token"
    }
}

$correctionReviewMainTokens = @(
    'GenerateCorrectionPreview(',
    'SetCorrectionTarget(',
    'TrainingTargetText(item)',
    'PENDING human review in Model Lab / Datasets',
    'Training Target (corrected',
    'item.correctionInstruction',
    'item.correctionInstruction.empty()?"Reviewed":"Correction"'
)
foreach ($token in $correctionReviewMainTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected reviewed-correction workflow is missing: $token"
    }
}

$correctionReviewStoreTokens = @(
    'TrainingTargetText(const TrainingReviewItem& item)',
    'TrainingReviewStore::SetCorrectionTarget',
    'target_output_text',
    'correction_instruction',
    'status=0,reviewer=',
    'JsonEscape(TrainingTargetText(item))'
)
foreach ($token in $correctionReviewStoreTokens) {
    if (-not $trainingReviewSrc.Contains($token)) {
        throw "Protected correction-target review store is missing: $token"
    }
}

$correctionModelTokens = @(
    'GenerateCorrectionPreview(',
    'originalInput',
    'originalResponse',
    'trainerInstruction'
)
foreach ($token in $correctionModelTokens) {
    if (-not $modelAdapterSrc.Contains($token)) {
        throw "Correction-preview model contract is missing: $token"
    }
    if (-not $openAiModelSrc.Contains($token)) {
        throw "Connected-model correction preview is missing: $token"
    }
}

$correctionMigrationTokens = @(
    'correction_instruction',
    'target_output_text',
    'correction_updated_utc'
)
foreach ($token in $correctionMigrationTokens) {
    if (-not $correctionMigrationSrc.Contains($token)) {
        throw "Correction-target schema migration is missing: $token"
    }
}

$jobWorkflowTokens = @(
    'job_cancel_selected',
    'job_retry_selected',
    'CancelQueuedJob(',
    'RetryJob(',
    'job_recover_stale',
    'RecoverStaleRunningJobs(',
    'Only queued jobs can be cancelled from SARA'
)
foreach ($token in $jobWorkflowTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected Trainer Jobs recovery workflow is missing: $token"
    }
}

$labTabs = @(
    'L"Overview"', 'L"Train"', 'L"Datasets"', 'L"Personas & LoRAs"',
    'L"Foundation Forks"', 'L"Jobs"', 'L"Evaluation"', 'L"Deployment"'
)
foreach ($token in $labTabs) {
    if (-not $src.Contains($token)) {
        throw "Required Model Lab internal workspace is missing: $token"
    }
}

$brandStart = $src.IndexOf("void DrawBrand()")
$brandEnd = $src.IndexOf("void DrawSidebar()", $brandStart)
if ($brandStart -lt 0 -or $brandEnd -le $brandStart) {
    throw "Could not inspect SARA brand renderer."
}
$brand = $src.Substring($brandStart, $brandEnd - $brandStart)
if ($brand.Contains("plateX") -or $brand.Contains("Rounded(plate")) {
    throw "White/light logo plate was reintroduced. Approved SARA logo must render transparently."
}

Write-Host "SARA product architecture contract: PASS"
Write-Host "Permanent main navigation: $($required -join ' | ')"
Write-Host "Model Lab remains subordinate with internal tabs."
Write-Host "Simulation Chat and investigative modules remain protected."

$evaluationGateMainTokens = @(
    'EvaluationPassedApprovalGate(run)',
    'EvaluationMatchesCurrentRuntime(run)',
    'modelRegistry_.Approve(',
    'deploymentRegistry_.Prepare('
)
foreach ($token in $evaluationGateMainTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected Model Lab evaluation-gate wiring is missing: $token"
    }
}

$evaluationGateModelTokens = @(
    'const EvaluationRun& evaluation',
    'EvaluationPassedApprovalGate(evaluation)',
    'evaluation.candidateId!=model.id',
    'evaluation.foundationId!=currentFoundationId',
    'evaluation.adapterId!=currentAdapterId',
    'model.approvedEvaluationRunId=evaluation.id',
    'model.approvedFoundationId=evaluation.foundationId',
    'model.approvedAdapterId=evaluation.adapterId',
    'models_[i].approvedEvaluationRunId.empty()'
)
foreach ($token in $evaluationGateModelTokens) {
    if (-not $modelRegistrySrc.Contains($token)) {
        throw "Core model-approval evaluation gate is missing: $token"
    }
}

$evaluationGateDeploymentTokens = @(
    'const RegisteredModel& model',
    'const EvaluationRun& evaluation',
    'model.stage!=ModelStage::Approved',
    'evaluation.candidateId!=model.id',
    'EvaluationPassedApprovalGate(evaluation)',
    'model.approvedEvaluationRunId!=evaluation.id',
    'model.approvedFoundationId!=evaluation.foundationId',
    'model.approvedAdapterId!=evaluation.adapterId',
    'deployment preparation requires a complete passing evaluation',
    'package.evaluationRunId=evaluation.id'
)
foreach ($token in $evaluationGateDeploymentTokens) {
    if (-not $deploymentRegistrySrc.Contains($token)) {
        throw "Core deployment evaluation gate is missing: $token"
    }
}

