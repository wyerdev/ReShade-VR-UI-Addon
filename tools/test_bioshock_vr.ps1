[CmdletBinding()]
param(
    [ValidateSet('Run', 'Stop', 'Status', 'BuildSimulator')]
    [string]$Action = 'Status',

    [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\BioShock Remastered\Build\Final',

    # The one OpenXR Simulator checkout. Its 32-bit build goes to its ignored bin-x86 folder; bin keeps the 64-bit one.
    [string]$SimulatorSource = 'A:\ws\OpenXR-Simulator',

    # Add-on only: does not load ReShade32 into the game.
    [switch]$WithoutReShade,

    [string]$AddonBuildDir = 'build\reshade-openxr-ui',

    # Leaves BioShock running after a successful capture; close it later with -Action Stop.
    [switch]$KeepRunning,

    # Skips the intro videos and continues the last save, so the capture shows the game world.
    [switch]$EnterGame,

    # Runs on the headset's own OpenXR runtime instead of the simulator; no capture is taken.
    [switch]$UseActiveRuntime,

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
$addonName = 'ReShade-OpenXR-UI.addon32'
$addonPath = Join-Path $AddonBuildDir $addonName
$reshadeDllPath = Join-Path $env:ProgramData 'ReShade\ReShade32.dll'
$gameExePath = Join-Path $GameDir 'BioshockHD.exe'
$steamAppId = '409710'
$modDataDirectory = Join-Path $env:LOCALAPPDATA 'BioshockVR'
$modLogPath = Join-Path $modDataDirectory 'bioshockvr.log'
# BioShock creates this while it runs and deletes it on a clean exit; a leftover copy makes it offer to revert all settings.
$runningMarkerPath = Join-Path $env:APPDATA 'BioshockHD\Bioshock\Running.ini'
$workDir = Join-Path $repoRoot 'build\bioshock-vr'
$outputDirectory = Join-Path $workDir 'last-run'
$reshadeBaseDir = Join-Path $workDir 'reshade-base'
$testLayerDir = Join-Path $workDir 'layers'
$testLayerName = 'XR_APILAYER_reshade_vr_ui_bioshock_test'
$testReShadeLayerName = 'XR_APILAYER_reshade_bioshock_test'
$testEffectPath = Join-Path $PSScriptRoot 'openxr\DeterministicInvert.fx'
$vulkanInclude = Join-Path $repoRoot '.deps\Vulkan-Headers\include'
$simulatorObjDir = Join-Path $SimulatorSource 'build-x86'
$simulatorDllPath = Join-Path $SimulatorSource 'bin-x86\openxr_simulator.dll'
$runtimeManifestPath = Join-Path $workDir 'openxr_simulator32.json'
$runStatePath = Join-Path $workDir 'run-state.json'
$settingsBackupPath = Join-Path $workDir 'simulator-settings.backup.json'
$simulatorDataDirectory = Join-Path $env:LOCALAPPDATA 'OpenXR-Simulator'
$simulatorSettingsPath = Join-Path $simulatorDataDirectory 'settings.json'
$runtimeStatusPath = Join-Path $simulatorDataDirectory 'runtime_status.json'
$screenshotRequestPath = Join-Path $simulatorDataDirectory 'screenshot_request.json'
$screenshotPath = Join-Path $simulatorDataDirectory 'screenshot.bmp'
$screenshotStatusPath = Join-Path $simulatorDataDirectory 'screenshot_status.json'
$gameProcessName = 'BioshockHD'

function Get-RunningGames {
    return @(Get-Process -Name $gameProcessName -ErrorAction Ignore)
}

# Module lists of a 32-bit game are only complete when read from a 32-bit process.
function Get-ModulePath {
    param([Parameter(Mandatory = $true)]$Process, [Parameter(Mandatory = $true)][string]$ModuleName)

    $output = & "$env:SystemRoot\SysWOW64\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -Command "try { (Get-Process -Id $($Process.Id)).Modules | Where-Object { `$_.ModuleName -eq '$ModuleName' } | Select-Object -First 1 -ExpandProperty FileName } catch { }"
    if ($output) { return ($output | Select-Object -First 1) }
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
        if ($Game.HasExited) { throw "BioShock exited (code $($Game.ExitCode)) while waiting for: $Description" }
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 250
    }
    throw "Timed out after $TimeoutSeconds s waiting for: $Description"
}

# ReShade and the mod keep their logs open for writing while the game runs.
function Read-SharedText {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $stream = [System.IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
    try { return (New-Object System.IO.StreamReader($stream)).ReadToEnd() }
    finally { $stream.Dispose() }
}

function Copy-SharedFile {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Destination)

    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        Read-SharedText -Path $Path | Set-Content -LiteralPath $Destination -Encoding UTF8
    }
}

function Find-ModLogLine {
    param([Parameter(Mandatory = $true)][string]$Pattern)

    $text = Read-SharedText -Path $modLogPath
    # The mod appends to its log, so only text written after this run started counts.
    if ($text.Length -ge $script:modLogStartLength) { $text = $text.Substring($script:modLogStartLength) }
    return $text -match $Pattern
}

# BioShock pauses while its window is in the background, and the simulator's preview window takes focus when it opens.
function Set-GameForeground {
    param([Parameter(Mandatory = $true)]$Game, [switch]$Click)

    if (-not ('BioShockTestWindow' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BioShockTestWindow {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int x, int y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr window, System.Text.StringBuilder name, int size);

    // The simulator's preview window belongs to the game process too, so the largest other visible window is the game's.
    public static IntPtr FindGameWindow(uint gameProcessId) {
        IntPtr best = IntPtr.Zero;
        long bestArea = 0;
        EnumWindows((window, parameter) => {
            uint processId;
            GetWindowThreadProcessId(window, out processId);
            if (processId != gameProcessId || !IsWindowVisible(window)) return true;
            var name = new System.Text.StringBuilder(64);
            GetClassName(window, name, name.Capacity);
            if (name.ToString() == "OpenXR Simulator") return true;
            RECT rect;
            GetWindowRect(window, out rect);
            long area = (long)(rect.Right - rect.Left) * (rect.Bottom - rect.Top);
            if (area > bestArea) { best = window; bestArea = area; }
            return true;
        }, IntPtr.Zero);
        return best;
    }
}
'@
    }
    $window = [BioShockTestWindow]::FindGameWindow([uint32]$Game.Id)
    if ($window -eq [IntPtr]::Zero) { return }
    if ([BioShockTestWindow]::GetForegroundWindow() -ne $window) {
        # A synthetic Alt press lets a background process move another window to the front.
        [BioShockTestWindow]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
        [BioShockTestWindow]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
        [void][BioShockTestWindow]::ShowWindow($window, 5)
        [void][BioShockTestWindow]::SetForegroundWindow($window)
    }
    if ($Click) {
        # Top-left corner, away from the main menu's buttons.
        $rect = New-Object BioShockTestWindow+RECT
        [void][BioShockTestWindow]::GetWindowRect($window, [ref]$rect)
        [void][BioShockTestWindow]::SetCursorPos($rect.Left + 40, $rect.Top + 60)
        [BioShockTestWindow]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        [BioShockTestWindow]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    }
}

function Enter-GameWorld {
    param([Parameter(Mandatory = $true)]$Game)

    Wait-Until -Game $Game -Description 'the main menu (intro videos skipped)' -Condition {
        Set-GameForeground -Game $Game -Click
        Start-Sleep -Milliseconds 750
        Find-ModLogLine -Pattern 'view state: menu'
    }
    # The menu needs a moment before it accepts input; Enter picks Continue, the first entry.
    Start-Sleep -Seconds 3
    Wait-Until -Game $Game -Description 'the game world after Continue' -Condition {
        Set-GameForeground -Game $Game
        if (-not (Find-ModLogLine -Pattern 'cine edge exit')) {
            (New-Object -ComObject WScript.Shell).SendKeys('{ENTER}')
            Start-Sleep -Seconds 2
        }
        Find-ModLogLine -Pattern 'cine edge exit'
    }
    Write-Host 'In the game world.'
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

function Get-SimulatorSources {
    return @(Get-ChildItem -LiteralPath (Join-Path $SimulatorSource 'src'), (Join-Path $SimulatorSource 'include') -Recurse -File -Include *.cpp, *.h)
}

# Builds the simulator's own sources for 32-bit games. Its CMake build always writes to bin, which holds the 64-bit DLL.
function Update-Simulator32 {
    if (-not (Test-Path -LiteralPath (Join-Path $SimulatorSource 'src\runtime.cpp') -PathType Leaf)) {
        throw "OpenXR Simulator source not found: $SimulatorSource"
    }
    if (Test-Path -LiteralPath $simulatorDllPath -PathType Leaf) {
        $builtAt = (Get-Item -LiteralPath $simulatorDllPath).LastWriteTimeUtc
        if (-not (Get-SimulatorSources | Where-Object { $_.LastWriteTimeUtc -gt $builtAt })) { return }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $vulkanInclude 'vulkan\vulkan.h') -PathType Leaf)) {
        throw "Vulkan headers not found: $vulkanInclude. Download https://github.com/KhronosGroup/Vulkan-Headers into .deps\Vulkan-Headers."
    }
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installationPath = (& $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1).Trim()
    $developerCommand = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'
    New-Item -ItemType Directory -Path $simulatorObjDir, (Split-Path $simulatorDllPath) -Force | Out-Null
    # The 32-bit start function is exported as _xrNegotiateLoaderRuntimeInterface@8; loaders look up the plain name.
    $command = 'call "' + $developerCommand + '" -arch=x86 -host_arch=x64 >nul & cl.exe /nologo /LD /std:c++17 /EHsc /O2 /MD /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE' +
        ' /I"' + (Join-Path $SimulatorSource 'include') + '" /I"' + (Join-Path $SimulatorSource 'src') + '" /I"' + $vulkanInclude + '" /Fo:"' + $simulatorObjDir + '\\"' +
        ' "' + (Join-Path $SimulatorSource 'src\runtime.cpp') + '" "' + (Join-Path $SimulatorSource 'src\flicker_detector.cpp') + '"' +
        ' /link d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib dwmapi.lib msimg32.lib uxtheme.lib user32.lib gdi32.lib ole32.lib shell32.lib' +
        ' /EXPORT:xrNegotiateLoaderRuntimeInterface=_xrNegotiateLoaderRuntimeInterface@8 /OUT:"' + $simulatorDllPath + '" /IMPLIB:"' + (Join-Path $simulatorObjDir 'openxr_simulator.lib') + '"'
    & cmd.exe /d /c $command | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "32-bit OpenXR Simulator build failed with exit code $LASTEXITCODE." }
    Write-Host "Built 32-bit OpenXR Simulator: $simulatorDllPath"
}

function Write-LayerManifest {
    param([string]$Name, [string]$Library, [string]$Description, [string]$FileName)

    [ordered]@{
        file_format_version = '1.0.0'
        api_layer = [ordered]@{
            name = $Name
            library_path = $Library
            api_version = '1.0'
            implementation_version = '1'
            description = $Description
        }
    } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $testLayerDir $FileName) -Encoding ASCII
}

# ReShade uses this folder instead of the game folder, so it writes nothing there.
function Initialize-TestFiles {
    $required = @($addonPath, $testEffectPath, $gameExePath)
    if (-not $WithoutReShade) { $required += $reshadeDllPath }
    foreach ($path in $required) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file not found: $path" }
    }
    Update-Simulator32

    if (Test-Path -LiteralPath $reshadeBaseDir) { Remove-Item -LiteralPath $reshadeBaseDir -Recurse -Force }
    $shaderDir = Join-Path $reshadeBaseDir 'Shaders'
    New-Item -ItemType Directory -Path $shaderDir -Force | Out-Null
    Copy-Item -LiteralPath $testEffectPath -Destination $shaderDir
    "[INSTALL]`r`nLogging=1`r`n" | Set-Content -LiteralPath (Join-Path $reshadeBaseDir 'ReShade.ini') -Encoding ASCII
    "[GENERAL]`r`nEffectSearchPaths=.\Shaders`r`nPresetPath=.\InvertOn.ini`r`n" |
        Set-Content -LiteralPath (Join-Path $reshadeBaseDir 'ReShadeVR.ini') -Encoding ASCII
    "Techniques=DeterministicInvert@DeterministicInvert.fx`r`nTechniqueSorting=DeterministicInvert@DeterministicInvert.fx`r`n" |
        Set-Content -LiteralPath (Join-Path $reshadeBaseDir 'InvertOn.ini') -Encoding ASCII

    if (Test-Path -LiteralPath $testLayerDir) { Remove-Item -LiteralPath $testLayerDir -Recurse -Force }
    New-Item -ItemType Directory -Path $testLayerDir -Force | Out-Null
    Write-LayerManifest -Name $testLayerName -Library $addonPath -Description 'ReShade VR UI Add-on (BioShock VR test build)' -FileName 'ReShade-OpenXR-UI-Test.json'
    if (-not $WithoutReShade) {
        Write-LayerManifest -Name $testReShadeLayerName -Library $reshadeDllPath -Description 'ReShade 32-bit (BioShock VR test)' -FileName 'ReShade32-Test.json'
    }

    [ordered]@{
        file_format_version = '1.0.0'
        runtime = [ordered]@{ library_path = $simulatorDllPath }
    } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $runtimeManifestPath -Encoding ASCII
}

function Save-Artifacts {
    param($GamePid)

    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $reshadeBaseDir -Filter 'ReShade*.log*' -File -ErrorAction Ignore |
        ForEach-Object { Copy-SharedFile -Path $_.FullName -Destination (Join-Path $outputDirectory $_.Name) }
    Copy-SharedFile -Path $modLogPath -Destination (Join-Path $outputDirectory 'bioshockvr.log')
    if ($null -ne $GamePid) {
        $simulatorLog = Join-Path $simulatorDataDirectory "openxr_simulator.$GamePid.log"
        Copy-SharedFile -Path $simulatorLog -Destination (Join-Path $outputDirectory 'openxr-simulator.log')
    }
}

function Complete-Session {
    param($GamePid)

    if ($null -ne $GamePid) {
        $game = Get-Process -Id $GamePid -ErrorAction Ignore
        if ($null -ne $game -and $game.ProcessName -eq $gameProcessName) {
            # A forced kill makes BioShock offer to reset all its settings at the next start, so close its window first.
            if ($game.CloseMainWindow() -and $game.WaitForExit($TimeoutSeconds * 1000)) { }
            else {
                Write-Host "BioShock did not close within $TimeoutSeconds s; stopping it."
                Stop-Process -Id $GamePid -Force
                $game.WaitForExit($TimeoutSeconds * 1000) | Out-Null
            }
        }
    }
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
    if ($running.Count -gt 0) { throw "Close BioShock first: $(($running | ForEach-Object { $_.Id }) -join ', ')" }
    if (Test-Path -LiteralPath $runStatePath -PathType Leaf) { throw 'A previous test session was not closed. Run test_bioshock_vr.bat stop first.' }
    if (-not (Get-Process -Name 'steam' -ErrorAction Ignore)) { throw 'Start Steam first; BioShock needs it.' }
    # The mod's OpenXR loader ignores XR_RUNTIME_JSON in elevated processes, so the game would use the headset runtime.
    $identity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if ($identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run this test without administrator rights.' }
    Remove-Item -LiteralPath $runningMarkerPath -Force -ErrorAction Ignore

    New-Item -ItemType Directory -Path $workDir -Force | Out-Null
    if (Test-Path -LiteralPath $outputDirectory) { Remove-Item -LiteralPath $outputDirectory -Recurse -Force }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    Initialize-TestFiles

    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $gameExePath
    $start.WorkingDirectory = $GameDir
    $start.UseShellExecute = $false
    # Starting the game directly with its Steam app id stops Steam from relaunching it, so these settings stay in it.
    $start.EnvironmentVariables['SteamAppId'] = $steamAppId
    $start.EnvironmentVariables['SteamGameId'] = $steamAppId
    $start.EnvironmentVariables['RESHADE_BASE_PATH_OVERRIDE'] = $reshadeBaseDir
    $start.EnvironmentVariables['DISABLE_XR_APILAYER_reshade_vr_ui'] = '1'
    $start.EnvironmentVariables['DISABLE_XR_APILAYER_reshade_1'] = '1'
    $start.EnvironmentVariables['XR_API_LAYER_PATH'] = $testLayerDir
    $start.EnvironmentVariables['XR_ENABLE_API_LAYERS'] = if ($WithoutReShade) { $testLayerName } else { "$testReShadeLayerName;$testLayerName" }
    if ($UseActiveRuntime) {
        $script:CaptureFrame = 0
    }
    else {
        $start.EnvironmentVariables['XR_RUNTIME_JSON'] = $runtimeManifestPath
        New-Item -ItemType Directory -Path $simulatorDataDirectory -Force | Out-Null
        if (Test-Path -LiteralPath $simulatorSettingsPath -PathType Leaf) {
            Copy-Item -LiteralPath $simulatorSettingsPath -Destination $settingsBackupPath -Force
        }
        Set-SimulatorCaptureWindow
        Remove-Item -LiteralPath $screenshotRequestPath, $screenshotPath, $screenshotStatusPath -Force -ErrorAction Ignore
    }

    $script:modLogStartLength = (Read-SharedText -Path $modLogPath).Length
    $startedAt = [DateTime]::UtcNow
    $game = $null
    $succeeded = $false
    try {
        $game = [System.Diagnostics.Process]::Start($start)
        Write-Host "Started BioShock (pid $($game.Id))."
        [ordered]@{ GamePid = $game.Id; StartedAt = $startedAt.ToString('o') } |
            ConvertTo-Json | Set-Content -LiteralPath $runStatePath -Encoding ASCII

        if (-not $UseActiveRuntime) {
            Wait-Until -Game $game -Description '32-bit OpenXR Simulator in BioShock' -Condition {
                $null -ne (Get-ModulePath -Process $game -ModuleName 'openxr_simulator.dll')
            }
        }
        Wait-Until -Game $game -Description 'the 32-bit add-on in BioShock' -Condition {
            $null -ne (Get-ModulePath -Process $game -ModuleName $addonName)
        }
        Write-Host "Add-on loaded: $(Get-ModulePath -Process $game -ModuleName $addonName)"
        if (-not $WithoutReShade) {
            Wait-Until -Game $game -Description 'ReShade32.dll in BioShock' -Condition {
                $null -ne (Get-ModulePath -Process $game -ModuleName 'ReShade32.dll')
            }
            Write-Host 'ReShade32 loaded.'
        }

        if ($EnterGame) {
            Enter-GameWorld -Game $game
            $settleUntil = [DateTime]::UtcNow.AddSeconds(5)
            while ([DateTime]::UtcNow -lt $settleUntil) { Set-GameForeground -Game $game; Start-Sleep -Milliseconds 500 }
        }

        if ($CaptureFrame -gt 0) {
            Wait-Until -Game $game -Description "simulator frame $CaptureFrame" -Condition {
                if ($EnterGame) { Set-GameForeground -Game $game }
                (Get-SimulatorFrame -Since $startedAt) -ge $CaptureFrame
            }
            $requestedAt = [DateTime]::UtcNow
            [ordered]@{
                timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() / 1000.0
                eye = 'both'
                layer = $CaptureLayer
                requested_by = 'bioshock-vr-test'
            } | ConvertTo-Json | Set-Content -LiteralPath $screenshotRequestPath -Encoding ASCII
            Wait-Until -Game $game -Description 'simulator screenshot' -Condition {
                if ($EnterGame) { Set-GameForeground -Game $game }
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
            Write-Host "Left running: BioShock (pid $gamePid). Close it with test_bioshock_vr.bat stop."
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
    Write-Host "Game:      $(Test-Path -LiteralPath $gameExePath -PathType Leaf)  $gameExePath"
    Write-Host "Add-on:    $(Test-Path -LiteralPath $addonPath -PathType Leaf)  $addonPath"
    Write-Host "ReShade:   $(Test-Path -LiteralPath $reshadeDllPath -PathType Leaf)  $reshadeDllPath"
    Write-Host "Simulator: $(Test-Path -LiteralPath $simulatorDllPath -PathType Leaf)  $simulatorDllPath"
    Get-RunningGames | ForEach-Object { Write-Host "Running: $($_.ProcessName) ($($_.Id))" }
    if (Test-Path -LiteralPath $runStatePath -PathType Leaf) { Write-Host "Open test session: $runStatePath (close it with test_bioshock_vr.bat stop)" }
}

switch ($Action) {
    'Run' { Invoke-Run }
    'Stop' { Invoke-Stop }
    'Status' { Show-Status }
    'BuildSimulator' { Update-Simulator32 }
}
