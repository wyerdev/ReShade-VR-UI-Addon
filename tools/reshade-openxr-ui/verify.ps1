param(
	[string]$ArtifactPath,
	[string]$SteamVRArtifactPath,
	[string]$LoaderPath,
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
if (-not $SteamVRArtifactPath) {
	$SteamVRArtifactPath = Join-Path $OutputDirectory 'ReShade-SteamVR-UI.addon64'
}
if (-not $LoaderPath) {
	$LoaderPath = Join-Path $OutputDirectory 'ReShade-VR-Loader.exe'
}

$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) {
	throw 'Visual Studio Installer vswhere.exe was not found.'
}
$dumpbin = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1
if (-not $dumpbin) {
	throw 'Visual Studio dumpbin.exe was not found.'
}

function Test-Artifact([string]$Path, [string[]]$RequiredExports, [string[]]$ForbiddenExports) {
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		throw "Add-on artifact does not exist: $Path"
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

Test-Artifact $ArtifactPath @('NAME', 'DESCRIPTION', 'xrNegotiateLoaderApiLayerInterface') @()
Test-Artifact $SteamVRArtifactPath @('NAME', 'DESCRIPTION') @('xrNegotiateLoaderApiLayerInterface')

if (-not (Test-Path -LiteralPath $LoaderPath -PathType Leaf)) {
	throw "Central loader artifact does not exist: $LoaderPath"
}
$loaderStream = [System.IO.File]::OpenRead($LoaderPath)
try {
	$loaderReader = [System.IO.BinaryReader]::new($loaderStream)
	$loaderStream.Position = 0x3c
	$peOffset = $loaderReader.ReadInt32()
	$loaderStream.Position = $peOffset
	$peSignature = $loaderReader.ReadUInt32()
	$machine = $loaderReader.ReadUInt16()
	$loaderStream.Position = $peOffset + 24 + 68
	$subsystem = $loaderReader.ReadUInt16()
}
finally {
	$loaderStream.Dispose()
}
if ($peSignature -ne 0x00004550 -or $machine -ne 0x8664 -or $subsystem -ne 2) {
	throw 'Central loader is not an x64 Windows GUI executable.'
}
$loader = Get-Item -LiteralPath $LoaderPath
$loaderHash = Get-FileHash -LiteralPath $LoaderPath -Algorithm SHA256
Write-Host "ARTIFACT:$LoaderPath"
Write-Host "SIZE:$($loader.Length)"
Write-Host "SHA256:$($loaderHash.Hash)"
Write-Host 'TYPE:x64 Windows GUI'