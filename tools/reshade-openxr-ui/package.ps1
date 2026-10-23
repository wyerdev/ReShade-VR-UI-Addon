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
$manifest = Join-Path $buildDirectory 'layers\ReShade-OpenXR-UI-Layer.json'
$openxrInstallScript = Join-Path $PSScriptRoot 'Install-OpenXR.cmd'
$openxrUninstallScript = Join-Path $PSScriptRoot 'Uninstall-OpenXR.cmd'
$openxrInstallGuide = Join-Path $PSScriptRoot 'INSTALL.md'
$steamvrInstallGuide = Join-Path $PSScriptRoot 'INSTALL-SteamVR.md'
$projectLicense = Join-Path $repo 'LICENSE'
$thirdPartyNotices = Join-Path $repo 'THIRD-PARTY-NOTICES.txt'

foreach ($requiredPath in @($addon, $openvrAddon, $manifest, $openxrInstallScript, $openxrUninstallScript, $openxrInstallGuide, $steamvrInstallGuide, $projectLicense, $thirdPartyNotices)) {
	if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
		throw "Required release file does not exist: $requiredPath"
	}
}

function New-ReleaseArchive([string]$Name, [scriptblock]$Fill) {
	$packageDirectory = Join-Path $OutputDirectory $Name
	$archive = Join-Path $OutputDirectory "$Name.zip"

	Remove-Item -LiteralPath $packageDirectory -Recurse -Force -ErrorAction SilentlyContinue
	Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
	New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null

	& $Fill $packageDirectory
	Copy-Item -LiteralPath $projectLicense, $thirdPartyNotices -Destination $packageDirectory

	Compress-Archive -Path (Join-Path $packageDirectory '*') -DestinationPath $archive -CompressionLevel Optimal

	$archiveItem = Get-Item -LiteralPath $archive
	$archiveHash = Get-FileHash -LiteralPath $archive -Algorithm SHA256
	Write-Host "PACKAGE:$archive"
	Write-Host "SIZE:$($archiveItem.Length)"
	Write-Host "SHA256:$($archiveHash.Hash)"
}

# Each ZIP holds only its own guide, renamed to INSTALL.md.
New-ReleaseArchive 'ReShade-VR-UI-Addon-OpenXR' {
	param($packageDirectory)
	$packageLayerDirectory = Join-Path $packageDirectory 'openxr-layers'
	New-Item -ItemType Directory -Path $packageLayerDirectory -Force | Out-Null
	Copy-Item -LiteralPath $addon, $openxrInstallScript, $openxrUninstallScript -Destination $packageDirectory
	Copy-Item -LiteralPath $manifest -Destination $packageLayerDirectory
	Copy-Item -LiteralPath $openxrInstallGuide -Destination (Join-Path $packageDirectory 'INSTALL.md')
}

New-ReleaseArchive 'ReShade-VR-UI-Addon-SteamVR' {
	param($packageDirectory)
	Copy-Item -LiteralPath $openvrAddon -Destination $packageDirectory
	Copy-Item -LiteralPath $steamvrInstallGuide -Destination (Join-Path $packageDirectory 'INSTALL.md')
}

# Remove the former single package so it cannot be published by mistake.
Remove-Item -LiteralPath (Join-Path $OutputDirectory 'ReShade-VR-UI-Addon') -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $OutputDirectory 'ReShade-VR-UI-Addon.zip') -Force -ErrorAction SilentlyContinue