[CmdletBinding()]
param(
    [ValidateSet('Install', 'Run', 'Capture', 'Status', 'Uninstall')]
    [string]$Action = 'Status',

    [string]$TestRoot = '.deps\openxr-simulator-test',

    [string]$ReShadePath = '.deps\reshade',

    [string]$SourceDll,

    [ValidateSet('D3D12', 'D3D11')]
    [string]$GraphicsApi = 'D3D12',

    [ValidateRange(1, 10000)]
    [int]$Frames = 30
)

$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not [System.IO.Path]::IsPathRooted($TestRoot)) {
    $TestRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $TestRoot))
}
if (-not [System.IO.Path]::IsPathRooted($ReShadePath)) {
    $ReShadePath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $ReShadePath))
}
$sourceDll = if ($SourceDll) {
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SourceDll)
}
else {
    $null
}
$sourceEffect = Join-Path $PSScriptRoot 'openxr\DeterministicInvert.fx'
$probeName = if ($GraphicsApi -eq 'D3D11') { 'xr_probe_d3d11.exe' } else { 'xr_probe.exe' }
$probePath = Join-Path $TestRoot $probeName
$loaderPath = Join-Path $TestRoot 'openxr_loader.dll'
$runtimeManifestPath = Join-Path $TestRoot 'openxr_simulator.json'
$proxyPath = Join-Path $TestRoot 'dxgi.dll'
$configPath = Join-Path $TestRoot 'ReShade.ini'
$vrConfigPath = Join-Path $TestRoot 'ReShadeVR.ini'
$logPath = Join-Path $TestRoot 'ReShade.log'
$effectDirectory = Join-Path $TestRoot 'reshade-shaders\Shaders'
$effectPath = Join-Path $effectDirectory 'DeterministicInvert.fx'
$offPresetPath = Join-Path $TestRoot 'OpenXR-Effect-Off.ini'
$onPresetPath = Join-Path $TestRoot 'OpenXR-Effect-On.ini'
$captureDirectory = if ($GraphicsApi -eq 'D3D11') {
    Join-Path $TestRoot 'captures\d3d11'
}
else {
    Join-Path $TestRoot 'captures'
}
$simulatorDataDirectory = Join-Path $env:LOCALAPPDATA 'OpenXR-Simulator'
$simulatorSettingsPath = Join-Path $simulatorDataDirectory 'settings.json'
$screenshotRequestPath = Join-Path $simulatorDataDirectory 'screenshot_request.json'
$screenshotPath = Join-Path $simulatorDataDirectory 'screenshot.bmp'
$screenshotStatusPath = Join-Path $simulatorDataDirectory 'screenshot_status.json'
$layerDirectory = Join-Path $TestRoot 'layers'
$layerManifestPath = Join-Path $layerDirectory 'ReShade64_XR.json'
$statePath = Join-Path $TestRoot 'reshade-test-state.json'

function Get-FileHashValue {
    param([Parameter(Mandatory = $true)][string]$Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Read-State {
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        return $null
    }

    return Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
}

function Assert-ManagedFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ExpectedHash
    )

    if ((Test-Path -LiteralPath $Path -PathType Leaf) -and (Get-FileHashValue $Path) -ne $ExpectedHash) {
        throw "Refusing to modify '$Path' because it differs from the file recorded by this test harness."
    }
}

function Assert-TestBundle {
    if (-not $sourceDll) {
        throw 'Pass -SourceDll to a separately built ReShade64.dll. This harness does not use the standalone OpenXR UI add-on DLL.'
    }
    foreach ($path in @($sourceDll, $sourceEffect, $probePath, $loaderPath, $runtimeManifestPath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required test artifact not found: $path"
        }
    }
}

function Set-VrConfig {
    param([Parameter(Mandatory = $true)][string]$PresetPath)

    $presetName = Split-Path $PresetPath -Leaf
    @"
[GENERAL]
EffectSearchPaths=.\reshade-shaders\Shaders
PresetPath=.\$presetName
TextureSearchPaths=.\reshade-shaders\Textures

[INPUT]
KeyOverlay=0,0,0,0
"@ | Set-Content -LiteralPath $vrConfigPath -Encoding ASCII
}

function Install-TestFiles {
    Assert-TestBundle

    $state = Read-State
    if ($state) {
        Assert-ManagedFile -Path $proxyPath -ExpectedHash $state.ProxyHash
        Assert-ManagedFile -Path $layerManifestPath -ExpectedHash $state.LayerManifestHash
        if ($state.Version -ge 2) {
            Assert-ManagedFile -Path $effectPath -ExpectedHash $state.EffectHash
            Assert-ManagedFile -Path $offPresetPath -ExpectedHash $state.OffPresetHash
            Assert-ManagedFile -Path $onPresetPath -ExpectedHash $state.OnPresetHash
            Assert-ManagedFile -Path $vrConfigPath -ExpectedHash $state.VrConfigHash
        }
    }
    elseif ((Test-Path -LiteralPath $proxyPath) -or
        (Test-Path -LiteralPath $layerManifestPath) -or
        (Test-Path -LiteralPath $effectPath) -or
        (Test-Path -LiteralPath $offPresetPath) -or
        (Test-Path -LiteralPath $onPresetPath) -or
        (Test-Path -LiteralPath $vrConfigPath)) {
        throw 'Refusing to overwrite existing ReShade test files that are not tracked by this test harness.'
    }

    if ($state) {
        $configCreated = [bool]$state.ConfigCreated
        $vrConfigExisted = [bool]$state.VrConfigExisted
        $logExisted = [bool]$state.LogExisted
    }
    else {
        $configCreated = -not (Test-Path -LiteralPath $configPath -PathType Leaf)
        $vrConfigExisted = Test-Path -LiteralPath $vrConfigPath -PathType Leaf
        $logExisted = Test-Path -LiteralPath $logPath -PathType Leaf
    }

    New-Item -ItemType Directory -Path $layerDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $effectDirectory -Force | Out-Null
    Copy-Item -LiteralPath $sourceDll -Destination $proxyPath -Force
    Copy-Item -LiteralPath $sourceEffect -Destination $effectPath -Force

    $manifest = [ordered]@{
        file_format_version = '1.0.0'
        api_layer = [ordered]@{
            name = 'XR_APILAYER_reshade'
            library_path = '..\dxgi.dll'
            api_version = '1.0'
            implementation_version = '2'
            description = "crosire's ReShade post-processing injector for 64-bit"
            disable_environment = 'DISABLE_XR_APILAYER_reshade_1'
        }
    }
    $manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $layerManifestPath -Encoding ASCII

    "Techniques=`r`nTechniqueSorting=DeterministicInvert@DeterministicInvert.fx`r`n" |
        Set-Content -LiteralPath $offPresetPath -Encoding ASCII
    "Techniques=DeterministicInvert@DeterministicInvert.fx`r`nTechniqueSorting=DeterministicInvert@DeterministicInvert.fx`r`n" |
        Set-Content -LiteralPath $onPresetPath -Encoding ASCII
    Set-VrConfig -PresetPath $offPresetPath

    if ($configCreated) {
        "[INSTALL]`r`nLogging=1`r`n" | Set-Content -LiteralPath $configPath -Encoding ASCII
    }

    $newState = [ordered]@{
        Version = 2
        TestRoot = $TestRoot
        ProxyHash = Get-FileHashValue $proxyPath
        LayerManifestHash = Get-FileHashValue $layerManifestPath
        EffectHash = Get-FileHashValue $effectPath
        OffPresetHash = Get-FileHashValue $offPresetPath
        OnPresetHash = Get-FileHashValue $onPresetPath
        VrConfigHash = Get-FileHashValue $vrConfigPath
        ConfigCreated = $configCreated
        ConfigHash = if ($configCreated) { Get-FileHashValue $configPath } else { $null }
        VrConfigExisted = $vrConfigExisted
        LogExisted = $logExisted
    }
    $newState | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding ASCII

    Write-Host "Installed ReShade proxy: $proxyPath"
    Write-Host "Staged process-scoped layer manifest: $layerManifestPath"
}

function Show-Status {
    Write-Host "Graphics API:     $GraphicsApi"
    Write-Host "Probe:            $((Test-Path -LiteralPath $probePath -PathType Leaf))  $probePath"
    Write-Host "Simulator:        $((Test-Path -LiteralPath $runtimeManifestPath -PathType Leaf))  $runtimeManifestPath"
    Write-Host "ReShade proxy:    $((Test-Path -LiteralPath $proxyPath -PathType Leaf))  $proxyPath"
    Write-Host "Layer manifest:   $((Test-Path -LiteralPath $layerManifestPath -PathType Leaf))  $layerManifestPath"
    Write-Host "ReShade config:   $((Test-Path -LiteralPath $configPath -PathType Leaf))  $configPath"
    Write-Host "VR config:        $((Test-Path -LiteralPath $vrConfigPath -PathType Leaf))  $vrConfigPath"
    Write-Host "ReShade log:      $((Test-Path -LiteralPath $logPath -PathType Leaf))  $logPath"
    Write-Host "Test effect:      $((Test-Path -LiteralPath $effectPath -PathType Leaf))  $effectPath"
    Write-Host "Effect-off shot:  $((Test-Path -LiteralPath (Join-Path $captureDirectory 'effect-off.bmp') -PathType Leaf))"
    Write-Host "Effect-on shot:   $((Test-Path -LiteralPath (Join-Path $captureDirectory 'effect-on.bmp') -PathType Leaf))"
    Write-Host "Managed state:    $($null -ne (Read-State))  $statePath"
}

function Invoke-Probe {
    Assert-TestBundle
    $state = Read-State
    if (-not $state) {
        throw 'ReShade test files are not installed. Run this script with -Action Install first.'
    }

    Assert-ManagedFile -Path $proxyPath -ExpectedHash $state.ProxyHash
    Assert-ManagedFile -Path $layerManifestPath -ExpectedHash $state.LayerManifestHash

    $savedRuntime = $env:XR_RUNTIME_JSON
    $savedLayerPath = $env:XR_API_LAYER_PATH
    $savedEnabledLayers = $env:XR_ENABLE_API_LAYERS
    $savedFrames = $env:XR_PROBE_FRAMES
    try {
        $env:XR_RUNTIME_JSON = $runtimeManifestPath
        $env:XR_API_LAYER_PATH = $layerDirectory
        $env:XR_ENABLE_API_LAYERS = 'XR_APILAYER_reshade'
        $env:XR_PROBE_FRAMES = $Frames.ToString()

        Push-Location $TestRoot
        try {
            & $probePath
            if ($LASTEXITCODE -ne 0) {
                throw "OpenXR probe failed with exit code $LASTEXITCODE"
            }
        }
        finally {
            Pop-Location
        }
    }
    finally {
        $env:XR_RUNTIME_JSON = $savedRuntime
        $env:XR_API_LAYER_PATH = $savedLayerPath
        $env:XR_ENABLE_API_LAYERS = $savedEnabledLayers
        $env:XR_PROBE_FRAMES = $savedFrames
        if (($null -ne $state) -and $state.Version -ge 2 -and
            (Test-Path -LiteralPath $vrConfigPath) -and
            (Test-Path -LiteralPath $effectPath) -and
            (Test-Path -LiteralPath $offPresetPath) -and
            (Test-Path -LiteralPath $onPresetPath)) {
            Set-VrConfig -PresetPath $offPresetPath
            Update-ManagedHashes -State (Read-State)
        }
    }
}

function Update-ManagedHashes {
    param([Parameter(Mandatory = $true)]$State)

    $State.EffectHash = Get-FileHashValue $effectPath
    $State.OffPresetHash = Get-FileHashValue $offPresetPath
    $State.OnPresetHash = Get-FileHashValue $onPresetPath
    $State.VrConfigHash = Get-FileHashValue $vrConfigPath
    $State | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding ASCII
}

function Set-SimulatorCaptureWindow {
    if (Test-Path -LiteralPath $simulatorSettingsPath -PathType Leaf) {
        $settings = Get-Content -LiteralPath $simulatorSettingsPath -Raw | ConvertFrom-Json
    }
    else {
        $settings = [pscustomobject]@{}
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

function Invoke-CaptureProbe {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('off', 'on')][string]$Mode,
        [Parameter(Mandatory = $true)][string]$PresetPath,
        [Parameter(Mandatory = $true)][string]$OutputPath
    )

    $state = Read-State
    if (-not $state -or $state.Version -lt 2) {
        throw 'The visual test files are not installed. Run this script with -Action Install first.'
    }

    Assert-ManagedFile -Path $proxyPath -ExpectedHash $state.ProxyHash
    Assert-ManagedFile -Path $layerManifestPath -ExpectedHash $state.LayerManifestHash
    Assert-ManagedFile -Path $effectPath -ExpectedHash $state.EffectHash
    Assert-ManagedFile -Path $offPresetPath -ExpectedHash $state.OffPresetHash
    Assert-ManagedFile -Path $onPresetPath -ExpectedHash $state.OnPresetHash
    Assert-ManagedFile -Path $vrConfigPath -ExpectedHash $state.VrConfigHash

    Set-VrConfig -PresetPath $PresetPath
    Update-ManagedHashes -State $state

    New-Item -ItemType Directory -Path $captureDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $simulatorDataDirectory -Force | Out-Null
    Remove-Item -LiteralPath $screenshotRequestPath, $screenshotPath, $screenshotStatusPath -Force -ErrorAction SilentlyContinue

    $stdoutPath = Join-Path $captureDirectory "effect-$Mode-probe.stdout.txt"
    $stderrPath = Join-Path $captureDirectory "effect-$Mode-probe.stderr.txt"
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    $savedRuntime = $env:XR_RUNTIME_JSON
    $savedLayerPath = $env:XR_API_LAYER_PATH
    $savedEnabledLayers = $env:XR_ENABLE_API_LAYERS
    $savedFrames = $env:XR_PROBE_FRAMES
    $savedSleep = $env:XR_PROBE_SLEEP_MS
    $savedProjectionOnly = $env:XR_PROBE_PROJECTION_ONLY
    try {
        Set-SimulatorCaptureWindow
        $env:XR_RUNTIME_JSON = $runtimeManifestPath
        $env:XR_API_LAYER_PATH = $layerDirectory
        $env:XR_ENABLE_API_LAYERS = 'XR_APILAYER_reshade'
        $env:XR_PROBE_FRAMES = $Frames.ToString()
        $env:XR_PROBE_SLEEP_MS = '10'
        $env:XR_PROBE_PROJECTION_ONLY = '1'

        $startedAt = [DateTime]::UtcNow
        $process = Start-Process -FilePath $probePath -WorkingDirectory $TestRoot -PassThru `
            -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
        $processHandle = $process.Handle

        $requestFrame = [Math]::Min(300, $Frames - 60)
        $requested = $false
        while (-not $process.WaitForExit(50)) {
            if ($requested -or -not (Test-Path -LiteralPath $screenshotStatusPath -PathType Leaf)) {
                if (-not (Test-Path -LiteralPath (Join-Path $simulatorDataDirectory 'runtime_status.json') -PathType Leaf)) {
                    continue
                }
            }

            $runtimeStatusPath = Join-Path $simulatorDataDirectory 'runtime_status.json'
            if (-not $requested -and (Get-Item -LiteralPath $runtimeStatusPath).LastWriteTimeUtc -ge $startedAt) {
                try {
                    $runtimeStatus = Get-Content -LiteralPath $runtimeStatusPath -Raw | ConvertFrom-Json
                    if ([int]$runtimeStatus.frame_count -ge $requestFrame) {
                        [ordered]@{
                            timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() / 1000.0
                            eye = 'both'
                            layer = 'projection'
                            requested_by = 'reshade-openxr-test'
                        } | ConvertTo-Json | Set-Content -LiteralPath $screenshotRequestPath -Encoding ASCII
                        $requested = $true
                    }
                }
                catch {
                    # The runtime may be replacing the status file while it is read.
                }
            }
        }
        $process.WaitForExit()
        $process.Refresh()

        $probeOutput = Get-Content -LiteralPath $stdoutPath -Raw
        $validationPattern = "no $GraphicsApi validation errors"
        $probeOutput -split '\r?\n' |
            Where-Object { $_ -match 'rendered [0-9]+/[0-9]+ frames' -or $_ -match [regex]::Escape($validationPattern) -or $_ -match '=+ [0-9]+ failure\(s\)' } |
            ForEach-Object { Write-Host "$Mode probe: $($_.Trim())" }
        $exitCode = $process.ExitCode
        if ($null -eq $exitCode) {
            throw "OpenXR $Mode probe completed without an available exit code. Output: $stdoutPath"
        }
        if ($exitCode -ne 0) {
            throw "OpenXR $Mode probe failed with exit code '$exitCode'. Output: $stdoutPath"
        }
        if ($probeOutput -notmatch '=+ 0 failure\(s\), [0-9]+ warning\(s\) =+') {
            throw "OpenXR $Mode probe did not report zero failures. Output: $stdoutPath"
        }
        if (-not $requested) {
            throw "OpenXR $Mode probe ended before screenshot frame $requestFrame."
        }
        if (-not (Test-Path -LiteralPath $screenshotPath -PathType Leaf)) {
            throw "The simulator did not produce a screenshot for the $Mode probe."
        }

        Copy-Item -LiteralPath $screenshotPath -Destination $OutputPath -Force
        if (Test-Path -LiteralPath $logPath -PathType Leaf) {
            Copy-Item -LiteralPath $logPath -Destination (Join-Path $captureDirectory "ReShade-effect-$Mode.log") -Force
            $compileErrors = Select-String -LiteralPath $logPath -Pattern 'error X[0-9]+|failed to compile|preprocessor error' -CaseSensitive:$false
            if ($compileErrors) {
                throw "ReShade reported an effect compilation error during the $Mode probe. See '$logPath'."
            }
        }
    }
    finally {
        $env:XR_RUNTIME_JSON = $savedRuntime
        $env:XR_API_LAYER_PATH = $savedLayerPath
        $env:XR_ENABLE_API_LAYERS = $savedEnabledLayers
        $env:XR_PROBE_FRAMES = $savedFrames
        $env:XR_PROBE_SLEEP_MS = $savedSleep
        $env:XR_PROBE_PROJECTION_ONLY = $savedProjectionOnly
        if ((Test-Path -LiteralPath $statePath) -and
            (Test-Path -LiteralPath $vrConfigPath) -and
            (Test-Path -LiteralPath $effectPath) -and
            (Test-Path -LiteralPath $offPresetPath) -and
            (Test-Path -LiteralPath $onPresetPath)) {
            Update-ManagedHashes -State (Read-State)
        }
    }
}

function Compare-Captures {
    param(
        [Parameter(Mandatory = $true)][string]$OffPath,
        [Parameter(Mandatory = $true)][string]$OnPath
    )

    Add-Type -AssemblyName System.Drawing
    $offImage = [System.Drawing.Bitmap]::new($OffPath)
    $onImage = [System.Drawing.Bitmap]::new($OnPath)
    try {
        if ($offImage.Width -ne $onImage.Width -or $offImage.Height -ne $onImage.Height) {
            throw "Capture dimensions differ: $($offImage.Size) versus $($onImage.Size)."
        }

        $samples = 0
        $changed = 0
        $differenceTotal = 0.0
        $inversionErrorTotal = 0.0
        for ($y = 4; $y -lt $offImage.Height; $y += 16) {
            for ($x = 4; $x -lt $offImage.Width; $x += 16) {
                $offPixel = $offImage.GetPixel($x, $y)
                $onPixel = $onImage.GetPixel($x, $y)
                $pixelDifference = 0
                $pixelInversionError = 0
                foreach ($channel in 'R', 'G', 'B') {
                    $offValue = [int]$offPixel.$channel
                    $onValue = [int]$onPixel.$channel
                    $pixelDifference += [Math]::Abs($offValue - $onValue)
                    $pixelInversionError += [Math]::Abs(($offValue + $onValue) - 255)
                }
                if ($pixelDifference -ge 90) {
                    $changed++
                    $inversionErrorTotal += $pixelInversionError
                }
                $differenceTotal += $pixelDifference
                $samples++
            }
        }

        $changedRatio = $changed / [double]$samples
        $meanChannelDifference = $differenceTotal / ($samples * 3.0)
        $meanInversionError = $inversionErrorTotal / ($changed * 3.0)
        Write-Host ("Capture comparison: {0:P1} changed, mean channel difference {1:N1}/255, changed-pixel inversion error {2:N2}/255" -f `
            $changedRatio, $meanChannelDifference, $meanInversionError)
        if ($changedRatio -lt 0.80 -or $meanChannelDifference -lt 40.0 -or $meanInversionError -gt 3.0) {
            throw 'The effect-on capture does not match the expected deterministic RGB inversion.'
        }
    }
    finally {
        $offImage.Dispose()
        $onImage.Dispose()
    }
}

function Invoke-VisualCapture {
    if ($Frames -lt 360) {
        throw 'Visual capture requires at least 360 frames so ReShade can compile the effect before the screenshot request.'
    }

    Assert-TestBundle
    $offCapturePath = Join-Path $captureDirectory 'effect-off.bmp'
    $onCapturePath = Join-Path $captureDirectory 'effect-on.bmp'
    $settingsExisted = Test-Path -LiteralPath $simulatorSettingsPath -PathType Leaf
    $savedSettings = if ($settingsExisted) { Get-Content -LiteralPath $simulatorSettingsPath -Raw } else { $null }
    try {
        Invoke-CaptureProbe -Mode off -PresetPath $offPresetPath -OutputPath $offCapturePath
        Invoke-CaptureProbe -Mode on -PresetPath $onPresetPath -OutputPath $onCapturePath
        Compare-Captures -OffPath $offCapturePath -OnPath $onCapturePath
        Write-Host "Saved effect-off capture: $offCapturePath"
        Write-Host "Saved effect-on capture:  $onCapturePath"
    }
    finally {
        if ((Test-Path -LiteralPath $statePath) -and (Read-State).Version -ge 2) {
            Set-VrConfig -PresetPath $offPresetPath
            Update-ManagedHashes -State (Read-State)
        }
        if ($settingsExisted) {
            $savedSettings | Set-Content -LiteralPath $simulatorSettingsPath -Encoding UTF8
        }
        else {
            Remove-Item -LiteralPath $simulatorSettingsPath -Force -ErrorAction SilentlyContinue
        }
    }
}

function Uninstall-TestFiles {
    $state = Read-State
    if (-not $state) {
        throw "Managed state not found at '$statePath'; nothing will be removed."
    }

    Assert-ManagedFile -Path $proxyPath -ExpectedHash $state.ProxyHash
    Assert-ManagedFile -Path $layerManifestPath -ExpectedHash $state.LayerManifestHash
    if ($state.Version -ge 2) {
        Assert-ManagedFile -Path $effectPath -ExpectedHash $state.EffectHash
        Assert-ManagedFile -Path $offPresetPath -ExpectedHash $state.OffPresetHash
        Assert-ManagedFile -Path $onPresetPath -ExpectedHash $state.OnPresetHash
        Assert-ManagedFile -Path $vrConfigPath -ExpectedHash $state.VrConfigHash
    }
    if ($state.ConfigCreated) {
        Assert-ManagedFile -Path $configPath -ExpectedHash $state.ConfigHash
    }

    Remove-Item -LiteralPath $proxyPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $layerManifestPath -Force -ErrorAction SilentlyContinue
    if ($state.Version -ge 2) {
        Remove-Item -LiteralPath $effectPath, $offPresetPath, $onPresetPath -Force -ErrorAction SilentlyContinue
    }
    if ($state.ConfigCreated) {
        Remove-Item -LiteralPath $configPath -Force -ErrorAction SilentlyContinue
    }
    if (-not $state.VrConfigExisted) {
        Remove-Item -LiteralPath $vrConfigPath -Force -ErrorAction SilentlyContinue
    }
    if (-not $state.LogExisted) {
        Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $statePath -Force
    if ((Test-Path -LiteralPath $layerDirectory) -and -not (Get-ChildItem -LiteralPath $layerDirectory -Force)) {
        Remove-Item -LiteralPath $layerDirectory -Force
    }
    if ((Test-Path -LiteralPath $effectDirectory) -and -not (Get-ChildItem -LiteralPath $effectDirectory -Force)) {
        Remove-Item -LiteralPath $effectDirectory -Force
        $effectParentDirectory = Split-Path $effectDirectory -Parent
        if ((Test-Path -LiteralPath $effectParentDirectory) -and -not (Get-ChildItem -LiteralPath $effectParentDirectory -Force)) {
            Remove-Item -LiteralPath $effectParentDirectory -Force
        }
    }

    Write-Host 'Removed the managed ReShade simulator deployment. Captures were retained and global OpenXR settings were not changed.'
}

switch ($Action) {
    'Install' { Install-TestFiles }
    'Run' { Invoke-Probe }
    'Capture' { Invoke-VisualCapture }
    'Status' { Show-Status }
    'Uninstall' { Uninstall-TestFiles }
}