./[CmdletBinding()]
param()

# Returns the machine-wide OpenXR runtime to a normal headset runtime if the OpenXR Simulator was left selected.
$ErrorActionPreference = 'Stop'

$registryPath = 'HKLM:\SOFTWARE\Khronos\OpenXR\1'
$runStatePath = Join-Path $PSScriptRoot '..\build\uevr-simulator\run-state.json'

function Test-IsSimulator([string]$Path) {
    return $Path -and $Path -match 'openxr_simulator'
}

if (Test-Path -LiteralPath $runStatePath -PathType Leaf) {
    Write-Host 'Closing the open UEVR test session first.'
    & (Join-Path $PSScriptRoot 'test_uevr_simulator.ps1') -Action Stop
}

$userRuntime = [Environment]::GetEnvironmentVariable('XR_RUNTIME_JSON', 'User')
if (Test-IsSimulator $userRuntime) {
    [Environment]::SetEnvironmentVariable('XR_RUNTIME_JSON', $null, 'User')
    Write-Host "Removed the user setting XR_RUNTIME_JSON ($userRuntime). Restart open programs to pick this up."
}
$machineRuntime = [Environment]::GetEnvironmentVariable('XR_RUNTIME_JSON', 'Machine')
if (Test-IsSimulator $machineRuntime) {
    Write-Warning "The machine setting XR_RUNTIME_JSON still points at the simulator ($machineRuntime). Remove it in System Properties > Environment Variables."
}

$settings = Get-ItemProperty -Path $registryPath -ErrorAction Ignore
$active = if ($settings) { $settings.ActiveRuntime } else { $null }
if (Test-IsSimulator $active) {
    $previous = $settings.PreviousActiveRuntime
    if (-not $previous) {
        throw "The OpenXR Simulator is the active runtime and no previous runtime was saved. Re-select your headset runtime in its own app (Pimax Play, SteamVR, Meta Link, ...)."
    }
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host 'Administrator rights are needed to change the active OpenXR runtime. Asking Windows for them...'
        $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -PassThru -ArgumentList @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
        exit $process.ExitCode
    }
    Set-ItemProperty -Path $registryPath -Name 'ActiveRuntime' -Value $previous
    Remove-ItemProperty -Path $registryPath -Name 'PreviousActiveRuntime'
    Write-Host "Restored the active OpenXR runtime: $previous"
}

$active = (Get-ItemProperty -Path $registryPath -ErrorAction Ignore).ActiveRuntime
Write-Host "Active OpenXR runtime: $active"
if (Test-IsSimulator $active) {
    throw 'The OpenXR Simulator is still the active runtime.'
}
Write-Host 'The OpenXR Simulator is not selected. Games will use your headset runtime.'
