# D2R-PCVR test run for the user. The agent never runs -Action Run: reaching the game world needs clicks
# in the game window (see .github\copilot-instructions.md, "D2R-PCVR Tests").
[CmdletBinding()]
param(
    [ValidateSet('Run', 'Stop', 'Status')]
    [string]$Action = 'Status',

    [string]$GameDir = 'C:\Progs\Diablo II Resurrected',

    [string]$SimulatorRoot = 'A:\ws\OpenXR-Simulator-test',

    # Runs on the user's active OpenXR runtime (headset) instead of the per-process OpenXR Simulator.
    [switch]$UseActiveRuntime,

    # Control run: no add-on layer at all (the installed one stays off too).
    [switch]$WithoutAddon,

    [string]$AddonBuildDir = 'build\reshade-openxr-ui',

    # Leaves the game running after a successful capture; close it later with -Action Stop.
    [switch]$KeepRunning,

    # 0 skips the simulator capture.
    [ValidateRange(0, 100000)]
    [int]$CaptureFrame = 300,

    [ValidateSet('projection', 'quad', 'all')]
    [string]$CaptureLayer = 'all',

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
$loaderPath = Join-Path $GameDir 'D2RLoader.exe'
$modDir = Join-Path $GameDir 'mods\D2R-PCVR'
$modLogs = [ordered]@{
    'ReShade.log' = Join-Path $modDir 'reshade\ReShade.log'
    'd2r-vr-frame.log' = Join-Path $modDir 'd2rloader\logs\d2r-vr-frame.log'
    'd2r-pcvr-vrcam.log' = Join-Path $modDir 'd2rloader\logs\d2r-pcvr-vrcam.log'
}
$workDir = Join-Path $repoRoot 'build\d2r-pcvr'
$outputDirectory = Join-Path $workDir 'last-run'
$testLayerDir = Join-Path $workDir 'layers'
$testLayerName = 'XR_APILAYER_reshade_vr_ui_d2r_test'
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
$gameProcessNames = @('D2RLoader', 'D2R')

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
        if ($Game.HasExited) { throw "D2RLoader exited (code $($Game.ExitCode)) while waiting for: $Description" }
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 250
    }
    throw "Timed out after $TimeoutSeconds s waiting for: $Description"
}

# The game keeps its logs open for writing while it runs.
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

function Initialize-TestFiles {
    if (-not (Test-Path -LiteralPath $addonPath -PathType Leaf)) { throw "Required file not found: $addonPath" }
    New-Item -ItemType Directory -Path $testLayerDir -Force | Out-Null
    [ordered]@{
        file_format_version = '1.0.0'
        api_layer = [ordered]@{
            name = $testLayerName
            library_path = $addonPath
            api_version = '1.0'
            implementation_version = '1'
            description = 'ReShade VR UI Add-on (D2R-PCVR test build)'
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

# Copies only; the game folder and its logs stay untouched.
function Save-Artifacts {
    param($GamePid)

    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    foreach ($entry in $modLogs.GetEnumerator()) {
        if (Test-Path -LiteralPath $entry.Value -PathType Leaf) {
            Read-SharedText -Path $entry.Value | Set-Content -LiteralPath (Join-Path $outputDirectory $entry.Key) -Encoding UTF8
        }
    }
    if ($null -ne $GamePid) {
        $simulatorLog = Join-Path $simulatorDataDirectory "openxr_simulator.$GamePid.log"
        if (Test-Path -LiteralPath $simulatorLog -PathType Leaf) {
            Read-SharedText -Path $simulatorLog | Set-Content -LiteralPath (Join-Path $outputDirectory 'openxr-simulator.log') -Encoding UTF8
        }
    }
}

function Stop-Game {
    param($GamePid)

    $targets = @(Get-RunningGames)
    if ($null -ne $GamePid) { $targets = @($targets | Where-Object { $_.Id -eq $GamePid }) + @($targets | Where-Object { $_.Id -ne $GamePid }) }
    foreach ($process in $targets) {
        if ($process.HasExited) { continue }
        $null = $process.CloseMainWindow()
        if (-not $process.WaitForExit(10000)) {
            Stop-Process -Id $process.Id -Force -ErrorAction Ignore
            $null = $process.WaitForExit(10000)
        }
        Write-Host "Closed $($process.ProcessName) (pid $($process.Id))."
    }
}

function Complete-Session {
    param($GamePid)

    Save-Artifacts -GamePid $GamePid
    Stop-Game -GamePid $GamePid
    # The game finishes its logs while closing.
    Save-Artifacts -GamePid $GamePid
    if (Test-Path -LiteralPath $settingsBackupPath -PathType Leaf) {
        Copy-Item -LiteralPath $settingsBackupPath -Destination $simulatorSettingsPath -Force
        Remove-Item -LiteralPath $settingsBackupPath -Force
    }
    Remove-Item -LiteralPath $runStatePath -Force -ErrorAction Ignore
    Write-Host "Artifacts: $outputDirectory"
}

function Invoke-Run {
    $running = Get-RunningGames
    if ($running.Count -gt 0) { throw "Close Diablo II: Resurrected first: $(($running | ForEach-Object { "$($_.ProcessName) $($_.Id)" }) -join ', ')" }
    if (Test-Path -LiteralPath $runStatePath -PathType Leaf) { throw 'A previous test session was not closed. Run -Action Stop first.' }
    if (-not (Test-Path -LiteralPath $loaderPath -PathType Leaf)) { throw "D2RLoader not found: $loaderPath" }

    New-Item -ItemType Directory -Path $workDir -Force | Out-Null
    if (Test-Path -LiteralPath $outputDirectory) { Remove-Item -LiteralPath $outputDirectory -Recurse -Force }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    Initialize-TestFiles

    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $loaderPath
    $start.Arguments = '-mod D2R-PCVR'
    $start.WorkingDirectory = $GameDir
    $start.UseShellExecute = $false
    # These settings reach the game process only.
    $start.EnvironmentVariables['DISABLE_XR_APILAYER_reshade_vr_ui'] = '1'
    if (-not $WithoutAddon) {
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
        $game = [System.Diagnostics.Process]::Start($start)
        Write-Host "Started D2RLoader (pid $($game.Id))."
        [ordered]@{ GamePid = $game.Id; StartedAt = $startedAt.ToString('o') } |
            ConvertTo-Json | Set-Content -LiteralPath $runStatePath -Encoding ASCII

        $reshadeLogPath = $modLogs['ReShade.log']
        # ReShade starts a new log for each game start.
        Wait-Until -Game $game -Description "a new ReShade.log from this start" -Condition {
            (Test-Path -LiteralPath $reshadeLogPath -PathType Leaf) -and
            (Get-Item -LiteralPath $reshadeLogPath).LastWriteTimeUtc -ge $startedAt
        }
        if (-not $WithoutAddon) {
            Wait-Until -Game $game -Description 'the add-on under test in D2RLoader' -Condition {
                $null -ne (Get-ModulePath -Process $game -ModuleName 'ReShade-OpenXR-UI.addon64')
            }
            Write-Host "Add-on loaded: $(Get-ModulePath -Process $game -ModuleName 'ReShade-OpenXR-UI.addon64')"
            Wait-Until -Game $game -Description "the add-on's own-ReShade line in ReShade.log" -Condition {
                (Read-SharedText -Path $reshadeLogPath) -match 'this game loaded its own ReShade'
            }
            Write-Host 'The add-on recognised the mod''s own ReShade.'
        }

        if (-not $UseActiveRuntime -and $CaptureFrame -gt 0) {
            Wait-Until -Game $game -Description 'openxr_simulator.dll in D2RLoader' -Condition {
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
                requested_by = 'd2r-pcvr-test'
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
            Write-Host "Left running: D2RLoader (pid $gamePid). Close with -Action Stop."
            Write-Host 'The game now waits at its start screen (it only looks frozen). A person must click the D2R window, press a key, then click Play to enter the world.'
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
    Write-Host "D2RLoader: $(Test-Path -LiteralPath $loaderPath -PathType Leaf)  $loaderPath"
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
