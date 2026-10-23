param(
	[string]$AddonBuildDirectory = 'build\reshade-openxr-ui',
	[string]$OutputDirectory = 'build\steamvr-listener'
)

$ErrorActionPreference = 'Stop'

function Start-IsolatedListener {
	param(
		[string]$FilePath,
		[string[]]$ArgumentList,
		[string]$LocalAppData
	)

	$savedLocalAppData = $env:LOCALAPPDATA
	try {
		$env:LOCALAPPDATA = $LocalAppData
		Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -PassThru
	}
	finally {
		$env:LOCALAPPDATA = $savedLocalAppData
	}
}

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not [System.IO.Path]::IsPathRooted($AddonBuildDirectory)) {
	$AddonBuildDirectory = Join-Path $repo $AddonBuildDirectory
}
if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
	$OutputDirectory = Join-Path $repo $OutputDirectory
}

# The tests inject the add-on DLLs, so the normal add-on build must run first.
$openxrAddon = Join-Path $AddonBuildDirectory 'ReShade-OpenXR-UI.addon64'
$openvrAddon = Join-Path $AddonBuildDirectory 'ReShade-SteamVR-UI.addon64'
foreach ($requiredPath in @($openxrAddon, $openvrAddon)) {
	if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
		throw "Add-on build output is missing: $requiredPath. Run build.bat first."
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

$listenerSource = Join-Path $PSScriptRoot 'steamvr_loader.cpp'
$testTargetSource = Join-Path $PSScriptRoot 'steamvr_loader_test_target.cpp'
$listener = Join-Path $OutputDirectory 'ReShade-System-Wide-SteamVR-Listener.exe'
$listenerProgramDatabase = Join-Path $OutputDirectory 'ReShade-System-Wide-SteamVR-Listener.pdb'
$testTarget = Join-Path $OutputDirectory 'Listener-Test-Target.exe'
$testTargetProgramDatabase = Join-Path $OutputDirectory 'Listener-Test-Target.pdb'
$testMarker = Join-Path $OutputDirectory 'Listener-Test-Marker.dll'
$testMarkerProgramDatabase = Join-Path $OutputDirectory 'Listener-Test-Marker.pdb'

$listenerCompileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /std:c++17 /EHsc /W4 /O2 /MD /Fo:"' + $OutputDirectory + '\\" "' + $listenerSource + '" /Fe:"' + $listener + '" /link /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup /PDB:"' + $listenerProgramDatabase + '"'
& cmd.exe /d /c $listenerCompileCommand
if ($LASTEXITCODE -ne 0) {
	throw "SteamVR listener build failed with exit code $LASTEXITCODE."
}

$testTargetCompileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /std:c++17 /EHsc /W4 /O2 /MD /Fo:"' + $OutputDirectory + '\\" "' + $testTargetSource + '" /Fe:"' + $testTarget + '" /link /PDB:"' + $testTargetProgramDatabase + '"'
& cmd.exe /d /c $testTargetCompileCommand
if ($LASTEXITCODE -ne 0) {
	throw "SteamVR listener test target build failed with exit code $LASTEXITCODE."
}

$testMarkerCompileCommand = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul & cl.exe /nologo /LD /std:c++17 /EHsc /W4 /O2 /MD /DSTEAMVR_LOADER_TEST_MARKER /Fo:"' + $OutputDirectory + '\\" "' + $testTargetSource + '" /Fe:"' + $testMarker + '" /link /PDB:"' + $testMarkerProgramDatabase + '"'
& cmd.exe /d /c $testMarkerCompileCommand
if ($LASTEXITCODE -ne 0) {
	throw "SteamVR listener test marker build failed with exit code $LASTEXITCODE."
}

$listenerStream = [System.IO.File]::OpenRead($listener)
try {
	$listenerReader = [System.IO.BinaryReader]::new($listenerStream)
	$listenerStream.Position = 0x3c
	$peOffset = $listenerReader.ReadInt32()
	$listenerStream.Position = $peOffset
	$peSignature = $listenerReader.ReadUInt32()
	$machine = $listenerReader.ReadUInt16()
	$listenerStream.Position = $peOffset + 24 + 68
	$subsystem = $listenerReader.ReadUInt16()
}
finally {
	$listenerStream.Dispose()
}
if ($peSignature -ne 0x00004550 -or $machine -ne 0x8664 -or $subsystem -ne 2) {
	throw 'SteamVR listener is not an x64 Windows GUI executable.'
}

$testDirectory = Join-Path $OutputDirectory 'listener-test'
$testAllowlist = Join-Path $testDirectory 'ReShadeApps.ini'
$testOpenVRMarker = Join-Path $testDirectory 'openvr_api.dll'
$testOpenXRMarker = Join-Path $testDirectory 'openxr_loader.dll'
Remove-Item -LiteralPath $testDirectory -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
Set-Content -LiteralPath $testAllowlist -Value "Apps=$testTarget" -Encoding UTF8
Copy-Item -LiteralPath $testMarker -Destination $testOpenVRMarker
Copy-Item -LiteralPath $testMarker -Destination $testOpenXRMarker
$listenerProcess = Start-IsolatedListener -FilePath $listener -ArgumentList @('--test', "`"$testAllowlist`"", "`"$openxrAddon`"", "`"$openvrAddon`"", '--once') -LocalAppData $testDirectory
$testStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$targetProcess = Start-Process -FilePath $testTarget -ArgumentList @('ReShade-OpenXR-UI.addon64', 'ReShade-SteamVR-UI.addon64', "`"$testOpenVRMarker`"") -PassThru
if (-not $targetProcess.WaitForExit(10000) -or $targetProcess.ExitCode -ne 0) {
	if (-not $targetProcess.HasExited) { $targetProcess.Kill() }
	if (-not $listenerProcess.HasExited) { $listenerProcess.Kill() }
	throw 'Listener test target did not observe both injected modules.'
}
if (-not $listenerProcess.WaitForExit(10000) -or $listenerProcess.ExitCode -ne 0) {
	if (-not $listenerProcess.HasExited) { $listenerProcess.Kill() }
	throw 'Listener did not complete its allowlisted injection test.'
}
Write-Host "Listener injected both modules after the synthetic target selected OpenVR ($($testStopwatch.ElapsedMilliseconds) ms observed)."

$listenerProcess = Start-IsolatedListener -FilePath $listener -ArgumentList @('--test', "`"$testAllowlist`"", "`"$openxrAddon`"", "`"$openvrAddon`"", '--once') -LocalAppData $testDirectory
$targetProcess = Start-Process -FilePath $testTarget -ArgumentList @('ReShade-OpenXR-UI.addon64', 'ReShade-SteamVR-UI.addon64', "`"$testOpenXRMarker`"", '--expect-absent') -PassThru
if (-not $targetProcess.WaitForExit(10000) -or $targetProcess.ExitCode -ne 0) {
	if (-not $targetProcess.HasExited) { $targetProcess.Kill() }
	if (-not $listenerProcess.HasExited) { $listenerProcess.Kill() }
	throw 'Listener injected SteamVR modules into the synthetic OpenXR target.'
}
if (-not $listenerProcess.WaitForExit(10000) -or $listenerProcess.ExitCode -ne 0) {
	if (-not $listenerProcess.HasExited) { $listenerProcess.Kill() }
	throw 'Listener did not resolve the synthetic OpenXR target.'
}
Remove-Item -LiteralPath $testDirectory -Recurse -Force
Write-Host 'Listener left the synthetic OpenXR target to the OpenXR API layer.'

$centralReShade = Join-Path $env:ProgramData 'ReShade\ReShade64.dll'
if (Test-Path -LiteralPath $centralReShade -PathType Leaf) {
	$centralTestDirectory = Join-Path $OutputDirectory 'central-reshade-test'
	$centralTestTarget = Join-Path $centralTestDirectory 'CentralReShadeListenerTarget.exe'
	$centralTestAllowlist = Join-Path $centralTestDirectory 'ReShadeApps.ini'
	$centralTestConfig = Join-Path $centralTestDirectory 'ReShade.ini'
	$centralTestLog = Join-Path $centralTestDirectory 'ReShade.log'
	$centralTestOpenVRMarker = Join-Path $centralTestDirectory 'openvr_api.dll'
	Remove-Item -LiteralPath $centralTestDirectory -Recurse -Force -ErrorAction SilentlyContinue
	New-Item -ItemType Directory -Path $centralTestDirectory -Force | Out-Null
	Copy-Item -LiteralPath $testTarget -Destination $centralTestTarget
	Copy-Item -LiteralPath $testMarker -Destination $centralTestOpenVRMarker
	Set-Content -LiteralPath $centralTestAllowlist -Value "Apps=$centralTestTarget" -Encoding UTF8
	Set-Content -LiteralPath $centralTestConfig -Value "[GENERAL]`r`nPerformanceMode=0" -Encoding ASCII
	$centralListenerProcess = Start-IsolatedListener -FilePath $listener -ArgumentList @('--test', "`"$centralTestAllowlist`"", "`"$centralReShade`"", "`"$openvrAddon`"", '--once') -LocalAppData $centralTestDirectory
	$centralTargetProcess = Start-Process -FilePath $centralTestTarget -ArgumentList @('ReShade64.dll', 'ReShade-SteamVR-UI.addon64', "`"$centralTestOpenVRMarker`"") -PassThru
	if (-not $centralTargetProcess.WaitForExit(10000) -or $centralTargetProcess.ExitCode -ne 0) {
		if (-not $centralTargetProcess.HasExited) { $centralTargetProcess.Kill() }
		if (-not $centralListenerProcess.HasExited) { $centralListenerProcess.Kill() }
		throw 'Actual central ReShade listener test target did not observe both modules.'
	}
	if (-not $centralListenerProcess.WaitForExit(10000) -or $centralListenerProcess.ExitCode -ne 0) {
		if (-not $centralListenerProcess.HasExited) { $centralListenerProcess.Kill() }
		throw 'Actual central ReShade listener test did not complete.'
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

Write-Host "Built SteamVR listener: $listener"
