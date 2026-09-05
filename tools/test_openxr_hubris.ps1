[CmdletBinding()]
param(
    [ValidateSet('Install', 'Register', 'Prepare', 'Run', 'Status', 'Unregister', 'Uninstall')]
    [string]$Action = 'Status',

    [Parameter(Mandatory = $true)]
    [string]$GameRoot,

    [string]$ReShadePath = '.deps\reshade',

    [string]$SourceDll
)

$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not [System.IO.Path]::IsPathRooted($GameRoot)) {
    $GameRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $GameRoot))
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
$steamAppId = '1265800'
$steamManifest = Join-Path (Split-Path (Split-Path $GameRoot -Parent) -Parent) "appmanifest_$steamAppId.acf"
$gameDirectory = Join-Path $GameRoot 'HubrisVR\Binaries\Win64'
$gameExecutable = Join-Path $gameDirectory 'HubrisVR-Win64-Shipping.exe'
$proxyPath = Join-Path $gameDirectory 'dxgi.dll'
$effectPath = Join-Path $gameDirectory 'DeterministicInvert.fx'
$configPath = Join-Path $gameDirectory 'ReShade.ini'
$logPath = Join-Path $gameDirectory 'ReShade.log'
$stageDirectory = Join-Path $repoRoot 'build\openxr-hubris'
$manifestPath = Join-Path $stageDirectory 'ReShade64_XR.json'
$statePath = Join-Path $stageDirectory 'state.json'
$registrationStatePath = Join-Path $stageDirectory 'registration-state.json'
$implicitLayerRegistryPath = 'HKCU:\Software\Khronos\OpenXR\1\ApiLayers\Implicit'
$machineImplicitLayerRegistryPath = 'HKLM:\Software\Khronos\OpenXR\1\ApiLayers\Implicit'
$officialManifestPath = Join-Path $env:ProgramData 'ReShade\ReShade64_XR.json'

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

function Read-RegistrationState {
    if (-not (Test-Path -LiteralPath $registrationStatePath -PathType Leaf)) {
        return $null
    }

    return Get-Content -LiteralPath $registrationStatePath -Raw | ConvertFrom-Json
}

function Get-ImplicitLayerValue {
    $key = Get-Item -LiteralPath $implicitLayerRegistryPath -ErrorAction SilentlyContinue
    if ($null -eq $key -or $key.GetValueNames() -notcontains $manifestPath) {
        return [pscustomobject]@{ Exists = $false; Value = $null }
    }

    return [pscustomobject]@{ Exists = $true; Value = $key.GetValue($manifestPath) }
}

function Get-OfficialImplicitLayerValue {
    $key = Get-Item -LiteralPath $machineImplicitLayerRegistryPath -ErrorAction SilentlyContinue
    if ($null -eq $key -or $key.GetValueNames() -notcontains $officialManifestPath) {
        return [pscustomobject]@{ Exists = $false; Value = $null }
    }

    return [pscustomobject]@{ Exists = $true; Value = $key.GetValue($officialManifestPath) }
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

function Install-TestFiles {
    if (-not $sourceDll) {
        throw 'Pass -SourceDll to a separately built ReShade64.dll. This harness does not use the standalone OpenXR UI add-on DLL.'
    }
    $officialValue = Get-OfficialImplicitLayerValue
    if ($officialValue.Exists -and $officialValue.Value -eq 0) {
        throw "Refusing to install a local ReShade proxy while the official machine-wide OpenXR layer is enabled: $officialManifestPath"
    }

    if (-not (Test-Path -LiteralPath $sourceDll -PathType Leaf)) {
        throw "ReShade proxy DLL not found: $sourceDll. Build ReShade separately or pass -SourceDll to a compatible ReShade64.dll."
    }
    if (-not (Test-Path -LiteralPath $sourceEffect -PathType Leaf)) {
        throw "Test effect not found: $sourceEffect."
    }
    if (-not (Test-Path -LiteralPath $steamManifest -PathType Leaf) -or
        -not (Test-Path -LiteralPath $gameExecutable -PathType Leaf)) {
        throw "Hubris was not found under '$GameRoot'."
    }

    $state = Read-State
    if ($state) {
        Assert-ManagedFile -Path $proxyPath -ExpectedHash $state.ProxyHash
        Assert-ManagedFile -Path $manifestPath -ExpectedHash $state.ManifestHash
        if ($state.Version -ge 2) {
            Assert-ManagedFile -Path $effectPath -ExpectedHash $state.EffectHash
        }
        elseif (Test-Path -LiteralPath $effectPath) {
            throw "Refusing to overwrite an existing test effect that is not tracked by this harness: $effectPath"
        }
    }
    elseif ((Test-Path -LiteralPath $proxyPath) -or
        (Test-Path -LiteralPath $manifestPath) -or
        (Test-Path -LiteralPath $effectPath)) {
        throw 'Refusing to overwrite existing ReShade test files that are not tracked by this test harness.'
    }

    New-Item -ItemType Directory -Path $stageDirectory -Force | Out-Null
    if (-not $state -or (Get-FileHashValue $sourceDll) -ne $state.ProxyHash) {
        Copy-Item -LiteralPath $sourceDll -Destination $proxyPath -Force
    }
    Copy-Item -LiteralPath $sourceEffect -Destination $effectPath -Force

    $manifest = [ordered]@{
        file_format_version = '1.0.0'
        api_layer = [ordered]@{
            name = 'XR_APILAYER_reshade'
            library_path = $proxyPath
            api_version = '1.0'
               implementation_version = '2'
            description = "crosire's ReShade post-processing injector for 64-bit"
            disable_environment = 'DISABLE_XR_APILAYER_reshade_1'
        }
    }
    $manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $manifestPath -Encoding ASCII

    if ($state) {
        $configCreated = [bool]$state.ConfigCreated
        $logExisted = [bool]$state.LogExisted
    }
    else {
        $configCreated = -not (Test-Path -LiteralPath $configPath -PathType Leaf)
        $logExisted = Test-Path -LiteralPath $logPath -PathType Leaf
    }
    if ($configCreated -and -not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        "[INSTALL]`r`nLogging=1`r`n" | Set-Content -LiteralPath $configPath -Encoding ASCII
    }

    $newState = [ordered]@{
        Version = 2
        GameRoot = $GameRoot
        ProxyPath = $proxyPath
        ProxyHash = Get-FileHashValue $proxyPath
        ManifestPath = $manifestPath
        ManifestHash = Get-FileHashValue $manifestPath
        EffectPath = $effectPath
        EffectHash = Get-FileHashValue $effectPath
        ConfigPath = $configPath
        ConfigCreated = $configCreated
        ConfigHash = if ($configCreated) { Get-FileHashValue $configPath } else { $null }
        LogPath = $logPath
        LogExisted = $logExisted
    }
    $newState | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding ASCII

    Write-Host "Installed Hubris graphics proxy: $proxyPath"
    Write-Host "Staged process-scoped OpenXR manifest: $manifestPath"
    Write-Host "Installed deterministic test effect: $effectPath"
}

function Register-OpenXrLayer {
    $officialValue = Get-OfficialImplicitLayerValue
    if ($officialValue.Exists -and $officialValue.Value -eq 0) {
        throw "Refusing to register a second ReShade OpenXR layer while the official machine-wide layer is enabled: $officialManifestPath"
    }

    $state = Read-State
    if (-not $state) {
        throw 'Hubris test files are not installed. Run this script with -Action Install first.'
    }

    Assert-ManagedFile -Path $proxyPath -ExpectedHash $state.ProxyHash
    Assert-ManagedFile -Path $manifestPath -ExpectedHash $state.ManifestHash

    $registrationState = Read-RegistrationState
    $currentValue = Get-ImplicitLayerValue
    if ($registrationState) {
        if ($registrationState.RegistryPath -ne $implicitLayerRegistryPath -or
            $registrationState.ManifestPath -ne $manifestPath) {
            throw 'The OpenXR registration differs from the state recorded by this test harness.'
        }
        if ($currentValue.Exists -and $currentValue.Value -ne 0) {
            throw 'The OpenXR registration value differs from the state recorded by this test harness.'
        }
        if (-not $currentValue.Exists) {
            New-Item -Path $implicitLayerRegistryPath -Force | Out-Null
            New-ItemProperty -LiteralPath $implicitLayerRegistryPath -Name $manifestPath -PropertyType DWord -Value 0 -Force | Out-Null
            Write-Host "Re-registered ReShade OpenXR layer using retained state: $manifestPath"
            return
        }

        Write-Host "ReShade OpenXR layer is already registered for the current user: $manifestPath"
        return
    }

    New-Item -Path $implicitLayerRegistryPath -Force | Out-Null
    New-ItemProperty -LiteralPath $implicitLayerRegistryPath -Name $manifestPath -PropertyType DWord -Value 0 -Force | Out-Null

    [ordered]@{
        RegistryPath = $implicitLayerRegistryPath
        ManifestPath = $manifestPath
        PreviousValueExisted = $currentValue.Exists
        PreviousValue = $currentValue.Value
    } | ConvertTo-Json | Set-Content -LiteralPath $registrationStatePath -Encoding ASCII

    Write-Host "Registered ReShade OpenXR layer for the current user: $manifestPath"
}

function Show-Status {
    $state = Read-State
    $registrationState = Read-RegistrationState
    $currentValue = Get-ImplicitLayerValue
    $officialValue = Get-OfficialImplicitLayerValue
    $sourceDllExists = $sourceDll -and (Test-Path -LiteralPath $sourceDll -PathType Leaf)
    Write-Host "Source DLL:      $sourceDllExists  $sourceDll"
    Write-Host "Graphics proxy:  $((Test-Path -LiteralPath $proxyPath -PathType Leaf))  $proxyPath"
    Write-Host "OpenXR manifest: $((Test-Path -LiteralPath $manifestPath -PathType Leaf))  $manifestPath"
    Write-Host "Test effect:     $((Test-Path -LiteralPath $effectPath -PathType Leaf))  $effectPath"
    Write-Host "ReShade config:  $((Test-Path -LiteralPath $configPath -PathType Leaf))  $configPath"
    Write-Host "ReShade log:     $((Test-Path -LiteralPath $logPath -PathType Leaf))  $logPath"
    Write-Host "Managed state:   $($null -ne $state)  $statePath"
    Write-Host "Layer registered: $($currentValue.Exists -and $currentValue.Value -eq 0)  $implicitLayerRegistryPath"
    Write-Host "Official layer enabled: $($officialValue.Exists -and $officialValue.Value -eq 0)  $officialManifestPath"
    Write-Host "Registration state: $($null -ne $registrationState)  $registrationStatePath"

    Get-Process -Name 'HubrisVR*' -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "Running process: $($_.ProcessName) ($($_.Id))"
        $_.Modules | Where-Object ModuleName -Match '^(dxgi|d3d11|d3d12|openxr_loader)\.dll$' |
            ForEach-Object { Write-Host "  $($_.ModuleName): $($_.FileName)" }
    }
}

function Confirm-HubrisReady {
    $officialValue = Get-OfficialImplicitLayerValue
    if ($officialValue.Exists -and $officialValue.Value -eq 0) {
        throw "The official machine-wide ReShade OpenXR layer is still enabled: $officialManifestPath"
    }

    $state = Read-State
    if (-not $state) {
        throw 'Hubris test files are not installed. Run this script with -Action Install first.'
    }

    Assert-ManagedFile -Path $proxyPath -ExpectedHash $state.ProxyHash
    Assert-ManagedFile -Path $manifestPath -ExpectedHash $state.ManifestHash

    $currentValue = Get-ImplicitLayerValue
    if (-not $currentValue.Exists -or $currentValue.Value -ne 0) {
        throw 'The ReShade OpenXR layer is not registered. Run this script with -Action Register first.'
    }

    $steamVrProcesses = Get-Process -Name 'vrserver', 'vrcompositor', 'vrdashboard', 'vrmonitor' -ErrorAction SilentlyContinue
    if ($steamVrProcesses) {
        $processList = ($steamVrProcesses | ForEach-Object { "$($_.ProcessName) ($($_.Id))" }) -join ', '
        throw "Refusing to launch Hubris while SteamVR is running: $processList"
    }

    Write-Host "Hubris OpenXR test is ready for app $steamAppId. Launch Hubris normally through Steam yourself."
}

function Start-Hubris {
    Confirm-HubrisReady
    Start-Process -FilePath "steam://rungameid/$steamAppId"
    Write-Host "Requested normal Steam launch for Hubris app $steamAppId."
}

function Unregister-OpenXrLayer {
    $registrationState = Read-RegistrationState
    if (-not $registrationState) {
        throw "Managed registration state not found at '$registrationStatePath'; no registry value will be changed."
    }
    if ($registrationState.RegistryPath -ne $implicitLayerRegistryPath -or
        $registrationState.ManifestPath -ne $manifestPath) {
        throw 'The managed registration state does not match this test harness.'
    }

    $currentValue = Get-ImplicitLayerValue
    if (-not $currentValue.Exists) {
        if (-not $registrationState.PreviousValueExisted) {
            Write-Host 'The managed current-user registration is already absent. Retained registration state for reuse.'
            return
        }

        throw 'Refusing to restore the OpenXR registration because its managed registry value is unexpectedly absent.'
    }
    if ($currentValue.Value -ne 0) {
        throw 'Refusing to restore the OpenXR registration because its current value was changed outside this test harness.'
    }

    if ($registrationState.PreviousValueExisted) {
        New-ItemProperty -LiteralPath $implicitLayerRegistryPath -Name $manifestPath -PropertyType DWord -Value $registrationState.PreviousValue -Force | Out-Null
    }
    else {
        Remove-ItemProperty -LiteralPath $implicitLayerRegistryPath -Name $manifestPath
    }

    Write-Host 'Restored the previous current-user OpenXR layer registration state. Retained registration state for reuse.'
}

function Uninstall-TestFiles {
    if (Read-RegistrationState) {
        Unregister-OpenXrLayer
    }

    $state = Read-State
    if (-not $state) {
        throw "Managed state not found at '$statePath'; nothing will be removed."
    }

    Assert-ManagedFile -Path $proxyPath -ExpectedHash $state.ProxyHash
    Assert-ManagedFile -Path $manifestPath -ExpectedHash $state.ManifestHash
    if ($state.Version -ge 2) {
        Assert-ManagedFile -Path $effectPath -ExpectedHash $state.EffectHash
    }

    Remove-Item -LiteralPath $proxyPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $manifestPath -Force -ErrorAction SilentlyContinue
    if ($state.Version -ge 2) {
        Remove-Item -LiteralPath $effectPath -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $statePath -Force
    if ((Test-Path -LiteralPath $stageDirectory) -and -not (Get-ChildItem -LiteralPath $stageDirectory -Force)) {
        Remove-Item -LiteralPath $stageDirectory -Force
    }

    Write-Host 'Removed the managed Hubris proxy, manifest, and test effect. Preserved ReShade configuration and logs. Global OpenXR settings were not changed.'
}

switch ($Action) {
    'Install' { Install-TestFiles }
    'Register' { Register-OpenXrLayer }
    'Prepare' { Confirm-HubrisReady }
    'Run' { Start-Hubris }
    'Status' { Show-Status }
    'Unregister' { Unregister-OpenXrLayer }
    'Uninstall' { Uninstall-TestFiles }
}