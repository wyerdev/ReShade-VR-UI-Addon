param(
	[Parameter(Mandatory = $true)][string]$TestRoot,
	[string]$SimulatorSource,
	[string]$OutputDirectory = 'build\reshade-openxr-ui\vulkan-probe'
)

# Builds the OpenXR Simulator's own Vulkan test program (probe\xr_probe_vk.cpp in its checkout) for the Vulkan smoke test.
# The Vulkan SDK is not needed: the link libraries are made from the Vulkan and OpenXR loader DLLs already on this machine.
$ErrorActionPreference = 'Stop'

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $SimulatorSource) {
	$SimulatorSource = Join-Path $repo '..\OpenXR-Simulator'
}
$resolve = { param($path) if ([System.IO.Path]::IsPathRooted($path)) { [System.IO.Path]::GetFullPath($path) } else { [System.IO.Path]::GetFullPath((Join-Path $repo $path)) } }
$TestRoot = & $resolve $TestRoot
$SimulatorSource = & $resolve $SimulatorSource
$OutputDirectory = & $resolve $OutputDirectory

$probeSource = Join-Path $SimulatorSource 'probe\xr_probe_vk.cpp'
$vulkanInclude = Join-Path $repo '.deps\Vulkan-Headers\include'
$openxrInclude = Join-Path $SimulatorSource 'include'
$vulkanLoader = Join-Path $env:WINDIR 'System32\vulkan-1.dll'
$openxrLoader = Join-Path $TestRoot 'openxr_loader.dll'
$probe = Join-Path $OutputDirectory 'xr_probe_vk.exe'
foreach ($requiredPath in @($probeSource, (Join-Path $vulkanInclude 'vulkan\vulkan.h'), (Join-Path $openxrInclude 'openxr\openxr.h'), $vulkanLoader, $openxrLoader)) {
	if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
		throw "Required Vulkan probe input does not exist: $requiredPath"
	}
}
if ((Test-Path -LiteralPath $probe -PathType Leaf) -and (Get-Item -LiteralPath $probe).LastWriteTimeUtc -gt (Get-Item -LiteralPath $probeSource).LastWriteTimeUtc) {
	return $probe
}

$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = (& $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1).Trim()
$developerCommand = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$libraries = foreach ($loader in @($vulkanLoader, $openxrLoader)) {
	$name = [System.IO.Path]::GetFileNameWithoutExtension($loader)
	$definition = Join-Path $OutputDirectory "$name.def"
	$library = Join-Path $OutputDirectory "$name.lib"
	$exports = & cmd.exe /d /c ('call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & dumpbin.exe /nologo /exports "' + $loader + '"') |
		ForEach-Object { if ($_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)') { $Matches[1] } }
	if (-not $exports) {
		throw "Could not read the exported functions of $loader."
	}
	Set-Content -LiteralPath $definition -Value (@("LIBRARY $name", 'EXPORTS') + $exports) -Encoding ASCII
	& cmd.exe /d /c ('call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & lib.exe /nologo /def:"' + $definition + '" /machine:x64 /out:"' + $library + '"') | Out-Null
	if ($LASTEXITCODE -ne 0) {
		throw "Could not make a link library for $loader."
	}
	$library
}

# The probe names the OpenXR 1.0 version the way newer OpenXR headers do; the simulator's bundled headers predate that name.
$compile = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /std:c++17 /EHsc /O2 /MD /D_CRT_SECURE_NO_WARNINGS' +
	' /D"XR_API_VERSION_1_0=XR_MAKE_VERSION(1,0,0)"' +
	' /I"' + $vulkanInclude + '" /I"' + $openxrInclude + '" /Fo:"' + $OutputDirectory + '\\" "' + $probeSource + '" /Fe:"' + $probe + '" /link ' +
	(($libraries | ForEach-Object { '"' + $_ + '"' }) -join ' ')
& cmd.exe /d /c $compile | Out-Host
if ($LASTEXITCODE -ne 0) {
	throw "Vulkan probe build failed with exit code $LASTEXITCODE."
}
return $probe
