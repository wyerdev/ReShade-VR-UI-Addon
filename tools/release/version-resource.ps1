# Builds a Windows version resource (.res) so Explorer and ReShade's add-on list show the release version.
# The version comes from RESHADE_VR_UI_VERSION (year.month.day.release, set by the release workflow);
# local builds get 0.0.0.0.
# The last of the four version numbers, which the menus show as "vN" ("dev" when 0).
function Get-ReleaseNumber {
	$version = $env:RESHADE_VR_UI_VERSION
	if ($version -match '^\d{1,5}\.\d{1,5}\.\d{1,5}\.(\d{1,5})$') {
		return [int]$Matches[1]
	}
	return 0
}

function New-VersionResource {
	param(
		[Parameter(Mandatory = $true)][string]$DeveloperCommand,
		[Parameter(Mandatory = $true)][string]$OutputDirectory,
		[Parameter(Mandatory = $true)][string]$FileName,
		[Parameter(Mandatory = $true)][string]$Description,
		[switch]$Executable
	)

	$version = $env:RESHADE_VR_UI_VERSION
	if (-not $version) {
		$version = '0.0.0.0'
	}
	if ($version -notmatch '^\d{1,5}\.\d{1,5}\.\d{1,5}\.\d{1,5}$' -or ($version.Split('.') | Where-Object { [int]$_ -gt 65535 })) {
		throw "RESHADE_VR_UI_VERSION '$version' must be four numbers from 0 to 65535, for example 2026.10.24.1."
	}
	$commaVersion = $version.Replace('.', ',')
	$fileType = if ($Executable) { 'VFT_APP' } else { 'VFT_DLL' }
	$baseName = [System.IO.Path]::GetFileNameWithoutExtension($FileName)
	$resourceScript = Join-Path $OutputDirectory "$baseName.version.rc"
	$resourceFile = Join-Path $OutputDirectory "$baseName.version.res"

	$content = @"
#include <winver.h>
VS_VERSION_INFO VERSIONINFO
FILEVERSION $commaVersion
PRODUCTVERSION $commaVersion
FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
FILEFLAGS 0
FILEOS VOS_NT_WINDOWS32
FILETYPE $fileType
FILESUBTYPE 0
BEGIN
	BLOCK "StringFileInfo"
	BEGIN
		BLOCK "040904b0"
		BEGIN
			VALUE "FileDescription", "$Description"
			VALUE "FileVersion", "$version"
			VALUE "OriginalFilename", "$FileName"
			VALUE "ProductName", "ReShade VR UI Add-on"
			VALUE "ProductVersion", "$version"
		END
	END
	BLOCK "VarFileInfo"
	BEGIN
		VALUE "Translation", 0x409, 1200
	END
END
"@
	Set-Content -LiteralPath $resourceScript -Value $content -Encoding ascii

	$command = 'call "' + $DeveloperCommand + '" -arch=x64 -host_arch=x64 >nul & rc.exe /nologo /fo "' + $resourceFile + '" "' + $resourceScript + '"'
	& cmd.exe /d /c $command | Out-Host
	if ($LASTEXITCODE -ne 0) {
		throw "Version resource for $FileName failed with exit code $LASTEXITCODE."
	}
	return $resourceFile
}
