[CmdletBinding()]
param(
    [ValidateSet('Run', 'Stop', 'Status')]
    [string]$Action = 'Status',

    [string]$GameDir = 'A:\SteamLibrary\steamapps\common\SOMA',

    [string]$SimulatorRoot = 'A:\ws\OpenXR-Simulator-test',

    # Runs on the user's active OpenXR runtime (headset) instead of the per-process OpenXR Simulator.
    [switch]$UseActiveRuntime,

    # Control run: disables both the ReShade and the add-on layers in the game process.
    [switch]$WithoutLayers,

    # Add-on only: disables the ReShade layer but keeps the add-on layer under test.
    [switch]$WithoutReShade,

    # Built-in ReShade add-ons to turn off in the test ReShade.ini, for example 'Generic Depth'.
    [string[]]$DisabledReShadeAddons = @(),

    # Loads the effects installed in the game folder (read only) and starts from a copy of its preset,
    # instead of the test invert effect. Like a normal start, the monitor window gets them too.
    [switch]$UseGameEffects,

    [string]$AddonBuildDir = 'build\reshade-openxr-ui',

    # Leaves SOMA running after a successful capture; close it later with -Action Stop.
    [switch]$KeepRunning,

    # 0 skips the simulator capture.
    [ValidateRange(0, 100000)]
    [int]$CaptureFrame = 300,

    [ValidateSet('projection', 'quad', 'all')]
    [string]$CaptureLayer = 'projection',

    # Limit for each wait step; on timeout the game is stopped.
    [ValidateRange(5, 600)]
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not [System.IO.Path]::IsPathRooted($AddonBuildDir)) {
    $AddonBuildDir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $AddonBuildDir))
}
$addonPath = Join-Path $AddonBuildDir 'ReShade-OpenXR-UI.addon64'
$launcherPath = Join-Path $GameDir 'Launch-SOMA-VR.ps1'
$modLogPath = Join-Path $GameDir 'hpl3vr.log'
$workDir = Join-Path $repoRoot 'build\soma-vr'
$outputDirectory = Join-Path $workDir 'last-run'
$reshadeBaseDir = Join-Path $workDir 'reshade-base'
$testLayerDir = Join-Path $workDir 'layers'
$testLayerName = 'XR_APILAYER_reshade_vr_ui_soma_test'
$testEffectPath = Join-Path $PSScriptRoot 'openxr\DeterministicInvert.fx'
$simulatorDllPath = Join-Path $SimulatorRoot 'openxr_simulator.dll'
$runtimeManifestPath = Join-Path $workDir 'openxr_simulator.json'
$runStatePath = Join-Path $workDir 'run-state.json'
$settingsBackupPath = Join-Path $workDir 'simulator-settings.backup.json'
$simulatorDataDirectory = Join-Path $env:LOCALAPPDATA 'OpenXR-Simulator'
$simulatorSettingsPath = Join-Path $simulatorDataDirectory 'settings.json'
$runtimeStatusPath = Join-Path $simulatorDataDirectory 'runtime_status.json'
$screenshotRequestPath = Join-Path $simulatorDataDirectory 'screenshot_request.json'
$screenshotPath = Join-Path $simulatorDataDirectory 'screenshot.bmp'
$screenshotStatusPath = Join-Path $simulatorDataDirectory 'screenshot_status.json'
$gameProcessNames = @('Soma', 'Soma_NoSteam')

function Get-RunningGames {
    return @(Get-Process -Name $gameProcessNames -ErrorAction Ignore)
}

function Get-ModulePath {
    param([Parameter(Mandatory = $true)]$Process, [Parameter(Mandatory = $true)][string]$ModuleName)

    try {
        $Process.Refresh()
        $module = $Process.Modules | Where-Object { $_.ModuleName -eq $ModuleName } | Select-Object -First 1
        if ($module) { return $module.FileName }
    }
    catch {
        # Module enumeration fails while the process is starting or exiting.
    }
    return $null
}

function Wait-Until {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Condition,
        [Parameter(Mandatory = $true)][string]$Description,
        [Parameter(Mandatory = $true)]$Game
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Game.HasExited) { throw "SOMA exited (code $($Game.ExitCode)) while waiting for: $Description" }
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 250
    }
    throw "Timed out after $TimeoutSeconds s waiting for: $Description"
}

# ReShade keeps its log open for writing while the game runs.
function Read-SharedText {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $stream = [System.IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
    try { return (New-Object System.IO.StreamReader($stream)).ReadToEnd() }
    finally { $stream.Dispose() }
}

function Get-SimulatorFrame {
    param([Parameter(Mandatory = $true)][DateTime]$Since)

    if (-not (Test-Path -LiteralPath $runtimeStatusPath -PathType Leaf) -or
        (Get-Item -LiteralPath $runtimeStatusPath).LastWriteTimeUtc -lt $Since) {
        return 0
    }
    try { return [int](Get-Content -LiteralPath $runtimeStatusPath -Raw | ConvertFrom-Json).frame_count }
    catch { return 0 }
}

function Set-SimulatorCaptureWindow {
    $settings = if (Test-Path -LiteralPath $simulatorSettingsPath -PathType Leaf) {
        Get-Content -LiteralPath $simulatorSettingsPath -Raw | ConvertFrom-Json
    }
    else {
        [pscustomobject]@{}
    }
    $captureSettings = [ordered]@{
        view_mode = 'both'
        zoom_mode = 'fill'
        show_stats = $false
        window_width = 1280
        window_height = 720
        preview_fps = 90
    }
    foreach ($entry in $captureSettings.GetEnumerator()) {
        $settings | Add-Member -NotePropertyName $entry.Key -NotePropertyValue $entry.Value -Force
    }
    $settings | ConvertTo-Json | Set-Content -LiteralPath $simulatorSettingsPath -Encoding ASCII
}

# The layer-loaded ReShade uses this folder instead of the game folder, so it writes nothing there.
function Initialize-TestFiles {
    foreach ($path in @($addonPath, $testEffectPath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file not found: $path" }
    }
    if (Test-Path -LiteralPath $reshadeBaseDir) { Remove-Item -LiteralPath $reshadeBaseDir -Recurse -Force }
    $shaderDir = Join-Path $reshadeBaseDir 'Shaders'
    New-Item -ItemType Directory -Path $shaderDir -Force | Out-Null
    Copy-Item -LiteralPath $testEffectPath -Destination $shaderDir
    $reshadeIni = "[INSTALL]`r`nLogging=1`r`n"
    if ($DisabledReShadeAddons.Count -gt 0) {
        $reshadeIni += "[ADDON]`r`nDisabledAddons=$($DisabledReShadeAddons -join ',')`r`n"
    }
    $gameEffectSettings = ''
    if ($UseGameEffects) {
        $shaders = Join-Path $GameDir 'reshade-shaders\Shaders\**'
        $textures = Join-Path $GameDir 'reshade-shaders\Textures\**'
        $gameEffectSettings = "[GENERAL]`r`nEffectSearchPaths=$shaders`r`nTextureSearchPaths=$textures`r`nPresetPath=.\GamePreset.ini`r`n"
        $reshadeIni += $gameEffectSettings
    }
    $reshadeIni | Set-Content -LiteralPath (Join-Path $reshadeBaseDir 'ReShade.ini') -Encoding ASCII
    if ($UseGameEffects) {
        $gamePreset = Join-Path $GameDir 'ReShadePreset.ini'
        $presetName = 'GamePreset.ini'
        if (Test-Path -LiteralPath $gamePreset -PathType Leaf) {
            Copy-Item -LiteralPath $gamePreset -Destination (Join-Path $reshadeBaseDir $presetName)
        }
        else {
            '' | Set-Content -LiteralPath (Join-Path $reshadeBaseDir $presetName) -Encoding ASCII
        }
        $gameEffectSettings |
            Set-Content -LiteralPath (Join-Path $reshadeBaseDir 'ReShadeVR.ini') -Encoding ASCII
    }
    else {
        "[GENERAL]`r`nEffectSearchPaths=.\Shaders`r`nPresetPath=.\InvertOn.ini`r`n" |
            Set-Content -LiteralPath (Join-Path $reshadeBaseDir 'ReShadeVR.ini') -Encoding ASCII
    }
    "Techniques=DeterministicInvert@DeterministicInvert.fx`r`nTechniqueSorting=DeterministicInvert@DeterministicInvert.fx`r`n" |
        Set-Content -LiteralPath (Join-Path $reshadeBaseDir 'InvertOn.ini') -Encoding ASCII

    New-Item -ItemType Directory -Path $testLayerDir -Force | Out-Null
    [ordered]@{
        file_format_version = '1.0.0'
        api_layer = [ordered]@{
            name = $testLayerName
            library_path = $addonPath
            api_version = '1.0'
            implementation_version = '1'
            description = 'ReShade VR UI Add-on (SOMA-VR test build)'
        }
    } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $testLayerDir 'ReShade-OpenXR-UI-Test.json') -Encoding ASCII

    if (-not $UseActiveRuntime) {
        if (-not (Test-Path -LiteralPath $simulatorDllPath -PathType Leaf)) { throw "Required file not found: $simulatorDllPath" }
        [ordered]@{
            file_format_version = '1.0.0'
            runtime = [ordered]@{ library_path = $simulatorDllPath }
        } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $runtimeManifestPath -Encoding ASCII
    }
}

function Save-Artifacts {
    param($GamePid)

    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $reshadeBaseDir -Filter 'ReShade*.log*' -File -ErrorAction Ignore |
        Copy-Item -Destination $outputDirectory -Force
    if (Test-Path -LiteralPath $modLogPath -PathType Leaf) {
        Copy-Item -LiteralPath $modLogPath -Destination (Join-Path $outputDirectory 'hpl3vr.log') -Force
    }
    if ($null -ne $GamePid) {
        $simulatorLog = Join-Path $simulatorDataDirectory "openxr_simulator.$GamePid.log"
        if (Test-Path -LiteralPath $simulatorLog -PathType Leaf) {
            Copy-Item -LiteralPath $simulatorLog -Destination (Join-Path $outputDirectory 'openxr-simulator.log') -Force
        }
    }
}

function Complete-Session {
    param($GamePid)

    # The mod's own launcher closes SOMA; it never stops Steam or the VR runtime.
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $launcherPath -StopOnly
    if (Test-Path -LiteralPath $settingsBackupPath -PathType Leaf) {
        Copy-Item -LiteralPath $settingsBackupPath -Destination $simulatorSettingsPath -Force
        Remove-Item -LiteralPath $settingsBackupPath -Force
    }
    Save-Artifacts -GamePid $GamePid
    Remove-Item -LiteralPath $runStatePath -Force -ErrorAction Ignore
    Write-Host "Artifacts: $outputDirectory"
}

function Invoke-Run {
    $running = Get-RunningGames
    if ($running.Count -gt 0) { throw "Close SOMA first: $(($running | ForEach-Object { $_.Id }) -join ', ')" }
    if (Test-Path -LiteralPath $runStatePath -PathType Leaf) { throw 'A previous test session was not closed. Run -Action Stop first.' }
    if (-not (Test-Path -LiteralPath $launcherPath -PathType Leaf)) { throw "Mod launcher not found: $launcherPath" }

    New-Item -ItemType Directory -Path $workDir -Force | Out-Null
    if (Test-Path -LiteralPath $outputDirectory) { Remove-Item -LiteralPath $outputDirectory -Recurse -Force }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    Initialize-TestFiles

    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = 'powershell.exe'
    $start.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$launcherPath`""
    $start.WorkingDirectory = $GameDir
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # The launcher starts SOMA with its own environment, so these settings reach the game process only.
    $start.EnvironmentVariables['RESHADE_BASE_PATH_OVERRIDE'] = $reshadeBaseDir
    $start.EnvironmentVariables['DISABLE_XR_APILAYER_reshade_vr_ui'] = '1'
    if ($WithoutLayers -or $WithoutReShade) {
        $start.EnvironmentVariables['DISABLE_XR_APILAYER_reshade_1'] = '1'
    }
    if (-not $WithoutLayers) {
        $start.EnvironmentVariables['XR_API_LAYER_PATH'] = $testLayerDir
        $start.EnvironmentVariables['XR_ENABLE_API_LAYERS'] = $testLayerName
    }
    if (-not $UseActiveRuntime) {
        $start.EnvironmentVariables['XR_RUNTIME_JSON'] = $runtimeManifestPath
        New-Item -ItemType Directory -Path $simulatorDataDirectory -Force | Out-Null
        if (Test-Path -LiteralPath $simulatorSettingsPath -PathType Leaf) {
            Copy-Item -LiteralPath $simulatorSettingsPath -Destination $settingsBackupPath -Force
        }
        Set-SimulatorCaptureWindow
        Remove-Item -LiteralPath $screenshotRequestPath, $screenshotPath, $screenshotStatusPath -Force -ErrorAction Ignore
    }

    $startedAt = [DateTime]::UtcNow
    $game = $null
    $succeeded = $false
    try {
        $launcher = [System.Diagnostics.Process]::Start($start)
        if (-not $launcher.WaitForExit($TimeoutSeconds * 1000)) {
            $launcher.Kill()
            throw "The mod launcher did not finish within $TimeoutSeconds s."
        }
        $launcherOutput = $launcher.StandardOutput.ReadToEnd() + $launcher.StandardError.ReadToEnd()
        $launcherOutput | Set-Content -LiteralPath (Join-Path $outputDirectory 'launcher.txt') -Encoding UTF8
        if ($launcherOutput -notmatch 'SOMA_VR_PID=(\d+)') { throw "The mod launcher did not start SOMA:`n$launcherOutput" }
        $game = Get-Process -Id ([int]$Matches[1])
        Write-Host "Started SOMA (pid $($game.Id))."
        [ordered]@{ GamePid = $game.Id; StartedAt = $startedAt.ToString('o') } |
            ConvertTo-Json | Set-Content -LiteralPath $runStatePath -Encoding ASCII

        if (-not $WithoutLayers) {
            Wait-Until -Game $game -Description 'the add-on under test in SOMA' -Condition {
                $null -ne (Get-ModulePath -Process $game -ModuleName 'ReShade-OpenXR-UI.addon64')
            }
            Write-Host "Add-on loaded: $(Get-ModulePath -Process $game -ModuleName 'ReShade-OpenXR-UI.addon64')"
        }
        if (-not $WithoutLayers -and -not $WithoutReShade) {
            $reshadeLogPath = Join-Path $reshadeBaseDir 'ReShade.log'
            # ReShade reports the ReShadeVR.ini runtime only once the game submits stereo gameplay frames, so the
            # session-level result is checked here: the add-on re-binds the context, and ReShade must not skip the session.
            Wait-Until -Game $game -Description 'the add-on OpenGL context re-bind in ReShade.log' -Condition {
                (Read-SharedText -Path $reshadeLogPath) -match 'OpenGL session context re-bound|OpenGL context re-bind failed|OpenGL session context is not current'
            }
            $reshadeLog = Read-SharedText -Path $reshadeLogPath
            if ($reshadeLog -match 'Skipping OpenXR session[^\r\n]*') { throw "ReShade skipped the headset session: $($Matches[0])" }
            if ($reshadeLog -notmatch 'OpenGL session context re-bound') { throw 'The add-on could not re-bind the OpenGL context; see ReShade.log.' }
            Write-Host 'ReShade accepted the OpenGL headset session.'
        }

        if (-not $UseActiveRuntime -and $CaptureFrame -gt 0) {
            Wait-Until -Game $game -Description 'openxr_simulator.dll in SOMA' -Condition {
                $null -ne (Get-ModulePath -Process $game -ModuleName 'openxr_simulator.dll')
            }
            Wait-Until -Game $game -Description "simulator frame $CaptureFrame" -Condition {
                (Get-SimulatorFrame -Since $startedAt) -ge $CaptureFrame
            }
            $requestedAt = [DateTime]::UtcNow
            [ordered]@{
                timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() / 1000.0
                eye = 'both'
                layer = $CaptureLayer
                requested_by = 'soma-vr-test'
            } | ConvertTo-Json | Set-Content -LiteralPath $screenshotRequestPath -Encoding ASCII
            Wait-Until -Game $game -Description 'simulator screenshot' -Condition {
                (Test-Path -LiteralPath $screenshotStatusPath -PathType Leaf) -and
                (Test-Path -LiteralPath $screenshotPath -PathType Leaf) -and
                (Get-Item -LiteralPath $screenshotStatusPath).LastWriteTimeUtc -ge $requestedAt
            }
            Start-Sleep -Milliseconds 500
            Add-Type -AssemblyName System.Drawing
            $bitmap = [System.Drawing.Bitmap]::FromFile($screenshotPath)
            try { $bitmap.Save((Join-Path $outputDirectory 'headset.png'), [System.Drawing.Imaging.ImageFormat]::Png) }
            finally { $bitmap.Dispose() }
            Copy-Item -LiteralPath $screenshotStatusPath -Destination $outputDirectory -Force
            Write-Host "Saved headset capture: $(Join-Path $outputDirectory 'headset.png')"
        }
        $succeeded = $true
    }
    finally {
        $gamePid = if ($null -ne $game) { $game.Id } else { $null }
        if ($succeeded -and $KeepRunning) {
            Save-Artifacts -GamePid $gamePid
            Write-Host "Left running: SOMA (pid $gamePid). Close with -Action Stop."
        }
        else {
            Complete-Session -GamePid $gamePid
        }
    }
}

function Invoke-Stop {
    $gamePid = $null
    if (Test-Path -LiteralPath $runStatePath -PathType Leaf) {
        $gamePid = (Get-Content -LiteralPath $runStatePath -Raw | ConvertFrom-Json).GamePid
    }
    Complete-Session -GamePid $gamePid
}

function Show-Status {
    Write-Host "Launcher:  $(Test-Path -LiteralPath $launcherPath -PathType Leaf)  $launcherPath"
    Write-Host "Add-on:    $(Test-Path -LiteralPath $addonPath -PathType Leaf)  $addonPath"
    Write-Host "Simulator: $(Test-Path -LiteralPath $simulatorDllPath -PathType Leaf)  $simulatorDllPath"
    Get-RunningGames | ForEach-Object { Write-Host "Running: $($_.ProcessName) ($($_.Id))" }
    if (Test-Path -LiteralPath $runStatePath -PathType Leaf) { Write-Host "Open test session: $runStatePath (close it with -Action Stop)" }
}

switch ($Action) {
    'Run' { Invoke-Run }
    'Stop' { Invoke-Stop }
    'Status' { Show-Status }
}
