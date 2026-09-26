[CmdletBinding()]
param(
    [ValidateSet('Deploy', 'Run', 'Stop', 'Status')]
    [string]$Action = 'Status',

    [string]$UevrTestDir = 'A:\UEVR-build\uevr-reshade-testing',

    [string]$UevrBuildDir = '.deps\UEVR\build\bin\uevr',

    [string]$GameExe = 'A:\SteamLibrary\steamapps\common\Ava\CreaturesOfAva\Binaries\Win64\CreaturesOfAva-Win64-Shipping.exe',

    [string]$GameArguments = '',

    [string]$SteamAppId = '2304440',

    [string]$SimulatorRoot = 'A:\ws\OpenXR-Simulator-test',

    # Enables the official ReShade layer and loads the add-on from $AddonBuildDir; by default both are
    # disabled in the game process only. The installed add-on layer is always disabled.
    [switch]$EnableReShadeLayers,

    # With -EnableReShadeLayers: points ReShade at a repo-owned base folder with the invert effect on in VR.
    # Leave off when ReShade is installed in the game folder, so it keeps its own settings.
    [switch]$UseTestReShadeBase,

    [string]$AddonBuildDir = 'build\reshade-openxr-ui',

    # Leaves the game and injector running after a successful capture; close them later with -Action Stop.
    [switch]$KeepRunning,

    [ValidateRange(1, 100000)]
    [int]$CaptureFrame = 300,

    # Limit for each wait step; on timeout every process this run started is killed.
    [ValidateRange(5, 600)]
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not [System.IO.Path]::IsPathRooted($UevrBuildDir)) {
    $UevrBuildDir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $UevrBuildDir))
}
if (-not [System.IO.Path]::IsPathRooted($AddonBuildDir)) {
    $AddonBuildDir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $AddonBuildDir))
}
$addonPath = Join-Path $AddonBuildDir 'ReShade-OpenXR-UI.addon64'
$reshadeBaseDir = Join-Path $repoRoot 'build\uevr-simulator\reshade-base'
$testLayerDir = Join-Path $repoRoot 'build\uevr-simulator\layers'
$testLayerName = 'XR_APILAYER_reshade_vr_ui_uevr_test'
$testEffectPath = Join-Path $PSScriptRoot 'openxr\DeterministicInvert.fx'
$gameProcessName = [System.IO.Path]::GetFileNameWithoutExtension($GameExe)
$injectorPath = Join-Path $UevrTestDir 'UEVRInjector.exe'
$simulatorDllPath = Join-Path $SimulatorRoot 'openxr_simulator.dll'
# The bundle's manifest uses a bare DLL name, which older loaders (UEVR ships 1.0.22) search for
# on the system path instead of next to the manifest, so a manifest with an absolute path is generated.
$runtimeManifestPath = Join-Path $repoRoot 'build\uevr-simulator\openxr_simulator.json'
$simulatorDataDirectory = Join-Path $env:LOCALAPPDATA 'OpenXR-Simulator'
$simulatorSettingsPath = Join-Path $simulatorDataDirectory 'settings.json'
$runtimeStatusPath = Join-Path $simulatorDataDirectory 'runtime_status.json'
$screenshotRequestPath = Join-Path $simulatorDataDirectory 'screenshot_request.json'
$screenshotPath = Join-Path $simulatorDataDirectory 'screenshot.bmp'
$screenshotStatusPath = Join-Path $simulatorDataDirectory 'screenshot_status.json'
$uevrGameLogPath = Join-Path $env:APPDATA "UnrealVRMod\$gameProcessName\log.txt"
$outputDirectory = Join-Path $repoRoot "build\uevr-simulator\$gameProcessName"
$runStatePath = Join-Path $repoRoot 'build\uevr-simulator\run-state.json'
$settingsBackupPath = Join-Path $repoRoot 'build\uevr-simulator\simulator-settings.backup.json'
$deployFiles = @('UEVRBackend.dll', 'UEVRBackend.pdb', 'openvr_api.dll', 'LuaVR.dll', 'UEVRPluginNullifier.dll')

function Get-RunningTargets {
    return @(Get-Process | Where-Object { $_.ProcessName -eq $gameProcessName -or $_.ProcessName -eq 'UEVRInjector' })
}

function Assert-NothingRunning {
    $running = Get-RunningTargets
    if ($running.Count -gt 0) {
        $list = ($running | ForEach-Object { "$($_.ProcessName) ($($_.Id))" }) -join ', '
        throw "Close these processes first: $list"
    }
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

function Invoke-Deploy {
    Assert-NothingRunning
    if (-not (Test-Path -LiteralPath (Join-Path $UevrBuildDir 'UEVRBackend.dll') -PathType Leaf)) {
        throw "UEVRBackend.dll not found in '$UevrBuildDir'. Build .deps\UEVR first."
    }
    if (-not (Test-Path -LiteralPath $injectorPath -PathType Leaf)) {
        throw "UEVRInjector.exe not found in '$UevrTestDir'."
    }

    foreach ($name in $deployFiles) {
        $source = Join-Path $UevrBuildDir $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
        Copy-Item -LiteralPath $source -Destination (Join-Path $UevrTestDir $name) -Force
        Write-Host "Copied $name  $((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash)"
    }
}

function Show-Status {
    foreach ($name in $deployFiles) {
        $source = Join-Path $UevrBuildDir $name
        $target = Join-Path $UevrTestDir $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
        $same = (Test-Path -LiteralPath $target -PathType Leaf) -and
            (Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath $target).Hash
        Write-Host "Deployed ${name}: $same"
    }
    Write-Host "Injector:  $(Test-Path -LiteralPath $injectorPath -PathType Leaf)  $injectorPath"
    Write-Host "Game:      $(Test-Path -LiteralPath $GameExe -PathType Leaf)  $GameExe"
    Write-Host "Simulator: $(Test-Path -LiteralPath $simulatorDllPath -PathType Leaf)  $simulatorDllPath"
    Get-RunningTargets | ForEach-Object { Write-Host "Running: $($_.ProcessName) ($($_.Id))" }
    if (Test-Path -LiteralPath $runStatePath -PathType Leaf) {
        Write-Host "Open test session: $runStatePath (close it with -Action Stop)"
    }
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

function Get-SimulatorFrame {
    param([Parameter(Mandatory = $true)][DateTime]$Since)

    if (-not (Test-Path -LiteralPath $runtimeStatusPath -PathType Leaf) -or
        (Get-Item -LiteralPath $runtimeStatusPath).LastWriteTimeUtc -lt $Since) {
        return 0
    }
    try { return [int](Get-Content -LiteralPath $runtimeStatusPath -Raw | ConvertFrom-Json).frame_count }
    catch { return 0 }
}

function Wait-Until {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Condition,
        [Parameter(Mandatory = $true)][string]$Description,
        [Parameter(Mandatory = $true)]$Game
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Game.HasExited) {
            $relaunched = @(Get-Process -Name $gameProcessName -ErrorAction Ignore)
            if ($relaunched.Count -gt 0) {
                throw "The game exited and was relaunched (pid $($relaunched[0].Id)), probably through Steam. The simulator environment is lost."
            }
            throw "The game exited (code $($Game.ExitCode)) while waiting for: $Description"
        }
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 250
    }
    throw "Timed out after $TimeoutSeconds s waiting for: $Description"
}

function Stop-TestProcess {
    param($Process, [int]$GraceSeconds)

    if ($null -eq $Process) { return }
    try {
        if ($Process.HasExited) { return }
        $name = "$($Process.ProcessName) ($($Process.Id))"
        if ($GraceSeconds -gt 0) {
            $null = $Process.CloseMainWindow()
            if ($Process.WaitForExit($GraceSeconds * 1000)) {
                Write-Host "$name closed normally."
                return
            }
        }
        $Process.Kill()
        $null = $Process.WaitForExit(10000)
        Write-Host "$name was killed."
    }
    catch {
        Write-Warning "Could not stop process $($Process.Id): $($_.Exception.Message)"
    }
}

function Write-RuntimeManifest {
    New-Item -ItemType Directory -Path (Split-Path $runtimeManifestPath -Parent) -Force | Out-Null
    [ordered]@{
        file_format_version = '1.0.0'
        runtime = [ordered]@{ library_path = $simulatorDllPath }
    } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $runtimeManifestPath -Encoding ASCII
}

# ReShade loaded as a layer only starts when ReShade.ini exists in its base folder, which
# RESHADE_BASE_PATH_OVERRIDE moves here so the game folder stays untouched.
function Initialize-ReShadeTestBase {
    if (-not (Test-Path -LiteralPath $testEffectPath -PathType Leaf)) { throw "Required file not found: $testEffectPath" }
    if (Test-Path -LiteralPath $reshadeBaseDir) { Remove-Item -LiteralPath $reshadeBaseDir -Recurse -Force }
    $shaderDir = Join-Path $reshadeBaseDir 'Shaders'
    New-Item -ItemType Directory -Path $shaderDir -Force | Out-Null
    Copy-Item -LiteralPath $testEffectPath -Destination $shaderDir

    "[INSTALL]`r`nLogging=1`r`n" | Set-Content -LiteralPath (Join-Path $reshadeBaseDir 'ReShade.ini') -Encoding ASCII
    "[GENERAL]`r`nEffectSearchPaths=.\Shaders`r`nPresetPath=.\InvertOn.ini`r`n" |
        Set-Content -LiteralPath (Join-Path $reshadeBaseDir 'ReShadeVR.ini') -Encoding ASCII
    "Techniques=DeterministicInvert@DeterministicInvert.fx`r`nTechniqueSorting=DeterministicInvert@DeterministicInvert.fx`r`n" |
        Set-Content -LiteralPath (Join-Path $reshadeBaseDir 'InvertOn.ini') -Encoding ASCII
    Write-Host "ReShade base folder: $reshadeBaseDir"
}

function Initialize-AddonTestLayer {
    if (-not (Test-Path -LiteralPath $addonPath -PathType Leaf)) { throw "Required file not found: $addonPath" }
    New-Item -ItemType Directory -Path $testLayerDir -Force | Out-Null
    [ordered]@{
        file_format_version = '1.0.0'
        api_layer = [ordered]@{
            name = $testLayerName
            library_path = $addonPath
            api_version = '1.0'
            implementation_version = '1'
            description = 'ReShade VR UI Add-on (UEVR test build)'
        }
    } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $testLayerDir 'ReShade-OpenXR-UI-Test.json') -Encoding ASCII
    Write-Host "Add-on under test: $addonPath"
}

# The injector remembers its runtime choice per install location and defaults to OpenVR.
function Select-InjectorOpenXr {
    param([Parameter(Mandatory = $true)]$Injector)

    Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $radio = $null
    while ($null -eq $radio -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 250
        $Injector.Refresh()
        if ($Injector.MainWindowHandle -eq [IntPtr]::Zero) { continue }
        $window = [System.Windows.Automation.AutomationElement]::FromHandle($Injector.MainWindowHandle)
        $condition = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'm_openxrRadio')
        $radio = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
    }
    if ($null -eq $radio) { throw 'The OpenXR option was not found in the UEVRInjector window.' }

    $selection = $radio.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
    if (-not $selection.Current.IsSelected) { $selection.Select() }
    if (-not $selection.Current.IsSelected) { throw 'Could not select OpenXR in the UEVRInjector window.' }
    Write-Host 'UEVRInjector runtime: OpenXR.'
}

function Invoke-Run {
    Assert-NothingRunning
    if (Test-Path -LiteralPath $runStatePath -PathType Leaf) {
        throw "A previous test session was not closed. Run this script with -Action Stop first."
    }
    foreach ($path in @($GameExe, $injectorPath, $simulatorDllPath, (Join-Path $UevrTestDir 'openxr_loader.dll'))) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file not found: $path" }
    }
    if (-not (Get-Process -Name 'steam' -ErrorAction Ignore)) {
        throw 'Steam is not running. Start Steam first.'
    }
    # UEVR appends to one log per game; starting fresh keeps each run's log readable.
    if (Test-Path -LiteralPath $uevrGameLogPath -PathType Leaf) {
        Remove-Item -LiteralPath $uevrGameLogPath -Force
        Write-Host 'Deleted the previous UEVR log for this game.'
    }

    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $simulatorDataDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $outputDirectory -File | Remove-Item -Force
    if (Test-Path -LiteralPath $simulatorSettingsPath -PathType Leaf) {
        Copy-Item -LiteralPath $simulatorSettingsPath -Destination $settingsBackupPath -Force
    }

    $game = $null
    $injector = $null
    $succeeded = $false
    $startedAt = [DateTime]::Now
    try {
        Write-RuntimeManifest
        Set-SimulatorCaptureWindow
        Remove-Item -LiteralPath $screenshotRequestPath, $screenshotPath, $screenshotStatusPath -Force -ErrorAction Ignore

        $gameStart = New-Object System.Diagnostics.ProcessStartInfo
        $gameStart.FileName = $GameExe
        $gameStart.Arguments = $GameArguments
        $gameStart.WorkingDirectory = Split-Path $GameExe -Parent
        $gameStart.UseShellExecute = $false
        $gameStart.EnvironmentVariables['XR_RUNTIME_JSON'] = $runtimeManifestPath
        # Steam API skips its restart-through-Steam path when these are set, which avoids the
        # "custom arguments" prompt and keeps this environment in the game process.
        $gameStart.EnvironmentVariables['SteamAppId'] = $SteamAppId
        $gameStart.EnvironmentVariables['SteamGameId'] = $SteamAppId
        # The installed add-on layer is always off; -EnableReShadeLayers loads the build under test instead.
        $gameStart.EnvironmentVariables['DISABLE_XR_APILAYER_reshade_vr_ui'] = '1'
        if ($EnableReShadeLayers) {
            Initialize-AddonTestLayer
            if ($UseTestReShadeBase) {
                Initialize-ReShadeTestBase
                $gameStart.EnvironmentVariables['RESHADE_BASE_PATH_OVERRIDE'] = $reshadeBaseDir
            }
            $gameStart.EnvironmentVariables['XR_API_LAYER_PATH'] = $testLayerDir
            $gameStart.EnvironmentVariables['XR_ENABLE_API_LAYERS'] = $testLayerName
        }
        else {
            $gameStart.EnvironmentVariables['DISABLE_XR_APILAYER_reshade_1'] = '1'
        }

        $injectorStart = New-Object System.Diagnostics.ProcessStartInfo
        $injectorStart.FileName = $injectorPath
        $injectorStart.Arguments = "--attach=$gameProcessName.exe"
        $injectorStart.WorkingDirectory = $UevrTestDir
        $injectorStart.UseShellExecute = $false
        $injector = [System.Diagnostics.Process]::Start($injectorStart)
        Write-Host "Started UEVRInjector (pid $($injector.Id)) waiting for $gameProcessName.exe."
        Select-InjectorOpenXr -Injector $injector

        $game = [System.Diagnostics.Process]::Start($gameStart)
        Write-Host "Started $gameProcessName (pid $($game.Id))."
        [ordered]@{
            GamePid = $game.Id
            InjectorPid = $injector.Id
            StartedAt = $startedAt.ToString('o')
        } | ConvertTo-Json | Set-Content -LiteralPath $runStatePath -Encoding ASCII

        Wait-Until -Game $game -Description 'UEVRBackend.dll in the game' -Condition {
            $null -ne (Get-ModulePath -Process $game -ModuleName 'UEVRBackend.dll')
        }
        $loaderPath = Get-ModulePath -Process $game -ModuleName 'openxr_loader.dll'
        $backendPath = Get-ModulePath -Process $game -ModuleName 'UEVRBackend.dll'
        Write-Host "UEVR backend: $backendPath"
        Write-Host "OpenXR loader: $loaderPath"
        if (-not $loaderPath) {
            $openVrPath = Get-ModulePath -Process $game -ModuleName 'openvr_api.dll'
            throw "UEVR was injected without OpenXR (openvr_api.dll: $openVrPath). Select OpenXR once in this UEVRInjector.exe window."
        }

        Wait-Until -Game $game -Description 'openxr_simulator.dll in the game' -Condition {
            $null -ne (Get-ModulePath -Process $game -ModuleName 'openxr_simulator.dll')
        }
        Write-Host 'OpenXR Simulator runtime loaded in the game.'

        Wait-Until -Game $game -Description "simulator frame $CaptureFrame" -Condition {
            (Get-SimulatorFrame -Since $startedAt.ToUniversalTime()) -ge $CaptureFrame
        }
        $requestedAt = [DateTime]::UtcNow
        [ordered]@{
            timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() / 1000.0
            eye = 'both'
            layer = 'projection'
            requested_by = 'uevr-simulator-test'
        } | ConvertTo-Json | Set-Content -LiteralPath $screenshotRequestPath -Encoding ASCII
        Wait-Until -Game $game -Description 'simulator screenshot' -Condition {
            (Test-Path -LiteralPath $screenshotStatusPath -PathType Leaf) -and
            (Test-Path -LiteralPath $screenshotPath -PathType Leaf) -and
            (Get-Item -LiteralPath $screenshotStatusPath).LastWriteTimeUtc -ge $requestedAt
        }
        Start-Sleep -Milliseconds 500

        Add-Type -AssemblyName System.Drawing
        $bitmap = [System.Drawing.Bitmap]::FromFile($screenshotPath)
        try {
            $bitmap.Save((Join-Path $outputDirectory 'headset.png'), [System.Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            $bitmap.Dispose()
        }
        Copy-Item -LiteralPath $screenshotStatusPath -Destination $outputDirectory -Force
        Write-Host "Saved headset capture: $(Join-Path $outputDirectory 'headset.png')"
        $succeeded = $true
    }
    finally {
        if ($succeeded -and $KeepRunning) {
            Write-Host "Left running: $gameProcessName (pid $($game.Id)), UEVRInjector (pid $($injector.Id)). Close with -Action Stop."
        }
        else {
            Complete-Session -Game $game -Injector $injector -StartedAt $startedAt -Graceful $succeeded
        }
    }
}

function Complete-Session {
    param($Game, $Injector, [DateTime]$StartedAt, [bool]$Graceful)

    $grace = if ($Graceful) { 30 } else { 0 }
    $gamePid = if ($null -ne $Game) { $Game.Id } else { $null }
    Stop-TestProcess -Process $Game -GraceSeconds $grace
    # A Steam relaunch creates a second game process that this run also owns.
    Get-Process -Name $gameProcessName -ErrorAction Ignore |
        Where-Object { try { $_.StartTime -ge $StartedAt } catch { $true } } |
        ForEach-Object { Stop-TestProcess -Process $_ -GraceSeconds $grace }
    Stop-TestProcess -Process $Injector -GraceSeconds $(if ($Graceful) { 10 } else { 0 })

    if (Test-Path -LiteralPath $settingsBackupPath -PathType Leaf) {
        Copy-Item -LiteralPath $settingsBackupPath -Destination $simulatorSettingsPath -Force
        Remove-Item -LiteralPath $settingsBackupPath -Force
    }
    if (Test-Path -LiteralPath $uevrGameLogPath -PathType Leaf) {
        Copy-Item -LiteralPath $uevrGameLogPath -Destination (Join-Path $outputDirectory 'uevr-log.txt') -Force
    }
    if ($UseTestReShadeBase -and (Test-Path -LiteralPath $reshadeBaseDir)) {
        Get-ChildItem -LiteralPath $reshadeBaseDir -Filter 'ReShade*.log*' -File |
            Copy-Item -Destination $outputDirectory -Force
    }
    if ($null -ne $gamePid) {
        $simulatorLog = Join-Path $simulatorDataDirectory "openxr_simulator.$gamePid.log"
        if (Test-Path -LiteralPath $simulatorLog -PathType Leaf) {
            Copy-Item -LiteralPath $simulatorLog -Destination (Join-Path $outputDirectory 'openxr-simulator.log') -Force
        }
    }
    Remove-Item -LiteralPath $runStatePath -Force -ErrorAction Ignore
}

function Invoke-Stop {
    if (-not (Test-Path -LiteralPath $runStatePath -PathType Leaf)) {
        throw 'No open test session was recorded.'
    }
    $state = Get-Content -LiteralPath $runStatePath -Raw | ConvertFrom-Json
    $game = Get-Process -Id $state.GamePid -ErrorAction Ignore | Where-Object ProcessName -eq $gameProcessName
    $injector = Get-Process -Id $state.InjectorPid -ErrorAction Ignore | Where-Object ProcessName -eq 'UEVRInjector'
    Complete-Session -Game $game -Injector $injector -StartedAt ([DateTime]::Parse($state.StartedAt)) -Graceful $true
}

switch ($Action) {
    'Deploy' { Invoke-Deploy }
    'Run' { Invoke-Run }
    'Stop' { Invoke-Stop }
    'Status' { Show-Status }
}
