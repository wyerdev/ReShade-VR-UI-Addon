# Writes the GitHub release page text: which ZIP to download (direct links to this release's files), the features
# with menu pictures from this release's tag, each install guide's quick steps with the rest of the guide in a
# fold-out (taken from the install guides, so the steps stay written in one place), the changelog and the checksums.
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

# Shows a guide's quick install steps (everything before its first fold-out) under a heading, and the rest of
# the guide in one fold-out. The guide's own title and download line are dropped (the release page already has
# the downloads), its "Step by step" fold-out is opened up because it is already inside a fold-out, its headings
# move down, and links to its own sections become plain text, because those links do not work inside a fold-out.
function Format-Guide([string]$Title, [string]$FileName) {
	$lines = @((Read-Text (Join-Path $guideRoot $FileName)) -split "`n" |
		Where-Object { $_ -notmatch '^# ' -and $_ -notmatch '^\*\*Download:\*\*' -and $_ -ne '## Install' } |
		ForEach-Object { $_ -replace '^(#{2,}) ', '$1## ' -replace '\[([^\]]+)\]\(#[^)]+\)', '**$1**' })
	$start = [Array]::FindIndex($lines, [Predicate[string]] { param($line) $line -match '^<details>' })
	$end = [Array]::FindIndex($lines, [Predicate[string]] { param($line) $line -eq '</details>' })
	if ($start -lt 1 -or $end -lt $start) {
		throw "$FileName needs quick install steps followed by a ""Step by step"" fold-out."
	}
	$quick = ($lines[0..($start - 1)] -join "`n").Trim()
	$rest = ((@($lines[($start + 1)..($end - 1)]) + @($lines | Select-Object -Skip ($end + 1))) -join "`n") -replace "`n{3,}", "`n`n"
	return @("### $Title", '', $quick, '', '<details><summary><b>Step by step, help and troubleshooting</b> (click to open)</summary>', '', $rest.Trim(), '', '</details>') -join "`n"
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
	'## Install',
	(Format-Guide 'OpenXR games and UEVR' 'INSTALL.md'),
	(Format-Guide 'SteamVR games' 'INSTALL-SteamVR.md'),
	(Format-Guide 'Vulkan and 32-bit games' 'INSTALL-Vulkan-32bit.md')
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
