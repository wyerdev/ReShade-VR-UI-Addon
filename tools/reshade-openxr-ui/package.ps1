param(
	[string]$BuildDirectory = 'build\reshade-openxr-ui',
	[string]$OutputDirectory = 'build\reshade-openxr-ui-release'
)

$ErrorActionPreference = 'Stop'

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) {
	$BuildDirectory = Join-Path $repo $BuildDirectory
}
if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
	$OutputDirectory = Join-Path $repo $OutputDirectory
}
$buildDirectory = $BuildDirectory

$addon = Join-Path $buildDirectory 'ReShade-OpenXR-UI.addon64'
$openvrAddon = Join-Path $buildDirectory 'ReShade-SteamVR-UI.addon64'
$loader = Join-Path $buildDirectory 'ReShade-VR-Loader.exe'
$manifest = Join-Path $buildDirectory 'layers\ReShade-OpenXR-UI-Layer.json'
$openxrInstallScript = Join-Path $PSScriptRoot 'Install-OpenXR.cmd'
$openxrUninstallScript = Join-Path $PSScriptRoot 'Uninstall-OpenXR.cmd'
$listenerInstallScript = Join-Path $PSScriptRoot 'Install-System-Wide-SteamVR-Listener.cmd'
$listenerUninstallScript = Join-Path $PSScriptRoot 'Uninstall-System-Wide-SteamVR-Listener.cmd'
$installGuide = Join-Path $PSScriptRoot 'INSTALL.md'
$projectLicense = Join-Path $repo 'LICENSE'
$thirdPartyNotices = Join-Path $repo 'THIRD-PARTY-NOTICES.txt'

foreach ($requiredPath in @($addon, $openvrAddon, $loader, $manifest, $openxrInstallScript, $openxrUninstallScript, $listenerInstallScript, $listenerUninstallScript, $installGuide, $projectLicense, $thirdPartyNotices)) {
	if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
		throw "Required release file does not exist: $requiredPath"
	}
}

$packageDirectory = Join-Path $OutputDirectory 'ReShade-VR-UI-Addon'
$packageLayerDirectory = Join-Path $packageDirectory 'openxr-layers'
$archive = Join-Path $OutputDirectory 'ReShade-VR-UI-Addon.zip'
$listenerPackageDirectory = Join-Path $OutputDirectory 'ReShade-System-Wide-SteamVR-Listener'
$listenerArchive = Join-Path $OutputDirectory 'ReShade-System-Wide-SteamVR-Listener.zip'

Remove-Item -LiteralPath $packageDirectory -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $listenerPackageDirectory -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $listenerArchive -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $packageLayerDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $listenerPackageDirectory -Force | Out-Null

Copy-Item -LiteralPath $addon -Destination $packageDirectory
Copy-Item -LiteralPath $openvrAddon -Destination $packageDirectory
Copy-Item -LiteralPath $manifest -Destination $packageLayerDirectory
Copy-Item -LiteralPath $openxrInstallScript, $openxrUninstallScript, $installGuide, $projectLicense, $thirdPartyNotices -Destination $packageDirectory

Copy-Item -LiteralPath $openvrAddon -Destination $listenerPackageDirectory
Copy-Item -LiteralPath $loader -Destination (Join-Path $listenerPackageDirectory 'ReShade-System-Wide-SteamVR-Listener.exe')
Copy-Item -LiteralPath $listenerInstallScript, $listenerUninstallScript, $installGuide, $projectLicense, $thirdPartyNotices -Destination $listenerPackageDirectory

Compress-Archive -Path (Join-Path $packageDirectory '*') -DestinationPath $archive -CompressionLevel Optimal
Compress-Archive -Path (Join-Path $listenerPackageDirectory '*') -DestinationPath $listenerArchive -CompressionLevel Optimal

foreach ($packageArchive in @($archive, $listenerArchive)) {
	$archiveItem = Get-Item -LiteralPath $packageArchive
	$archiveHash = Get-FileHash -LiteralPath $packageArchive -Algorithm SHA256
	Write-Host "PACKAGE:$packageArchive"
	Write-Host "SIZE:$($archiveItem.Length)"
	Write-Host "SHA256:$($archiveHash.Hash)"
}