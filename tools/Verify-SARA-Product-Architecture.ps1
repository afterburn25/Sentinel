param()

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$mainPath = Join-Path $repo "src\Gui\Main.cpp"
$contractPath = Join-Path $repo "docs\SARA_PRODUCT_ARCHITECTURE.md"

if (-not (Test-Path $contractPath)) {
    throw "SARA product architecture contract is missing: $contractPath"
}
if (-not (Test-Path $mainPath)) {
    throw "SARA GUI source is missing: $mainPath"
}

$src = Get-Content $mainPath -Raw

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
    'L"Deployment"'
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

$personaWorkflowTokens = @(
    'L"Rules & Learning"',
    'L"persona_tab_rules"',
    'PersonaResponseRules(50)',
    'rule_toggle:',
    'rule_delete:'
)
foreach ($token in $personaWorkflowTokens) {
    if (-not $src.Contains($token)) {
        throw "Protected Persona Rules & Learning workflow is missing: $token"
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
