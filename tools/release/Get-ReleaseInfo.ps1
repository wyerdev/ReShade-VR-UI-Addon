# Works out the next release number and the changelog from Conventional Commits.
# The release number follows the highest vN tag. The changelog starts at the last vN tag that still
# has a published release (-ReleasedTags), so deleting releases folds their changes into the next one.
# Without -ReleasedTags every vN tag counts as released.
# Whether to release is decided by the workflow (every push to main unless a docs commit or "(Skip)"), not here.
# Documentation entries and lines without a conventional prefix are left out of the changelog.
param(
	[string]$Head = 'HEAD',
	[string]$NotesFile,
	[datetime]$Date = [datetime]::UtcNow,
	[AllowEmptyCollection()]
	[string[]]$ReleasedTags
)

$ErrorActionPreference = 'Stop'

function Invoke-GitRead([string[]]$Arguments) {
	$previous = $ErrorActionPreference
	$ErrorActionPreference = 'Continue'
	$output = & git @Arguments 2>$null
	$exitCode = $LASTEXITCODE
	$ErrorActionPreference = $previous
	return [pscustomobject]@{ ExitCode = $exitCode; Output = @($output) }
}

$filterReleased = $PSBoundParameters.ContainsKey('ReleasedTags')
$released = [System.Collections.Generic.HashSet[string]]::new([string[]]@($ReleasedTags | Where-Object { $_ }))

$lastNumber = 0
$lastReleasedTag = $null
$lastReleasedNumber = 0
$tags = Invoke-GitRead @('tag', '--list', 'v*', '--merged', $Head)
foreach ($tag in $tags.Output) {
	$name = "$tag".Trim()
	if ($name -notmatch '^v(\d+)$') { continue }
	$tagNumber = [int]$Matches[1]
	if ($tagNumber -gt $lastNumber) { $lastNumber = $tagNumber }
	if ((-not $filterReleased -or $released.Contains($name)) -and $tagNumber -gt $lastReleasedNumber) {
		$lastReleasedNumber = $tagNumber
		$lastReleasedTag = $name
	}
}

$range = if ($lastReleasedTag) { "$lastReleasedTag..$Head" } else { $Head }
$log = Invoke-GitRead @('log', '--no-merges', '--format=%H%x1f%s%x1f%b%x1e', $range)
if ($log.ExitCode -ne 0) {
	throw "git log failed for range $range."
}

$sections = [ordered]@{
	breaking = 'Breaking changes'
	feat     = 'New features'
	fix      = 'Fixes'
	perf     = 'Performance'
	revert   = 'Reverted'
	refactor = 'Code clean-up'
	build    = 'Build'
	ci       = 'Build automation'
	test     = 'Tests'
	style    = 'Code style'
	chore    = 'Maintenance'
}
$entries = @{}
foreach ($key in $sections.Keys) { $entries[$key] = [System.Collections.Generic.List[string]]::new() }

$pattern = '^(?<type>[a-zA-Z]+)(\((?<scope>[^)]+)\))?(?<bang>!)?:\s*(?<desc>.+)$'
foreach ($record in (($log.Output -join "`n") -split [char]0x1e)) {
	$fields = $record.Trim() -split [char]0x1f
	if ($fields.Count -lt 2) { continue }
	$hash = $fields[0].Trim()
	$body = if ($fields.Count -ge 3) { $fields[2] } else { '' }
	$bodyBreaking = $body -match '(?m)^BREAKING[ -]CHANGE:'

	# One commit can hold several changes: the summary line and every body line that starts with a type each become an entry.
	$lines = @($fields[1]) + @($body -split "`r?`n")
	for ($index = 0; $index -lt $lines.Count; ++$index) {
		$match = [regex]::Match($lines[$index].TrimEnd(), $pattern)
		if (-not $match.Success) { continue }
		$type = $match.Groups['type'].Value.ToLowerInvariant()
		if (-not $sections.Contains($type)) { continue }

		$description = ($match.Groups['desc'].Value -replace '\s*\(Skip\)\s*', ' ').Trim()
		if ($match.Groups['scope'].Success) {
			$description = "**$($match.Groups['scope'].Value):** $description"
		}
		if ($match.Groups['bang'].Success -or ($index -eq 0 -and $bodyBreaking)) {
			$type = 'breaking'
		}
		$entries[$type].Add("- $description ($($hash.Substring(0, 7)))")
	}
}

$notes = [System.Text.StringBuilder]::new()
foreach ($key in $sections.Keys) {
	if ($entries[$key].Count -eq 0) { continue }
	[void]$notes.AppendLine("### $($sections[$key])").AppendLine()
	foreach ($line in $entries[$key]) { [void]$notes.AppendLine($line) }
	[void]$notes.AppendLine()
}

if ($NotesFile) {
	Set-Content -LiteralPath $NotesFile -Value $notes.ToString() -Encoding utf8
}

$number = $lastNumber + 1
$day = $Date.ToString('yyyy-MM-dd')
[pscustomobject]@{
	LastTag     = $lastReleasedTag
	Number      = $number
	Tag         = "v$number"
	Title       = "v$number ($day)"
	FileVersion = "$($Date.Year).$($Date.Month).$($Date.Day).$number"
	# Added to the ZIP names: ReShade-VR-UI-Addon-OpenXR-v12-2026-10-24.zip.
	# No brackets: GitHub renames uploaded files that contain them.
	AssetSuffix    = "-v$number-$day"
	Changelog   = $notes.ToString()
}
