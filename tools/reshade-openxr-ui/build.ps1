param(
	[string]$ReShadePath = '.deps\reshade',
	[string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([System.IO.Path]::IsPathRooted($ReShadePath)) {
	$reshadeRoot = [System.IO.Path]::GetFullPath($ReShadePath)
}
else {
	$reshadeRoot = [System.IO.Path]::GetFullPath((Join-Path $repo $ReShadePath))
}
if (-not $OutputDirectory) {
	$OutputDirectory = Join-Path $repo 'build\reshade-openxr-ui'
}
elseif (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
	$OutputDirectory = Join-Path $repo $OutputDirectory
}

foreach ($requiredPath in @(
	(Join-Path $reshadeRoot 'include\reshade.hpp'),
	(Join-Path $reshadeRoot 'deps\openxr\include\openxr\openxr.h'),
	(Join-Path $reshadeRoot 'deps\imgui\imgui.cpp'),
	(Join-Path $reshadeRoot 'deps\openvr\headers\openvr.h')
)) {
	if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
		throw "ReShade dependency is missing required content: $requiredPath. Run ..\..\setup-reshade.ps1 -Action Setup or pass -ReShadePath to a complete ReShade checkout."
	}
}

$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vsWhere)) {
	throw 'Visual Studio Installer vswhere.exe was not found.'
}

$installationPath = (& $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1).Trim()
$developerCommand = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $developerCommand)) {
	throw 'Visual Studio C++ developer command prompt was not found.'
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$source = Join-Path $PSScriptRoot 'reshade_openxr_ui_addon.cpp'
$openvrOverlaySource = Join-Path $PSScriptRoot 'openvr_ui_overlay.cpp'
$presetSource = Join-Path $PSScriptRoot 'openxr_ui_preset.cpp'
$layerSource = Join-Path $PSScriptRoot 'openxr_ui_layer.cpp'
$effectFallbackSource = Join-Path $PSScriptRoot 'openxr_effect_fallback.cpp'
$rendererSource = Join-Path $PSScriptRoot 'openxr_ui_renderer.cpp'
$rendererTestSource = Join-Path $PSScriptRoot 'openxr_ui_renderer_test.cpp'
$openvrOverlayProbeSource = Join-Path $PSScriptRoot 'openvr_overlay_probe.cpp'
$manifestSource = Join-Path $PSScriptRoot 'ReShade-OpenXR-UI-Layer.json'
$expectedLayerName = 'XR_APILAYER_reshade_vr_ui'
$manifestDefinition = Get-Content -LiteralPath $manifestSource -Raw | ConvertFrom-Json
if ($manifestDefinition.api_layer.name -cne $expectedLayerName) {
	throw "OpenXR API-layer name '$expectedLayerName' is a stable SteamVR identifier and must not be renamed."
}
$include = Join-Path $reshadeRoot 'include'
$openxrInclude = Join-Path $reshadeRoot 'deps\openxr\include'
$imgui = Join-Path $reshadeRoot 'deps\imgui'
$imguiBackend = Join-Path $imgui 'backends'
$output = Join-Path $OutputDirectory 'ReShade-OpenXR-UI.addon64'
$openvrOutputDirectory = Join-Path $OutputDirectory 'openvr'
$openvrOutput = Join-Path $OutputDirectory 'ReShade-SteamVR-UI.addon64'
$manifestOutputDirectory = Join-Path $OutputDirectory 'layers'
$manifestOutput = Join-Path $manifestOutputDirectory 'ReShade-OpenXR-UI-Layer.json'
$importLibrary = Join-Path $OutputDirectory 'reshade_openxr_ui_addon.lib'
$programDatabase = Join-Path $OutputDirectory 'ReShade-OpenXR-UI.pdb'
$openvrImportLibrary = Join-Path $openvrOutputDirectory 'reshade_steamvr_ui_addon.lib'
$openvrProgramDatabase = Join-Path $openvrOutputDirectory 'ReShade-SteamVR-UI.pdb'
$openvrOverlayProbe = Join-Path $OutputDirectory 'ReShade-OpenVR-Overlay-Probe.exe'
$openvrOverlayProbeProgramDatabase = Join-Path $OutputDirectory 'ReShade-OpenVR-Overlay-Probe.pdb'
$rendererTest = Join-Path $OutputDirectory 'ReShade-OpenXR-UI-Renderer-Test.exe'
$rendererTestProgramDatabase = Join-Path $OutputDirectory 'ReShade-OpenXR-UI-Renderer-Test.pdb'

. (Join-Path $PSScriptRoot '..\release\version-resource.ps1')
$versionResource = New-VersionResource -DeveloperCommand $developerCommand -OutputDirectory $OutputDirectory -FileName 'ReShade-OpenXR-UI.addon64' -Description 'ReShade VR UI Add-on for OpenXR'
$releaseDefine = '/DRESHADE_VR_UI_RELEASE_NUMBER=' + (Get-ReleaseNumber)

$compileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /LD /std:c++17 /EHsc /W4 /O2 /MD ' + $releaseDefine + ' /I"' + $include + '" /I"' + $openxrInclude + '" /I"' + $imgui + '" /I"' + $imguiBackend + '" /Fo:"' + $OutputDirectory + '\\" "' + $versionResource + '" "' + $source + '" "' + $presetSource + '" "' + $layerSource + '" "' + $effectFallbackSource + '" "' + $rendererSource + '" "' + (Join-Path $imgui 'imgui.cpp') + '" "' + (Join-Path $imgui 'imgui_draw.cpp') + '" "' + (Join-Path $imgui 'imgui_tables.cpp') + '" "' + (Join-Path $imgui 'imgui_widgets.cpp') + '" "' + (Join-Path $imguiBackend 'imgui_impl_dx11.cpp') + '" "' + (Join-Path $imguiBackend 'imgui_impl_dx12.cpp') + '" "' + (Join-Path $imguiBackend 'imgui_impl_opengl3.cpp') + '" /link d3d11.lib d3d12.lib dxgi.lib /OUT:"' + $output + '" /IMPLIB:"' + $importLibrary + '" /PDB:"' + $programDatabase + '"'
& cmd.exe /d /c $compileCommand
if ($LASTEXITCODE -ne 0) {
	throw "ReShade VR UI Add-on build failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path $output)) {
	throw "Build completed without producing $output."
}

New-Item -ItemType Directory -Path $openvrOutputDirectory -Force | Out-Null
$openvrVersionResource = New-VersionResource -DeveloperCommand $developerCommand -OutputDirectory $openvrOutputDirectory -FileName 'ReShade-SteamVR-UI.addon64' -Description 'ReShade VR UI Add-on for SteamVR'
$openvrCompileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /LD /std:c++17 /EHsc /W4 /O2 /MD ' + $releaseDefine + ' /DRESHADE_OPENVR_FRONTEND /I"' + $include + '" /I"' + $imgui + '" /I"' + (Join-Path $reshadeRoot 'deps\openvr\headers') + '" /Fo:"' + $openvrOutputDirectory + '\\" "' + $openvrVersionResource + '" "' + $source + '" "' + $openvrOverlaySource + '" "' + $presetSource + '" /link /OUT:"' + $openvrOutput + '" /IMPLIB:"' + $openvrImportLibrary + '" /PDB:"' + $openvrProgramDatabase + '"'
& cmd.exe /d /c $openvrCompileCommand
if ($LASTEXITCODE -ne 0) {
	throw "ReShade VR UI Add-on SteamVR frontend build failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path $openvrOutput)) {
	throw "Build completed without producing $openvrOutput."
}

$openvrOverlayProbeCompileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /std:c++17 /EHsc /W4 /O2 /MD /I"' + (Join-Path $reshadeRoot 'deps\openvr\headers') + '" "' + $openvrOverlayProbeSource + '" /Fe:"' + $openvrOverlayProbe + '" /link /PDB:"' + $openvrOverlayProbeProgramDatabase + '"'
& cmd.exe /d /c $openvrOverlayProbeCompileCommand
if ($LASTEXITCODE -ne 0) {
	throw "OpenVR overlay probe build failed with exit code $LASTEXITCODE."
}

$rendererTestCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /std:c++17 /EHsc /W4 /O2 /MD /I"' + $imgui + '" /I"' + $imguiBackend + '" /Fo:"' + $OutputDirectory + '\\" "' + $rendererTestSource + '" "' + $rendererSource + '" "' + (Join-Path $imgui 'imgui.cpp') + '" "' + (Join-Path $imgui 'imgui_draw.cpp') + '" "' + (Join-Path $imgui 'imgui_tables.cpp') + '" "' + (Join-Path $imgui 'imgui_widgets.cpp') + '" "' + (Join-Path $imguiBackend 'imgui_impl_dx11.cpp') + '" "' + (Join-Path $imguiBackend 'imgui_impl_dx12.cpp') + '" "' + (Join-Path $imguiBackend 'imgui_impl_opengl3.cpp') + '" /link d3d11.lib d3d12.lib dxgi.lib /OUT:"' + $rendererTest + '" /PDB:"' + $rendererTestProgramDatabase + '"'
& cmd.exe /d /c $rendererTestCommand
if ($LASTEXITCODE -ne 0) {
	throw "Renderer test build failed with exit code $LASTEXITCODE."
}

& $rendererTest
if ($LASTEXITCODE -ne 0) {
	throw "Renderer interaction test failed with exit code $LASTEXITCODE."
}

New-Item -ItemType Directory -Path $manifestOutputDirectory -Force | Out-Null
Copy-Item -LiteralPath $manifestSource -Destination $manifestOutput -Force
Write-Host "Built ReShade VR UI Add-on: $output"
Write-Host "Wrote OpenXR API-layer manifest: $manifestOutput"