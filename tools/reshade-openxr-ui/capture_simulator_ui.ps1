[CmdletBinding()]
param(
	[string]$TestRoot = '.deps\openxr-simulator-test',
	[int]$Frames = 1600,
	[ValidateSet('Both', 'D3D11', 'D3D12')]
	[string]$GraphicsApi = 'Both',
	[string]$OutputDirectory = 'build\reshade-openxr-ui',
	[switch]$RevertRestoreOnly
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
		& $PSCommandPath -TestRoot $TestRoot -Frames $Frames -GraphicsApi $api -OutputDirectory $OutputDirectory -RevertRestoreOnly:$RevertRestoreOnly
	}
	Write-Host "Saved D3D11 and D3D12 simulator UI captures."
	return
}

$buildDirectory = $OutputDirectory
$sourceDll = Join-Path $buildDirectory 'ReShade-OpenXR-UI.addon64'
$sourceManifest = Join-Path $buildDirectory 'layers\ReShade-OpenXR-UI-Layer.json'
$layerDirectory = Join-Path $TestRoot 'layers'
$testReshadeManifest = Join-Path $layerDirectory 'ReShade64_XR_UI_Capture.json'
$existingReshadeManifest = Join-Path $layerDirectory 'ReShade64_XR.json'
$probeName = if ($GraphicsApi -eq 'D3D11') { 'xr_probe_d3d11.exe' } else { 'xr_probe.exe' }
$probe = Join-Path $TestRoot $probeName
$probeFrames = $Frames + 800
$runtimeManifest = Join-Path $TestRoot 'openxr_simulator.json'
$outputDirectory = Join-Path $buildDirectory "simulator-captures\$($GraphicsApi.ToLowerInvariant())"
$testLocalAppData = Join-Path $TestRoot 'openxr-ui-capture-localappdata'
$testInstallDirectory = Join-Path $testLocalAppData 'ReShade VR UI Add-on'
$testLayerDirectory = Join-Path $testInstallDirectory 'openxr-layers'
$testDll = Join-Path $testInstallDirectory 'ReShade-OpenXR-UI.addon64'
$testManifest = Join-Path $testLayerDirectory 'ReShade-OpenXR-UI-Layer-Capture.json'
$simulatorDataDirectory = Join-Path $testLocalAppData 'OpenXR-Simulator'
$runtimeStatusPath = Join-Path $simulatorDataDirectory 'runtime_status.json'
$controllerCommandPath = Join-Path $simulatorDataDirectory 'controller_pose_command.json'
$stdoutPath = Join-Path $outputDirectory 'probe.stdout.txt'
$stderrPath = Join-Path $outputDirectory 'probe.stderr.txt'
$vrConfigPath = Join-Path $TestRoot 'ReShadeVR.ini'
$sourcePresetPath = Join-Path $TestRoot 'OpenXR-Effect-Off.ini'
$createdPresetPath = Join-Path $TestRoot 'HI.ini'

foreach ($requiredPath in @($sourceDll, $sourceManifest, $layerDirectory, $existingReshadeManifest, $probe, $runtimeManifest)) {
	if (-not (Test-Path -LiteralPath $requiredPath)) {
		throw "Required simulator capture path does not exist: $requiredPath"
	}
}
if ((Test-Path -LiteralPath $testLocalAppData) -and
	-not (Test-Path -LiteralPath $testDll) -and -not (Test-Path -LiteralPath $testManifest) -and
	-not (Test-Path -LiteralPath $testReshadeManifest) -and -not (Test-Path -LiteralPath $createdPresetPath)) {
	Remove-Item -LiteralPath $testLocalAppData -Recurse -Force
}
if ((Test-Path -LiteralPath $testDll) -or (Test-Path -LiteralPath $testManifest) -or
	(Test-Path -LiteralPath $testReshadeManifest) -or (Test-Path -LiteralPath $testLocalAppData) -or
	(Test-Path -LiteralPath $createdPresetPath)) {
	throw 'Refusing to overwrite an existing simulator capture artifact.'
}
$savedVrConfig = [System.IO.File]::ReadAllBytes($vrConfigPath)
$savedSourcePreset = [System.IO.File]::ReadAllBytes($sourcePresetPath)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class SimulatorWindow
{
	public delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

	[StructLayout(LayoutKind.Sequential)]
	public struct Rect { public int Left, Top, Right, Bottom; }

	[DllImport("user32.dll")]
	private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);
	[DllImport("user32.dll")]
	private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
	[DllImport("user32.dll")]
	private static extern int GetWindowText(IntPtr window, StringBuilder text, int maximumCount);
	[DllImport("user32.dll")]
	private static extern bool IsWindowVisible(IntPtr window);
	[DllImport("user32.dll")]
	public static extern bool GetWindowRect(IntPtr window, out Rect rect);
	[DllImport("user32.dll")]
	public static extern bool SetForegroundWindow(IntPtr window);
	[DllImport("user32.dll")]
	public static extern bool PrintWindow(IntPtr window, IntPtr deviceContext, uint flags);

	public static IntPtr Find(int processId)
	{
		IntPtr result = IntPtr.Zero;
		EnumWindows((window, parameter) => {
			uint owner;
			GetWindowThreadProcessId(window, out owner);
			if (owner != processId || !IsWindowVisible(window)) return true;
			var title = new StringBuilder(256);
			GetWindowText(window, title, title.Capacity);
			if (title.ToString().IndexOf("OpenXR Simulator", StringComparison.OrdinalIgnoreCase) < 0) return true;
			result = window;
			return false;
		}, IntPtr.Zero);
		return result;
	}
}
'@

function Get-FrameCount {
	if (-not (Test-Path -LiteralPath $runtimeStatusPath -PathType Leaf)) { return 0 }
	try { return [int](Get-Content -LiteralPath $runtimeStatusPath -Raw | ConvertFrom-Json).frame_count }
	catch { return 0 }
}

function Wait-ForFrame {
	param([Parameter(Mandatory = $true)][int]$Frame)
	while (-not $process.WaitForExit(50)) {
		if ((Get-FrameCount) -ge $Frame) { return }
	}
	throw "OpenXR probe exited before frame $Frame."
}

function Set-ControllerPointer {
	param(
		[Parameter(Mandatory = $true)][float]$X,
		[Parameter(Mandatory = $true)][float]$Y,
		[Parameter(Mandatory = $true)][float]$Trigger,
		[switch]$Pinned
	)
	$hitX = ($X / 1024.0 - 0.5) * 0.96
	$panelWorldHeight = 0.96 * 650.0 / 1024.0
	$hitY = -0.08 + (0.5 - $Y / 650.0) * $panelWorldHeight
	# The simulator reports one controller pose for every base space; the pinned hit test subtracts the 1.7 m head height.
	$headHeight = if ($Pinned) { 0.0 } else { 1.7 }
	[ordered]@{
		hand = 1
		posX = $hitX
		posY = $hitY - $headHeight
		posZ = -0.4
		yaw = 0.0
		pitch = 0.0
		trigger = $Trigger
	} | ConvertTo-Json | Set-Content -LiteralPath $controllerCommandPath -Encoding ASCII
}

function Invoke-ControllerClick {
	param([Parameter(Mandatory = $true)][float]$X, [Parameter(Mandatory = $true)][float]$Y, [switch]$Pinned)
	$frame = Get-FrameCount
	Set-ControllerPointer -X $X -Y $Y -Trigger 0.0 -Pinned:$Pinned
	Wait-ForFrame ($frame + 60)
	Set-ControllerPointer -X $X -Y $Y -Trigger 1.0 -Pinned:$Pinned
	Wait-ForFrame ($frame + 65)
	Set-ControllerPointer -X $X -Y $Y -Trigger 0.0 -Pinned:$Pinned
	Wait-ForFrame ($frame + 70)
}

function Save-SimulatorWindow {
	param([Parameter(Mandatory = $true)][string]$Path)
	$window = [SimulatorWindow]::Find($process.Id)
	if ($window -eq [IntPtr]::Zero) { throw 'The OpenXR Simulator preview window was not found.' }
	$rect = New-Object SimulatorWindow+Rect
	if (-not [SimulatorWindow]::GetWindowRect($window, [ref]$rect)) { throw 'Could not read the simulator window bounds.' }
	$width = $rect.Right - $rect.Left
	$height = $rect.Bottom - $rect.Top
	for ($attempt = 0; $attempt -lt 10; ++$attempt) {
		$bitmap = New-Object System.Drawing.Bitmap($width, $height)
		$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
		$deviceContext = $graphics.GetHdc()
		try {
			if (-not [SimulatorWindow]::PrintWindow($window, $deviceContext, 2)) { throw 'PrintWindow failed for the simulator preview.' }
		}
		finally {
			$graphics.ReleaseHdc($deviceContext)
			$graphics.Dispose()
		}

		$hasScenePixels = $false
		for ($sampleY = 80; $sampleY -lt $height -and -not $hasScenePixels; $sampleY += [Math]::Max(1, [int](($height - 80) / 8))) {
			for ($sampleX = 0; $sampleX -lt $width; $sampleX += [Math]::Max(1, [int]($width / 8))) {
				$pixel = $bitmap.GetPixel($sampleX, $sampleY)
				if ([int]$pixel.R + [int]$pixel.G + [int]$pixel.B -gt 24) {
					$hasScenePixels = $true
					break
				}
			}
		}
		if ($hasScenePixels) {
			$bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
			$bitmap.Dispose()
			return
		}

		$bitmap.Dispose()
		Wait-ForFrame ((Get-FrameCount) + 15)
	}
	throw 'The OpenXR Simulator preview remained blank across ten capture attempts.'
}

$savedEnvironment = @{
	LOCALAPPDATA = $env:LOCALAPPDATA
	XR_RUNTIME_JSON = $env:XR_RUNTIME_JSON
	XR_API_LAYER_PATH = $env:XR_API_LAYER_PATH
	XR_ENABLE_API_LAYERS = $env:XR_ENABLE_API_LAYERS
	XR_PROBE_FRAMES = $env:XR_PROBE_FRAMES
	XR_PROBE_SLEEP_MS = $env:XR_PROBE_SLEEP_MS
	XR_PROBE_PROJECTION_ONLY = $env:XR_PROBE_PROJECTION_ONLY
	DISABLE_XR_APILAYER_reshade_1 = $env:DISABLE_XR_APILAYER_reshade_1
	DISABLE_XR_APILAYER_reshade_vr_ui = $env:DISABLE_XR_APILAYER_reshade_vr_ui
	DISABLE_ReShade_VR_UI_Addon = $env:DISABLE_ReShade_VR_UI_Addon
}
$process = $null

try {
	New-Item -ItemType Directory -Path $outputDirectory, $testLayerDirectory, $simulatorDataDirectory -Force | Out-Null
	Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
	Copy-Item -LiteralPath $sourceDll -Destination $testDll

	$openXRUILayer = Get-Content -LiteralPath $sourceManifest -Raw | ConvertFrom-Json
	$openXRUILayer.api_layer.name = 'XR_APILAYER_reshade_vr_ui_capture'
	$openXRUILayer.api_layer.disable_environment = 'DISABLE_XR_APILAYER_reshade_vr_ui_capture'
	$openXRUILayer | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $testManifest -Encoding UTF8
	$reshadeLayer = Get-Content -LiteralPath $existingReshadeManifest -Raw | ConvertFrom-Json
	$reshadeLayer.api_layer.name = 'XR_APILAYER_reshade_local_ui_capture'
	$reshadeLayer.api_layer.disable_environment = 'DISABLE_XR_APILAYER_reshade_local_ui_capture'
	$reshadeLayer | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $testReshadeManifest -Encoding UTF8

	[ordered]@{
		view_mode = 'left'
		zoom_mode = 'fill'
		show_stats = $false
		window_width = 1920
		window_height = 1080
		preview_fps = 90
	} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $simulatorDataDirectory 'settings.json') -Encoding ASCII

	$env:LOCALAPPDATA = $testLocalAppData
	$env:XR_RUNTIME_JSON = $runtimeManifest
	$env:XR_API_LAYER_PATH = $testLayerDirectory + ';' + $layerDirectory
	$env:XR_ENABLE_API_LAYERS = 'XR_APILAYER_reshade_vr_ui_capture;XR_APILAYER_reshade_local_ui_capture'
	$env:DISABLE_XR_APILAYER_reshade_1 = '1'
	$env:DISABLE_XR_APILAYER_reshade_vr_ui = '1'
	$env:DISABLE_ReShade_VR_UI_Addon = '1'
	$env:XR_PROBE_FRAMES = $probeFrames.ToString()
	$env:XR_PROBE_SLEEP_MS = '10'
	$env:XR_PROBE_PROJECTION_ONLY = '1'

	$process = Start-Process -FilePath $probe -WorkingDirectory $TestRoot -PassThru `
		-RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
	$processHandle = $process.Handle
	Wait-ForFrame 60
	if ($RevertRestoreOnly) {
		Invoke-ControllerClick -X 900 -Y 145
		Wait-ForFrame ((Get-FrameCount) + 15)
		Save-SimulatorWindow (Join-Path $outputDirectory 'revert-restore-detail.png')

		Invoke-ControllerClick -X 520 -Y 32
		Wait-ForFrame ((Get-FrameCount) + 15)
		Save-SimulatorWindow (Join-Path $outputDirectory 'revert-restore-edited.png')
		Invoke-ControllerClick -X 735 -Y 32
		Wait-ForFrame ((Get-FrameCount) + 15)
		Save-SimulatorWindow (Join-Path $outputDirectory 'revert-restore-reverted.png')
		Invoke-ControllerClick -X 830 -Y 32
		Wait-ForFrame ((Get-FrameCount) + 15)
		Save-SimulatorWindow (Join-Path $outputDirectory 'revert-restore-restored.png')

		$process.WaitForExit()
		$process.Refresh()
		if ($process.ExitCode -ne 0) { throw "OpenXR probe failed with exit code $($process.ExitCode)." }
		$probeOutput = Get-Content -LiteralPath $stdoutPath -Raw
		if ($probeOutput -notmatch '=+ 0 failure\(s\), [0-9]+ warning\(s\) =+') {
			throw "OpenXR probe did not report zero failures. See '$stdoutPath'."
		}
		Write-Host "Saved $GraphicsApi Revert/Restore simulator captures to $outputDirectory"
		return
	}
	Save-SimulatorWindow (Join-Path $outputDirectory 'main.png')
	Invoke-ControllerClick -X 780 -Y 32
	Wait-ForFrame ((Get-FrameCount) + 15)
	Save-SimulatorWindow (Join-Path $outputDirectory 'pinned.png')
	Invoke-ControllerClick -X 780 -Y 32 -Pinned
	Wait-ForFrame ((Get-FrameCount) + 15)
	Save-SimulatorWindow (Join-Path $outputDirectory 'preset-off.png')

	Invoke-ControllerClick -X 629 -Y 102
	Wait-ForFrame ((Get-FrameCount) + 15)
	Save-SimulatorWindow (Join-Path $outputDirectory 'preset-on.png')
	Invoke-ControllerClick -X 587 -Y 102
	Wait-ForFrame ((Get-FrameCount) + 15)
	Save-SimulatorWindow (Join-Path $outputDirectory 'preset-off-again.png')

	Invoke-ControllerClick -X 900 -Y 145
	Wait-ForFrame ((Get-FrameCount) + 15)
	Save-SimulatorWindow (Join-Path $outputDirectory 'detail.png')

	Invoke-ControllerClick -X 520 -Y 32
	Wait-ForFrame ((Get-FrameCount) + 15)
	Save-SimulatorWindow (Join-Path $outputDirectory 'detail-edited.png')
	Invoke-ControllerClick -X 735 -Y 32
	Wait-ForFrame ((Get-FrameCount) + 15)
	Save-SimulatorWindow (Join-Path $outputDirectory 'detail-reverted.png')
	Invoke-ControllerClick -X 830 -Y 32
	Wait-ForFrame ((Get-FrameCount) + 15)
	Save-SimulatorWindow (Join-Path $outputDirectory 'detail-restored.png')

	Invoke-ControllerClick -X 50 -Y 32
	Invoke-ControllerClick -X 827 -Y 102
	Wait-ForFrame ((Get-FrameCount) + 15)
	Save-SimulatorWindow (Join-Path $outputDirectory 'save-as.png')

	Invoke-ControllerClick -X 720 -Y 387
	Invoke-ControllerClick -X 604 -Y 279
	Invoke-ControllerClick -X 742 -Y 229
	Invoke-ControllerClick -X 920 -Y 453
	Wait-ForFrame ((Get-FrameCount) + 10)
	Save-SimulatorWindow (Join-Path $outputDirectory 'creating.png')
	Wait-ForFrame ((Get-FrameCount) + 150)
	Save-SimulatorWindow (Join-Path $outputDirectory 'created.png')

	Invoke-ControllerClick -X 960 -Y 102
	Wait-ForFrame ((Get-FrameCount) + 130)
	if (Test-Path -LiteralPath $createdPresetPath) {
		throw "Deleted preset was recreated after ReShade flushed its preset cache: $createdPresetPath"
	}
	Invoke-ControllerClick -X 587 -Y 102
	Wait-ForFrame ((Get-FrameCount) + 10)
	if (Test-Path -LiteralPath $createdPresetPath) {
		throw "Deleted preset reappeared after backward preset navigation: $createdPresetPath"
	}

	$process.WaitForExit()
	$process.Refresh()
	if ($process.ExitCode -ne 0) { throw "OpenXR probe failed with exit code $($process.ExitCode)." }
	$probeOutput = Get-Content -LiteralPath $stdoutPath -Raw
	if ($probeOutput -notmatch '=+ 0 failure\(s\), [0-9]+ warning\(s\) =+') {
		throw "OpenXR probe did not report zero failures. See '$stdoutPath'."
	}
	Write-Host "Saved $GraphicsApi simulator UI captures to $outputDirectory"
}
finally {
	if ($null -ne $process -and -not $process.HasExited) {
		Stop-Process -Id $process.Id -Force
		$process.WaitForExit()
	}
	foreach ($entry in $savedEnvironment.GetEnumerator()) {
		Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value
	}
	Remove-Item -LiteralPath $testDll, $testManifest, $testReshadeManifest -Force -ErrorAction SilentlyContinue
	Remove-Item -LiteralPath $testLocalAppData -Recurse -Force -ErrorAction SilentlyContinue
	Remove-Item -LiteralPath $createdPresetPath -Force -ErrorAction SilentlyContinue
	[System.IO.File]::WriteAllBytes($vrConfigPath, $savedVrConfig)
	[System.IO.File]::WriteAllBytes($sourcePresetPath, $savedSourcePreset)
}
