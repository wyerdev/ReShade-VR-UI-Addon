[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [string]$GameDirectory,

    [switch]$Force
)

$ErrorActionPreference = 'Stop'

function Get-IniValue {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Section,
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)][string]$DefaultValue
    )

    $currentSection = ''
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmedLine = $line.Trim()
        if ($trimmedLine -match '^\[(.+)\]$') {
            $currentSection = $Matches[1]
            continue
        }

        if ($currentSection -eq $Section -and $trimmedLine -match '^([^=]+)=(.*)$' -and $Matches[1].Trim() -eq $Key) {
            return $Matches[2].Trim()
        }
    }

    return $DefaultValue
}

$resolvedGameDirectory = [System.IO.Path]::GetFullPath($GameDirectory).TrimEnd('\')
if (($resolvedGameDirectory -split '[\\/]') -contains 'ourReshade') {
    throw "Refusing to write inside the protected ourReshade backup: $resolvedGameDirectory"
}
if (-not (Test-Path -LiteralPath $resolvedGameDirectory -PathType Container)) {
    throw "Game directory not found: $resolvedGameDirectory"
}

$sourcePath = Join-Path $resolvedGameDirectory 'ReShade.ini'
$destinationPath = Join-Path $resolvedGameDirectory 'ReShadeVR.ini'
$cachePath = Join-Path $resolvedGameDirectory 'ReShadeCache'

if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "Source configuration not found: $sourcePath"
}
if ((Test-Path -LiteralPath $destinationPath -PathType Leaf) -and -not $Force) {
    throw "Destination already exists: $destinationPath. Use -Force to replace it."
}

$effectSearchPaths = Get-IniValue -Path $sourcePath -Section 'GENERAL' -Key 'EffectSearchPaths' -DefaultValue '.\'
$textureSearchPaths = Get-IniValue -Path $sourcePath -Section 'GENERAL' -Key 'TextureSearchPaths' -DefaultValue '.\'
$presetPath = Get-IniValue -Path $sourcePath -Section 'GENERAL' -Key 'PresetPath' -DefaultValue '.\ReShadePreset.ini'

$content = @"
[GENERAL]
EffectSearchPaths=$effectSearchPaths
IntermediateCachePath=.\ReShadeCache
PresetPath=$presetPath
TextureSearchPaths=$textureSearchPaths

[INPUT]
KeyOverlay=36,0,0,0

[STYLE]
FontSize=12.000000
FontScale=1.000000
"@

if ($PSCmdlet.ShouldProcess($destinationPath, 'Generate portable ReShade VR configuration')) {
    New-Item -ItemType Directory -Path $cachePath -Force | Out-Null
    $content | Set-Content -LiteralPath $destinationPath -Encoding ASCII
    Write-Host "Generated: $destinationPath"
    Write-Host "Derived from: $sourcePath"
}