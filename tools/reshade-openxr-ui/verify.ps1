param(
	[string]$ArtifactPath,
	[string]$Artifact32Path,
	[string]$SteamVRArtifactPath,
	[string]$OutputDirectory = 'build\reshade-openxr-ui'
)

$ErrorActionPreference = 'Stop'

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
	$OutputDirectory = Join-Path $repo $OutputDirectory
}
if (-not $ArtifactPath) {
	$ArtifactPath = Join-Path $OutputDirectory 'ReShade-OpenXR-UI.addon64'
}
if (-not $Artifact32Path) {
	$Artifact32Path = Join-Path $OutputDirectory 'ReShade-OpenXR-UI.addon32'
}
if (-not $SteamVRArtifactPath) {
	$SteamVRArtifactPath = Join-Path $OutputDirectory 'ReShade-SteamVR-UI.addon64'
}

$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) {
	throw 'Visual Studio Installer vswhere.exe was not found.'
}
$dumpbin = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1
if (-not $dumpbin) {
	throw 'Visual Studio dumpbin.exe was not found.'
}

function Test-Artifact([string]$Path, [string]$Machine, [string[]]$RequiredExports, [string[]]$ForbiddenExports) {
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		throw "Add-on artifact does not exist: $Path"
	}
	$headers = & $dumpbin /nologo /headers $Path | Out-String
	if ($LASTEXITCODE -ne 0) {
		throw "dumpbin failed with exit code $LASTEXITCODE."
	}
	if ($headers -notmatch "machine \($Machine\)") {
		throw "$Path is not built for $Machine."
	}
	$exports = & $dumpbin /nologo /exports $Path | Out-String
	if ($LASTEXITCODE -ne 0) {
		throw "dumpbin failed with exit code $LASTEXITCODE."
	}
	foreach ($requiredExport in $RequiredExports) {
		if ($exports -notmatch "(?m)\s$requiredExport\s*$") {
			throw "$Path is missing required export: $requiredExport"
		}
	}
	foreach ($forbiddenExport in $ForbiddenExports) {
		if ($exports -match "(?m)\s$forbiddenExport\s*$") {
			throw "$Path has unexpected export: $forbiddenExport"
		}
	}
	$artifact = Get-Item -LiteralPath $Path
	$hash = Get-FileHash -LiteralPath $Path -Algorithm SHA256
	Write-Host "ARTIFACT:$Path"
	Write-Host "SIZE:$($artifact.Length)"
	Write-Host "SHA256:$($hash.Hash)"
	foreach ($requiredExport in $RequiredExports) {
		Write-Host "EXPORT:$requiredExport"
	}
}

Test-Artifact $ArtifactPath 'x64' @('NAME', 'DESCRIPTION', 'xrNegotiateLoaderApiLayerInterface') @()
Test-Artifact $Artifact32Path 'x86' @('NAME', 'DESCRIPTION', 'xrNegotiateLoaderApiLayerInterface') @()
Test-Artifact $SteamVRArtifactPath 'x64' @('NAME', 'DESCRIPTION') @('xrNegotiateLoaderApiLayerInterface')