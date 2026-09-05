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
$rendererSource = Join-Path $PSScriptRoot 'openxr_ui_renderer.cpp'
$rendererTestSource = Join-Path $PSScriptRoot 'openxr_ui_renderer_test.cpp'
$loaderSource = Join-Path $PSScriptRoot 'steamvr_loader.cpp'
$loaderTestTargetSource = Join-Path $PSScriptRoot 'steamvr_loader_test_target.cpp'
$openvrOverlayProbeSource = Join-Path $PSScriptRoot 'openvr_overlay_probe.cpp'
$manifestSource = Join-Path $PSScriptRoot 'ReShade-OpenXR-UI-Layer.json'
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
$loader = Join-Path $OutputDirectory 'ReShade-VR-Loader.exe'
$loaderProgramDatabase = Join-Path $OutputDirectory 'ReShade-VR-Loader.pdb'
$loaderTestTarget = Join-Path $OutputDirectory 'ReShade-VR-Loader-Test-Target.exe'
$loaderTestTargetProgramDatabase = Join-Path $OutputDirectory 'ReShade-VR-Loader-Test-Target.pdb'
$loaderTestMarker = Join-Path $OutputDirectory 'ReShade-VR-Loader-Test-Marker.dll'
$loaderTestMarkerProgramDatabase = Join-Path $OutputDirectory 'ReShade-VR-Loader-Test-Marker.pdb'
$openvrOverlayProbe = Join-Path $OutputDirectory 'ReShade-OpenVR-Overlay-Probe.exe'
$openvrOverlayProbeProgramDatabase = Join-Path $OutputDirectory 'ReShade-OpenVR-Overlay-Probe.pdb'
$rendererTest = Join-Path $OutputDirectory 'ReShade-OpenXR-UI-Renderer-Test.exe'
$rendererTestProgramDatabase = Join-Path $OutputDirectory 'ReShade-OpenXR-UI-Renderer-Test.pdb'

$compileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /LD /std:c++17 /EHsc /W4 /O2 /MD /I"' + $include + '" /I"' + $openxrInclude + '" /I"' + $imgui + '" /I"' + $imguiBackend + '" /Fo:"' + $OutputDirectory + '\\" "' + $source + '" "' + $presetSource + '" "' + $layerSource + '" "' + $rendererSource + '" "' + (Join-Path $imgui 'imgui.cpp') + '" "' + (Join-Path $imgui 'imgui_draw.cpp') + '" "' + (Join-Path $imgui 'imgui_tables.cpp') + '" "' + (Join-Path $imgui 'imgui_widgets.cpp') + '" "' + (Join-Path $imguiBackend 'imgui_impl_dx11.cpp') + '" "' + (Join-Path $imguiBackend 'imgui_impl_dx12.cpp') + '" /link d3d11.lib d3d12.lib dxgi.lib /OUT:"' + $output + '" /IMPLIB:"' + $importLibrary + '" /PDB:"' + $programDatabase + '"'
& cmd.exe /d /c $compileCommand
if ($LASTEXITCODE -ne 0) {
	throw "ReShade VR UI Add-on build failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path $output)) {
	throw "Build completed without producing $output."
}

New-Item -ItemType Directory -Path $openvrOutputDirectory -Force | Out-Null
$openvrCompileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /LD /std:c++17 /EHsc /W4 /O2 /MD /DRESHADE_OPENVR_FRONTEND /I"' + $include + '" /I"' + $imgui + '" /I"' + (Join-Path $reshadeRoot 'deps\openvr\headers') + '" /Fo:"' + $openvrOutputDirectory + '\\" "' + $source + '" "' + $openvrOverlaySource + '" "' + $presetSource + '" /link /OUT:"' + $openvrOutput + '" /IMPLIB:"' + $openvrImportLibrary + '" /PDB:"' + $openvrProgramDatabase + '"'
& cmd.exe /d /c $openvrCompileCommand
if ($LASTEXITCODE -ne 0) {
	throw "ReShade VR UI Add-on SteamVR frontend build failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path $openvrOutput)) {
	throw "Build completed without producing $openvrOutput."
}

$loaderCompileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /std:c++17 /EHsc /W4 /O2 /MD "' + $loaderSource + '" /Fe:"' + $loader + '" /link /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup /PDB:"' + $loaderProgramDatabase + '"'
& cmd.exe /d /c $loaderCompileCommand
if ($LASTEXITCODE -ne 0) {
	throw "ReShade VR loader build failed with exit code $LASTEXITCODE."
}

$loaderTestTargetCompileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /std:c++17 /EHsc /W4 /O2 /MD "' + $loaderTestTargetSource + '" /Fe:"' + $loaderTestTarget + '" /link /PDB:"' + $loaderTestTargetProgramDatabase + '"'
& cmd.exe /d /c $loaderTestTargetCompileCommand
if ($LASTEXITCODE -ne 0) {
	throw "ReShade VR loader test target build failed with exit code $LASTEXITCODE."
}

$loaderTestMarkerCompileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /LD /std:c++17 /EHsc /W4 /O2 /MD /DSTEAMVR_LOADER_TEST_MARKER "' + $loaderTestTargetSource + '" /Fe:"' + $loaderTestMarker + '" /link /PDB:"' + $loaderTestMarkerProgramDatabase + '"'
& cmd.exe /d /c $loaderTestMarkerCompileCommand
if ($LASTEXITCODE -ne 0) {
	throw "ReShade VR loader test marker build failed with exit code $LASTEXITCODE."
}

$openvrOverlayProbeCompileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /std:c++17 /EHsc /W4 /O2 /MD /I"' + (Join-Path $reshadeRoot 'deps\openvr\headers') + '" "' + $openvrOverlayProbeSource + '" /Fe:"' + $openvrOverlayProbe + '" /link /PDB:"' + $openvrOverlayProbeProgramDatabase + '"'
& cmd.exe /d /c $openvrOverlayProbeCompileCommand
if ($LASTEXITCODE -ne 0) {
	throw "OpenVR overlay probe build failed with exit code $LASTEXITCODE."
}

$loaderTestDirectory = Join-Path $OutputDirectory 'loader-test'
$loaderTestAllowlist = Join-Path $loaderTestDirectory 'ReShadeApps.ini'
$loaderTestOpenVRMarker = Join-Path $loaderTestDirectory 'openvr_api.dll'
$loaderTestOpenXRMarker = Join-Path $loaderTestDirectory 'openxr_loader.dll'
Remove-Item -LiteralPath $loaderTestDirectory -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $loaderTestDirectory -Force | Out-Null
Set-Content -LiteralPath $loaderTestAllowlist -Value "Apps=$loaderTestTarget" -Encoding UTF8
Copy-Item -LiteralPath $loaderTestMarker -Destination $loaderTestOpenVRMarker
Copy-Item -LiteralPath $loaderTestMarker -Destination $loaderTestOpenXRMarker
$loaderProcess = Start-Process -FilePath $loader -ArgumentList @('--test', "`"$loaderTestAllowlist`"", "`"$output`"", "`"$openvrOutput`"", '--once') -PassThru
$loaderTestStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$targetProcess = Start-Process -FilePath $loaderTestTarget -ArgumentList @('ReShade-OpenXR-UI.addon64', 'ReShade-SteamVR-UI.addon64', "`"$loaderTestOpenVRMarker`"") -PassThru
if (-not $targetProcess.WaitForExit(10000) -or $targetProcess.ExitCode -ne 0) {
	if (-not $targetProcess.HasExited) { $targetProcess.Kill() }
	if (-not $loaderProcess.HasExited) { $loaderProcess.Kill() }
	throw "Central loader test target did not observe both injected modules."
}
if (-not $loaderProcess.WaitForExit(10000) -or $loaderProcess.ExitCode -ne 0) {
	if (-not $loaderProcess.HasExited) { $loaderProcess.Kill() }
	throw "Central loader did not complete its allowlisted injection test."
}
Write-Host "Central allowlist loader injected both modules after the synthetic target selected OpenVR ($($loaderTestStopwatch.ElapsedMilliseconds) ms observed)."

$loaderProcess = Start-Process -FilePath $loader -ArgumentList @('--test', "`"$loaderTestAllowlist`"", "`"$output`"", "`"$openvrOutput`"", '--once') -PassThru
$targetProcess = Start-Process -FilePath $loaderTestTarget -ArgumentList @('ReShade-OpenXR-UI.addon64', 'ReShade-SteamVR-UI.addon64', "`"$loaderTestOpenXRMarker`"", '--expect-absent') -PassThru
if (-not $targetProcess.WaitForExit(10000) -or $targetProcess.ExitCode -ne 0) {
	if (-not $targetProcess.HasExited) { $targetProcess.Kill() }
	if (-not $loaderProcess.HasExited) { $loaderProcess.Kill() }
	throw 'Central loader injected SteamVR modules into the synthetic OpenXR target.'
}
if (-not $loaderProcess.WaitForExit(10000) -or $loaderProcess.ExitCode -ne 0) {
	if (-not $loaderProcess.HasExited) { $loaderProcess.Kill() }
	throw 'Central loader did not resolve the synthetic OpenXR target.'
}
Remove-Item -LiteralPath $loaderTestDirectory -Recurse -Force
Write-Host 'Central allowlist loader left the synthetic OpenXR target to the OpenXR API layer.'

$centralReShade = Join-Path $env:ProgramData 'ReShade\ReShade64.dll'
if (Test-Path -LiteralPath $centralReShade -PathType Leaf) {
	$centralTestDirectory = Join-Path $OutputDirectory 'central-reshade-loader-test'
	$centralTestTarget = Join-Path $centralTestDirectory 'CentralReShadeLoaderTarget.exe'
	$centralTestAllowlist = Join-Path $centralTestDirectory 'ReShadeApps.ini'
	$centralTestConfig = Join-Path $centralTestDirectory 'ReShade.ini'
	$centralTestLog = Join-Path $centralTestDirectory 'ReShade.log'
	$centralTestOpenVRMarker = Join-Path $centralTestDirectory 'openvr_api.dll'
	Remove-Item -LiteralPath $centralTestDirectory -Recurse -Force -ErrorAction SilentlyContinue
	New-Item -ItemType Directory -Path $centralTestDirectory -Force | Out-Null
	Copy-Item -LiteralPath $loaderTestTarget -Destination $centralTestTarget
	Copy-Item -LiteralPath $loaderTestMarker -Destination $centralTestOpenVRMarker
	Set-Content -LiteralPath $centralTestAllowlist -Value "Apps=$centralTestTarget" -Encoding UTF8
	Set-Content -LiteralPath $centralTestConfig -Value "[GENERAL]`r`nPerformanceMode=0" -Encoding ASCII
	$centralLoaderProcess = Start-Process -FilePath $loader -ArgumentList @('--test', "`"$centralTestAllowlist`"", "`"$centralReShade`"", "`"$openvrOutput`"", '--once') -PassThru
	$centralTargetProcess = Start-Process -FilePath $centralTestTarget -ArgumentList @('ReShade64.dll', 'ReShade-SteamVR-UI.addon64', "`"$centralTestOpenVRMarker`"") -PassThru
	if (-not $centralTargetProcess.WaitForExit(10000) -or $centralTargetProcess.ExitCode -ne 0) {
		if (-not $centralTargetProcess.HasExited) { $centralTargetProcess.Kill() }
		if (-not $centralLoaderProcess.HasExited) { $centralLoaderProcess.Kill() }
		throw 'Actual central ReShade loader test target did not observe both modules.'
	}
	if (-not $centralLoaderProcess.WaitForExit(10000) -or $centralLoaderProcess.ExitCode -ne 0) {
		if (-not $centralLoaderProcess.HasExited) { $centralLoaderProcess.Kill() }
		throw 'Actual central ReShade loader test did not complete.'
	}
	if (-not (Test-Path -LiteralPath $centralTestLog -PathType Leaf)) {
		throw 'Actual central ReShade injection did not create ReShade.log.'
	}
	$centralLogContent = Get-Content -LiteralPath $centralTestLog -Raw
	if ($centralLogContent -notmatch 'Initialized\.' -or $centralLogContent -notmatch 'Registered add-on "ReShade VR UI Add-on for SteamVR"') {
		throw 'Actual central ReShade injection did not initialize and register the SteamVR UI companion.'
	}
	Remove-Item -LiteralPath $centralTestDirectory -Recurse -Force
	Write-Host 'Actual central ReShade initialized and registered the SteamVR UI companion in the isolated target.'
}

$rendererTestCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /std:c++17 /EHsc /W4 /O2 /MD /I"' + $imgui + '" /I"' + $imguiBackend + '" /Fo:"' + $OutputDirectory + '\\" "' + $rendererTestSource + '" "' + $rendererSource + '" "' + (Join-Path $imgui 'imgui.cpp') + '" "' + (Join-Path $imgui 'imgui_draw.cpp') + '" "' + (Join-Path $imgui 'imgui_tables.cpp') + '" "' + (Join-Path $imgui 'imgui_widgets.cpp') + '" "' + (Join-Path $imguiBackend 'imgui_impl_dx11.cpp') + '" "' + (Join-Path $imguiBackend 'imgui_impl_dx12.cpp') + '" /link d3d11.lib d3d12.lib dxgi.lib /OUT:"' + $rendererTest + '" /PDB:"' + $rendererTestProgramDatabase + '"'
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