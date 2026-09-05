[CmdletBinding()]
param(
    [string]$ReShadePath = '.deps\reshade',

    [string]$OutputDirectory = 'build\reshade-openxr-ui'
)

$ErrorActionPreference = 'Stop'

$buildScript = Join-Path $PSScriptRoot 'tools\reshade-openxr-ui\build.ps1'
if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
    throw "Plugin build script not found: $buildScript"
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript -ReShadePath $ReShadePath -OutputDirectory $OutputDirectory
exit $LASTEXITCODE
