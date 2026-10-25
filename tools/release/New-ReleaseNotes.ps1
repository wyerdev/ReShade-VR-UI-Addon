# Writes the GitHub release page text: which ZIP to download (direct links to this release's files) with a link to
# each install guide at this release's tag, the features with menu pictures, the changelog and the checksums.
param(
	[Parameter(Mandatory)][string]$OutFile,
	[Parameter(Mandatory)][string]$Tag,
	# The same suffix package.ps1 added to the ZIP names, for example "-v12-2026-10-24".
	[Parameter(Mandatory)][string]$AssetSuffix,
	[string]$Repository = 'wyerdev/ReShade-VR-UI-Addon',
	[string]$ChangelogFile,
	[string]$ChecksumsFile
)

$ErrorActionPreference = 'Stop'
$guideRoot = Join-Path $PSScriptRoot '..\reshade-openxr-ui'

function Read-Text([string]$Path) {
	return [IO.File]::ReadAllText((Resolve-Path -LiteralPath $Path)).Replace("`r`n", "`n").Trim()
}

# Markdown link to one ZIP of this release.
function Format-Download([string]$Name) {
	$file = "$Name$AssetSuffix.zip"
	$url = "https://github.com/$Repository/releases/download/$Tag/" + [Uri]::EscapeDataString($file)
	return "**[$([char]0x2B07) $file]($url)**"
}

$top = (Read-Text (Join-Path $guideRoot 'RELEASE_NOTES.md')).
	Replace('{{OPENXR_DOWNLOAD}}', (Format-Download 'ReShade-VR-UI-Addon-OpenXR')).
	Replace('{{STEAMVR_DOWNLOAD}}', (Format-Download 'ReShade-VR-UI-Addon-SteamVR')).
	Replace('{{REPOSITORY}}', $Repository).
	Replace('{{TAG}}', [Uri]::EscapeDataString($Tag))
if ($top -match '\{\{') {
	throw 'RELEASE_NOTES.md has a placeholder this script does not fill.'
}

$sections = @($top)
if ($ChangelogFile) {
	$sections += '## What Changed'
	$sections += Read-Text $ChangelogFile
}
if ($ChecksumsFile) {
	$sections += '## SHA-256 Checksums'
	$sections += @('```', (Read-Text $ChecksumsFile), '```') -join "`n"
}

[IO.File]::WriteAllText([IO.Path]::GetFullPath($OutFile), (($sections -join "`n`n") + "`n"), [Text.UTF8Encoding]::new($false))
