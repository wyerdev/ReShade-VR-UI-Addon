<#
.SYNOPSIS
Copies the locally built SteamVR add-on next to a SteamVR game's executable, or removes it.

.DESCRIPTION
Test helper for SteamVR (OpenVR) games, which load the add-on per game. Run it yourself;
it changes files in the game folder. ReShade itself must already be installed for the game
with ReShade Setup (DirectX 10/11/12, OpenXR unticked). Defaults to Propagation: Paradise Hotel.

Run it through install_steamvr_addon.bat in the repository root, not directly.
#>
param(
    [ValidateSet('Install', 'Uninstall', 'Status')]
    [string]$Action = 'Install',
    [string]$GameExe = "${env:ProgramFiles(x86)}\Steam\steamapps\common\Propagation Paradise Hotel\Propagation_PH_Win64\Binaries\Win64\Propagation_PH-Win64-Shipping.exe",
    [string]$AddonPath = (Join-Path $PSScriptRoot '..\build\reshade-openxr-ui\ReShade-SteamVR-UI.addon64')
)

$ErrorActionPreference = 'Stop'
$addonName = 'ReShade-SteamVR-UI.addon64'

if (-not (Test-Path -LiteralPath $GameExe -PathType Leaf)) {
    throw "Game executable not found: $GameExe. Pass the game's real .exe with -GameExe (for Unreal games it is the *-Win64-Shipping.exe under Binaries\Win64)."
}
$gameDir = Split-Path -Parent (Resolve-Path -LiteralPath $GameExe).Path
$target = Join-Path $gameDir $addonName

function Show-Status {
    $proxies = @('dxgi.dll', 'd3d11.dll', 'd3d12.dll') | Where-Object { Test-Path -LiteralPath (Join-Path $gameDir $_) }
    if ($proxies) {
        Write-Host "ReShade DLL found: $($proxies -join ', ')"
    } else {
        Write-Host 'ReShade is NOT installed for this game (no dxgi.dll, d3d11.dll or d3d12.dll next to the .exe).'
        Write-Host "Install it with ReShade Setup: pick '$GameExe', choose DirectX 10/11/12, leave OpenXR unticked."
    }
    if (Test-Path -LiteralPath $target) {
        $item = Get-Item -LiteralPath $target
        Write-Host "Add-on installed: $target ($($item.LastWriteTime))"
    } else {
        Write-Host 'Add-on not installed.'
    }
    # The add-on writes these on first run and they stay after it is removed, so ReShade's
    # SteamVR menu keeps readable text and no preset sync even without the add-on.
    $leftovers = @()
    if (Select-String -LiteralPath (Join-Path $gameDir 'ReShadeVR.ini') -Pattern '^SteamVROverlayScaleNormalized=1' -Quiet -ErrorAction SilentlyContinue) {
        $leftovers += 'ReShadeVR.ini has the add-on''s smaller menu text (FontSize=12, FontScale=1)'
    }
    if (Select-String -LiteralPath (Join-Path $gameDir 'ReShade.ini') -Pattern '^SyncEffectRuntimes=0' -Quiet -ErrorAction SilentlyContinue) {
        $leftovers += 'ReShade.ini has desktop/VR preset sync turned off (SyncEffectRuntimes=0)'
    }
    foreach ($leftover in $leftovers) {
        Write-Host "Setting from an earlier add-on run: $leftover"
    }
    $log = Join-Path $gameDir 'ReShade.log'
    if (Test-Path -LiteralPath $log) {
        Write-Host "ReShade log: $log ($((Get-Item -LiteralPath $log).LastWriteTime))"
    }
}

if ($Action -eq 'Status') {
    Show-Status
    return
}

$exeName = [IO.Path]::GetFileNameWithoutExtension($GameExe)
if (Get-Process -Name $exeName -ErrorAction SilentlyContinue) {
    throw "$exeName is running. Close the game first."
}

if ($Action -eq 'Uninstall') {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target
        Write-Host "Removed $target"
    } else {
        Write-Host 'Add-on was not installed.'
    }
    return
}

if (-not (Test-Path -LiteralPath $AddonPath -PathType Leaf)) {
    throw "Built add-on not found: $AddonPath. Run build.bat first."
}
Copy-Item -LiteralPath $AddonPath -Destination $target -Force
Write-Host "Installed $target"
Show-Status
