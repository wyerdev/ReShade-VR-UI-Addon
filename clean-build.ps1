[CmdletBinding()]
param(
    [string]$ReShadePath = '.deps\reshade',

    [string]$OutputDirectory = 'build\reshade-openxr-ui'
)

$ErrorActionPreference = 'Stop'

$resolvedOutput = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    [System.IO.Path]::GetFullPath($OutputDirectory)
}
else {
    [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $OutputDirectory))
}
$repositoryRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$expectedPrefix = $repositoryRoot.TrimEnd('\') + '\build\'
if (-not $resolvedOutput.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean outside the standalone repository build directory: $resolvedOutput"
}

Remove-Item -LiteralPath $resolvedOutput -Recurse -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $resolvedOutput) {
    throw "Could not remove build output directory: $resolvedOutput"
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repositoryRoot 'build-openxr-ui.ps1') -ReShadePath $ReShadePath -OutputDirectory $OutputDirectory
exit $LASTEXITCODE
