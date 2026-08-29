param(
    [Parameter(Mandatory = $true)][int]$AppProcessId,
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\artifacts\ui-tests')
)

$ErrorActionPreference = 'Stop'
$settingsPath = Join-Path $env:LOCALAPPDATA 'LumaShot\settings.json'
$requiredAutomationIds = @(
    'StartCaptureButton',
    'RegionModeButton',
    'WindowModeButton',
    'MonitorModeButton',
    'DesktopModeButton',
    'HotkeyBox',
    'IncludeCursorToggle',
    'CopyOnEnterToggle',
    'HdrCalibrationButton',
    'LanguageCombo',
    'LaunchAtLoginToggle'
)

function Invoke-WinApp {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $output = & winapp @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "winapp failed: winapp $($Arguments -join ' ')"
    }
    return $output
}

function Wait-SettingsValue {
    param([string]$Name, $Expected)
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        if (Test-Path $settingsPath) {
            $settings = Get-Content -Raw $settingsPath | ConvertFrom-Json
            if ($settings.$Name -eq $Expected) { return }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for setting '$Name' to become '$Expected'."
}

New-Item -ItemType Directory -Force $ArtifactDirectory | Out-Null
$initialSettings = Get-Content -Raw $settingsPath | ConvertFrom-Json
$initialMode = $initialSettings.lastCaptureMode
$initialIncludeCursor = [bool]$initialSettings.includeCursor

$tree = (Invoke-WinApp -Arguments @('ui', 'inspect', '-a', $AppProcessId, '-i', '--json')) | ConvertFrom-Json
$elements = @($tree.windows[0].elements)
foreach ($automationId in $requiredAutomationIds) {
    $element = $elements | Where-Object automationId -eq $automationId | Select-Object -First 1
    if (-not $element) { throw "Missing UI Automation element: $automationId" }
    if (-not $element.isEnabled) { throw "Disabled UI Automation element: $automationId" }
    if ([string]::IsNullOrWhiteSpace($element.name)) { throw "Missing accessible name: $automationId" }
}

try {
    Invoke-WinApp -Arguments @('ui', 'invoke', 'WindowModeButton', '-a', $AppProcessId, '--quiet') | Out-Null
    Wait-SettingsValue lastCaptureMode 'window'

    Invoke-WinApp -Arguments @('ui', 'invoke', 'IncludeCursorToggle', '-a', $AppProcessId, '--quiet') | Out-Null
    Wait-SettingsValue includeCursor (-not $initialIncludeCursor)

    $screenshot = Join-Path $ArtifactDirectory 'control-center-tested.png'
    Invoke-WinApp -Arguments @('ui', 'screenshot', '-a', $AppProcessId, '--focus', '--output', $screenshot, '--quiet') | Out-Null
}
finally {
    $modeSelector = switch ($initialMode) {
        'window' { 'WindowModeButton' }
        'monitor' { 'MonitorModeButton' }
        'virtualDesktop' { 'DesktopModeButton' }
        default { 'RegionModeButton' }
    }
    Invoke-WinApp -Arguments @('ui', 'invoke', $modeSelector, '-a', $AppProcessId, '--quiet') | Out-Null
    Wait-SettingsValue lastCaptureMode $initialMode

    $currentSettings = Get-Content -Raw $settingsPath | ConvertFrom-Json
    if ([bool]$currentSettings.includeCursor -ne $initialIncludeCursor) {
        Invoke-WinApp -Arguments @('ui', 'invoke', 'IncludeCursorToggle', '-a', $AppProcessId, '--quiet') | Out-Null
        Wait-SettingsValue includeCursor $initialIncludeCursor
    }
}

Write-Output "PASS: Control center UI, accessibility metadata, and settings persistence."
