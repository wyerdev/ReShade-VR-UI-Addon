param(
	[string]$TestRoot = '.deps\openxr-simulator-test',
	[int]$Frames = 30,
	[ValidateSet('Both', 'D3D11', 'D3D12')]
	[string]$GraphicsApi = 'Both',
	[string]$OutputDirectory = 'build\reshade-openxr-ui',
	[switch]$LoaderDebug
)

$ErrorActionPreference = 'Stop'

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not [System.IO.Path]::IsPathRooted($TestRoot)) {
	$TestRoot = [System.IO.Path]::GetFullPath((Join-Path $repo $TestRoot))
}
if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
	$OutputDirectory = [System.IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
}

if ($GraphicsApi -eq 'Both') {
	foreach ($api in @('D3D11', 'D3D12')) {
		& $PSCommandPath -TestRoot $TestRoot -Frames $Frames -GraphicsApi $api -OutputDirectory $OutputDirectory -LoaderDebug:$LoaderDebug
	}
	Write-Host "ReShade VR UI Add-on dual-API smoke passed for $Frames frame(s) per API."
	return
}

$buildDirectory = $OutputDirectory
$sourceDll = Join-Path $buildDirectory 'ReShade-OpenXR-UI.addon64'
$rendererTest = Join-Path $buildDirectory 'ReShade-OpenXR-UI-Renderer-Test.exe'
$sourceManifest = Join-Path $buildDirectory 'layers\ReShade-OpenXR-UI-Layer.json'
$layerDirectory = Join-Path $TestRoot 'layers'
$testReshadeManifest = Join-Path $layerDirectory 'ReShade64_XR_UI_Smoke.json'
$existingReshadeManifest = Join-Path $layerDirectory 'ReShade64_XR.json'
$probeName = if ($GraphicsApi -eq 'D3D11') { 'xr_probe_d3d11.exe' } else { 'xr_probe.exe' }
$probe = Join-Path $TestRoot $probeName
$loader = Join-Path $TestRoot 'openxr_loader.dll'
$runtimeDll = Join-Path $TestRoot 'openxr_simulator.dll'
$runtimeManifest = Join-Path $TestRoot 'openxr_simulator.json'
$reshadeLog = Join-Path $TestRoot 'ReShade.log'
$testLocalAppData = Join-Path $TestRoot 'openxr-ui-smoke-localappdata'
$centralInstallDirectory = Join-Path $testLocalAppData 'ReShade VR UI Add-on'
$centralLayerDirectory = Join-Path $centralInstallDirectory 'openxr-layers'
$testDll = Join-Path $centralInstallDirectory 'ReShade-OpenXR-UI.addon64'
$testManifest = Join-Path $centralLayerDirectory 'ReShade-OpenXR-UI-Layer.json'

foreach ($requiredPath in @($sourceDll, $rendererTest, $sourceManifest, $layerDirectory, $existingReshadeManifest, $probe, $loader, $runtimeDll, $runtimeManifest)) {
	if (-not (Test-Path -LiteralPath $requiredPath)) {
		throw "Required smoke-test path does not exist: $requiredPath"
	}
}

& $rendererTest
if ($LASTEXITCODE -ne 0) {
	throw "Private ImGui renderer test failed with exit code $LASTEXITCODE."
}
if ((Test-Path -LiteralPath $testDll) -or (Test-Path -LiteralPath $testManifest) -or (Test-Path -LiteralPath $testReshadeManifest) -or
	(Test-Path -LiteralPath $testLocalAppData)) {
	throw 'Refusing to overwrite an existing ReShade VR UI Add-on artifact.'
}

$savedLocalAppData = $env:LOCALAPPDATA
$savedRuntime = $env:XR_RUNTIME_JSON
$savedLayerPath = $env:XR_API_LAYER_PATH
$savedEnabledLayers = $env:XR_ENABLE_API_LAYERS
$savedFrames = $env:XR_PROBE_FRAMES
$savedProjectionOnly = $env:XR_PROBE_PROJECTION_ONLY
$savedLoaderDebug = $env:XR_LOADER_DEBUG
$savedDisableCentralReshade = $env:DISABLE_XR_APILAYER_reshade_1
$savedDisableGlobalVrUi = $env:DISABLE_ReShade_VR_UI_Addon
$previousLogHash = if (Test-Path -LiteralPath $reshadeLog) { (Get-FileHash -LiteralPath $reshadeLog -Algorithm SHA256).Hash } else { $null }
$exitCode = -1

try {
	New-Item -ItemType Directory -Path $testLocalAppData | Out-Null
	New-Item -ItemType Directory -Path $centralLayerDirectory | Out-Null
	Copy-Item -LiteralPath $sourceDll -Destination $testDll
	$localOpenXRUILayer = Get-Content -LiteralPath $sourceManifest -Raw | ConvertFrom-Json
	$localOpenXRUILayer.api_layer.name = 'XR_APILAYER_reshade_vr_ui_smoke'
	$localOpenXRUILayer.api_layer.disable_environment = 'DISABLE_XR_APILAYER_reshade_vr_ui_smoke'
	$localOpenXRUILayer | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $testManifest -Encoding UTF8
	$localReshadeLayer = Get-Content -LiteralPath $existingReshadeManifest -Raw | ConvertFrom-Json
	$localReshadeLayer.api_layer.name = 'XR_APILAYER_reshade_local_ui_smoke'
	$localReshadeLayer.api_layer.disable_environment = 'DISABLE_XR_APILAYER_reshade_local_ui_smoke'
	$localReshadeLayer | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $testReshadeManifest -Encoding UTF8
	$env:XR_RUNTIME_JSON = $runtimeManifest
	$env:LOCALAPPDATA = $testLocalAppData
	$env:XR_API_LAYER_PATH = $centralLayerDirectory + ';' + $layerDirectory
	$env:XR_ENABLE_API_LAYERS = 'XR_APILAYER_reshade_vr_ui_smoke;XR_APILAYER_reshade_local_ui_smoke'
	$env:DISABLE_XR_APILAYER_reshade_1 = '1'
	$env:DISABLE_ReShade_VR_UI_Addon = '1'
	$env:XR_PROBE_FRAMES = $Frames.ToString()
	$env:XR_PROBE_PROJECTION_ONLY = '1'
	$env:XR_LOADER_DEBUG = if ($LoaderDebug) { 'all' } else { $null }

	Push-Location $TestRoot
	try {
		& $probe
		$exitCode = $LASTEXITCODE
	}
	finally {
		Pop-Location
	}
}
finally {
	$env:LOCALAPPDATA = $savedLocalAppData
	$env:XR_RUNTIME_JSON = $savedRuntime
	$env:XR_API_LAYER_PATH = $savedLayerPath
	$env:XR_ENABLE_API_LAYERS = $savedEnabledLayers
	$env:XR_PROBE_FRAMES = $savedFrames
	$env:XR_PROBE_PROJECTION_ONLY = $savedProjectionOnly
	$env:XR_LOADER_DEBUG = $savedLoaderDebug
	$env:DISABLE_XR_APILAYER_reshade_1 = $savedDisableCentralReshade
	$env:DISABLE_ReShade_VR_UI_Addon = $savedDisableGlobalVrUi
	Remove-Item -LiteralPath $testDll, $testManifest, $testReshadeManifest -Force -ErrorAction SilentlyContinue
	Remove-Item -LiteralPath $testLocalAppData -Recurse -Force -ErrorAction SilentlyContinue
}

if ($exitCode -ne 0) {
	throw "ReShade VR UI Add-on probe failed with exit code $exitCode."
}

if (-not (Test-Path -LiteralPath $reshadeLog)) {
	throw 'ReShade VR UI Add-on probe did not produce a ReShade log.'
}
$currentLogHash = (Get-FileHash -LiteralPath $reshadeLog -Algorithm SHA256).Hash
if ($currentLogHash -eq $previousLogHash) {
	throw 'ReShade VR UI Add-on probe did not refresh the ReShade log.'
}
$logText = Get-Content -LiteralPath $reshadeLog -Raw
foreach ($expectedMessage in @(
	'native VIEW-space quad initialized with private ImGui technique renderer.',
	"native quad graphics backend: $GraphicsApi.",
	'scrollable technique model ready for controller input.',
	'submitting native XrCompositionLayerQuad in VIEW space.'
)) {
	if (-not $logText.Contains($expectedMessage)) {
		throw "ReShade VR UI Add-on log is missing expected evidence: $expectedMessage"
	}
}

$negativeLocalAppData = Join-Path $TestRoot 'openxr-ui-negative-localappdata'
$negativeInstallDirectory = Join-Path $negativeLocalAppData 'ReShade VR UI Add-on'
$negativeLayerDirectory = Join-Path $negativeInstallDirectory 'openxr-layers'
$negativeDll = Join-Path $negativeInstallDirectory 'ReShade-OpenXR-UI.addon64'
$negativeManifest = Join-Path $negativeLayerDirectory 'ReShade-OpenXR-UI-Layer.json'
$plainProbeDirectory = Join-Path $negativeLocalAppData "plain-$($GraphicsApi.ToLowerInvariant())"
$plainProbe = Join-Path $plainProbeDirectory $probeName
$plainReshadeLog = Join-Path $plainProbeDirectory 'ReShade.log'
$positiveLogHash = (Get-FileHash -LiteralPath $reshadeLog -Algorithm SHA256).Hash
$negativeExitCode = -1
$negativeProducedLog = $false

if (Test-Path -LiteralPath $negativeLocalAppData) {
	throw 'Refusing to overwrite existing no-ReShade smoke-test evidence.'
}

try {
	New-Item -ItemType Directory -Path $negativeLayerDirectory -Force | Out-Null
	New-Item -ItemType Directory -Path $plainProbeDirectory -Force | Out-Null
	Copy-Item -LiteralPath $sourceDll -Destination $negativeDll
	Copy-Item -LiteralPath $probe, $loader, $runtimeDll -Destination $plainProbeDirectory
	$negativeOpenXRUILayer = Get-Content -LiteralPath $sourceManifest -Raw | ConvertFrom-Json
	$negativeOpenXRUILayer.api_layer.name = 'XR_APILAYER_reshade_vr_ui_negative_smoke'
	$negativeOpenXRUILayer.api_layer.disable_environment = 'DISABLE_XR_APILAYER_reshade_vr_ui_negative_smoke'
	$negativeOpenXRUILayer | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $negativeManifest -Encoding UTF8

	$env:LOCALAPPDATA = $negativeLocalAppData
	$env:XR_RUNTIME_JSON = $runtimeManifest
	$env:XR_API_LAYER_PATH = $negativeLayerDirectory
	$env:XR_ENABLE_API_LAYERS = 'XR_APILAYER_reshade_vr_ui_negative_smoke'
	$env:DISABLE_XR_APILAYER_reshade_1 = '1'
	$env:DISABLE_ReShade_VR_UI_Addon = '1'
	$env:XR_PROBE_FRAMES = $Frames.ToString()
	$env:XR_PROBE_PROJECTION_ONLY = '1'
	$env:XR_LOADER_DEBUG = if ($LoaderDebug) { 'all' } else { $null }

	Push-Location $plainProbeDirectory
	try {
		& $plainProbe
		$negativeExitCode = $LASTEXITCODE
	}
	finally {
		Pop-Location
	}
	$negativeProducedLog = Test-Path -LiteralPath $plainReshadeLog
}
finally {
	$env:LOCALAPPDATA = $savedLocalAppData
	$env:XR_RUNTIME_JSON = $savedRuntime
	$env:XR_API_LAYER_PATH = $savedLayerPath
	$env:XR_ENABLE_API_LAYERS = $savedEnabledLayers
	$env:XR_PROBE_FRAMES = $savedFrames
	$env:XR_PROBE_PROJECTION_ONLY = $savedProjectionOnly
	$env:XR_LOADER_DEBUG = $savedLoaderDebug
	$env:DISABLE_XR_APILAYER_reshade_1 = $savedDisableCentralReshade
	$env:DISABLE_ReShade_VR_UI_Addon = $savedDisableGlobalVrUi
	Remove-Item -LiteralPath $negativeLocalAppData -Recurse -Force -ErrorAction SilentlyContinue
}

if ($negativeExitCode -ne 0) {
	throw "No-ReShade OpenXR probe failed with exit code $negativeExitCode."
}
if ($negativeProducedLog) {
	throw 'The globally installed UI add-on activated without ReShade.'
}
if ((Get-FileHash -LiteralPath $reshadeLog -Algorithm SHA256).Hash -ne $positiveLogHash) {
	throw 'The no-ReShade control unexpectedly changed the positive ReShade log.'
}

Write-Host "$GraphicsApi no-ReShade control passed for $Frames frame(s) without UI activation."
Write-Host "ReShade VR UI Add-on $GraphicsApi probe passed for $Frames frame(s)."