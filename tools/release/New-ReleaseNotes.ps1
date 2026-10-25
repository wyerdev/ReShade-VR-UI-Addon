# Writes the GitHub release page text: which ZIP to download (direct links to this release's files), the features
# with menu pictures from this release's tag, the full
# install steps for each kind of game (taken from the install guides, so the steps stay written in one place),
# the changelog and the checksums.
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

# Shows a guide as a fold-out section. Its own title and download line are dropped (the release page already
# has the downloads), its headings move two levels down, and links to its own sections become plain text,
# because those links do not work inside a fold-out section.
function Format-Guide([string]$Summary, [string]$FileName) {
	$lines = (Read-Text (Join-Path $guideRoot $FileName)) -split "`n" |
		Where-Object { $_ -notmatch '^# ' -and $_ -notmatch '^\*\*Download:\*\*' } |
		ForEach-Object { $_ -replace '^(#{2,}) ', '$1## ' -replace '\[([^\]]+)\]\(#[^)]+\)', '**$1**' }
	return @("<details><summary><b>$Summary</b> (click to open)</summary>", '', (($lines -join "`n") -replace "`n{3,}", "`n`n").Trim(), '', '</details>') -join "`n"
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

$sections = @(
	$top,
	'## Install Steps',
	(Format-Guide 'Install steps: OpenXR games and UEVR' 'INSTALL.md'),
	(Format-Guide 'Install steps: SteamVR games' 'INSTALL-SteamVR.md'),
	(Format-Guide 'Install steps: Vulkan and 32-bit games' 'INSTALL-Vulkan-32bit.md')
)
if ($ChangelogFile) {
	$sections += '## What Changed'
	$sections += Read-Text $ChangelogFile
}
if ($ChecksumsFile) {
	$sections += '## SHA-256 Checksums'
	$sections += @('```', (Read-Text $ChecksumsFile), '```') -join "`n"
}

[IO.File]::WriteAllText([IO.Path]::GetFullPath($OutFile), (($sections -join "`n`n") + "`n"), [Text.UTF8Encoding]::new($false))
