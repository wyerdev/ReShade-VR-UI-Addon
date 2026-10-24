# Keeps OpenXR add-ons installed on this PC out of a test process.
# Windows loads every add-on registered under ApiLayers\Implicit into every OpenXR program, so an installed copy of
# this add-on or another program's layer (for example a frame-generation tool) would otherwise run inside the test
# and change its results. Each registered add-on declares its own off switch (disable_environment); setting it only
# affects processes started from this PowerShell session, and Restore-InstalledOpenXRLayers puts the old values back.

function Disable-InstalledOpenXRLayers {
	$saved = @{}
	foreach ($registryPath in @('HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit', 'HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit')) {
		if (-not (Test-Path -LiteralPath $registryPath)) {
			continue
		}
		$entries = Get-ItemProperty -LiteralPath $registryPath
		foreach ($entry in $entries.PSObject.Properties) {
			if ($entry.Name -like 'PS*' -or $entry.Value -ne 0 -or -not (Test-Path -LiteralPath $entry.Name)) {
				continue
			}
			$manifest = Get-Content -LiteralPath $entry.Name -Raw | ConvertFrom-Json
			$switch = $manifest.api_layer.disable_environment
			# The OpenXR loader refuses implicit add-ons without an off switch, so they never load.
			if ([string]::IsNullOrWhiteSpace($switch) -or $saved.ContainsKey($switch)) {
				continue
			}
			$saved[$switch] = [Environment]::GetEnvironmentVariable($switch, 'Process')
			[Environment]::SetEnvironmentVariable($switch, '1', 'Process')
			Write-Host "Test isolation: switched off installed OpenXR add-on '$($manifest.api_layer.name)' for this test."
		}
	}
	return $saved
}

function Restore-InstalledOpenXRLayers([hashtable]$Saved) {
	if ($null -eq $Saved) {
		return
	}
	foreach ($switch in $Saved.Keys) {
		[Environment]::SetEnvironmentVariable($switch, $Saved[$switch], 'Process')
	}
}
