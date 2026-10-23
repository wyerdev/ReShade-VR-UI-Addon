param(
	[string]$AddonBuildDirectory = 'build\reshade-openxr-ui',
	[string]$BuildDirectory = 'build\steamvr-listener',
	[string]$OutputDirectory = 'build\steamvr-listener-release'
)

$ErrorActionPreference = 'Stop'

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
foreach ($name in @('AddonBuildDirectory', 'BuildDirectory', 'OutputDirectory')) {
	$value = Get-Variable -Name $name -ValueOnly
	if (-not [System.IO.Path]::IsPathRooted($value)) {
		Set-Variable -Name $name -Value (Join-Path $repo $value)
	}
}

$listener = Join-Path $BuildDirectory 'ReShade-System-Wide-SteamVR-Listener.exe'
$openvrAddon = Join-Path $AddonBuildDirectory 'ReShade-SteamVR-UI.addon64'
$installScript = Join-Path $PSScriptRoot 'Install-System-Wide-SteamVR-Listener.cmd'
$uninstallScript = Join-Path $PSScriptRoot 'Uninstall-System-Wide-SteamVR-Listener.cmd'
$installGuide = Join-Path $PSScriptRoot 'INSTALL.md'
$projectLicense = Join-Path $repo 'LICENSE'
$thirdPartyNotices = Join-Path $repo 'THIRD-PARTY-NOTICES.txt'

foreach ($requiredPath in @($listener, $openvrAddon, $installScript, $uninstallScript, $installGuide, $projectLicense, $thirdPartyNotices)) {
	if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
		throw "Required listener release file does not exist: $requiredPath"
	}
}

$packageDirectory = Join-Path $OutputDirectory 'ReShade-System-Wide-SteamVR-Listener'
$archive = Join-Path $OutputDirectory 'ReShade-System-Wide-SteamVR-Listener.zip'

Remove-Item -LiteralPath $packageDirectory -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null

Copy-Item -LiteralPath $listener, $openvrAddon, $installScript, $uninstallScript, $installGuide, $projectLicense, $thirdPartyNotices -Destination $packageDirectory

Compress-Archive -Path (Join-Path $packageDirectory '*') -DestinationPath $archive -CompressionLevel Optimal

$archiveItem = Get-Item -LiteralPath $archive
$archiveHash = Get-FileHash -LiteralPath $archive -Algorithm SHA256
Write-Host "PACKAGE:$archive"
Write-Host "SIZE:$($archiveItem.Length)"
Write-Host "SHA256:$($archiveHash.Hash)"
