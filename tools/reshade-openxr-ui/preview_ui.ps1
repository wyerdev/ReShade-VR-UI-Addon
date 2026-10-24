param(
	[string]$ReShadePath = '.deps\reshade',
	[string]$OutputDirectory = 'build\reshade-openxr-ui\preview'
)

# Fast design loop: compiles only the panel renderer and the preview tool, reuses Dear ImGui objects from the last run,
# and writes PNGs of every menu page and theme to $OutputDirectory. No game, no simulator window, a few seconds per run.
$ErrorActionPreference = 'Stop'

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$reshadeRoot = if ([System.IO.Path]::IsPathRooted($ReShadePath)) { $ReShadePath } else { Join-Path $repo $ReShadePath }
$OutputDirectory = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $repo $OutputDirectory }
$imgui = Join-Path $reshadeRoot 'deps\imgui'
$imguiBackend = Join-Path $imgui 'backends'
$objects = Join-Path $repo 'build\reshade-openxr-ui\preview-obj'
$generated = Join-Path $objects 'generated'
New-Item -ItemType Directory -Path $OutputDirectory, $objects -Force | Out-Null
Get-ChildItem -LiteralPath $OutputDirectory -Filter *.png -ErrorAction SilentlyContinue | Remove-Item -Force

& (Join-Path $PSScriptRoot 'embed_fonts.ps1') -OutputFile (Join-Path $generated 'vr_ui_fonts.inc')

$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = (& $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1).Trim()
$developerCommand = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'

$imguiSources = @(
	(Join-Path $imgui 'imgui.cpp'), (Join-Path $imgui 'imgui_draw.cpp'), (Join-Path $imgui 'imgui_tables.cpp'),
	(Join-Path $imgui 'imgui_widgets.cpp'), (Join-Path $imguiBackend 'imgui_impl_dx11.cpp'),
	(Join-Path $imguiBackend 'imgui_impl_dx12.cpp'), (Join-Path $imguiBackend 'imgui_impl_opengl3.cpp'))
$staleImgui = @($imguiSources | Where-Object { -not (Test-Path (Join-Path $objects ([System.IO.Path]::GetFileNameWithoutExtension($_) + '.obj'))) })
$sources = @((Join-Path $PSScriptRoot 'openxr_ui_preview.cpp'), (Join-Path $PSScriptRoot 'openxr_ui_renderer.cpp')) + $staleImgui
$imguiObjects = $imguiSources | ForEach-Object { '"' + (Join-Path $objects ([System.IO.Path]::GetFileNameWithoutExtension($_) + '.obj')) + '"' }
$executable = Join-Path $objects 'ReShade-OpenXR-UI-Preview.exe'

$compile = 'call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /O1 /MD /MP /c' +
	' /I"' + $imgui + '" /I"' + $imguiBackend + '" /I"' + $generated + '" /Fo:"' + $objects + '\\" ' + (($sources | ForEach-Object { '"' + $_ + '"' }) -join ' ') +
	' && link.exe /nologo /OUT:"' + $executable + '" "' + (Join-Path $objects 'openxr_ui_preview.obj') + '" "' + (Join-Path $objects 'openxr_ui_renderer.obj') + '" ' +
	($imguiObjects -join ' ') + ' d3d11.lib d3d12.lib dxgi.lib windowscodecs.lib ole32.lib'
& cmd.exe /d /c $compile
if ($LASTEXITCODE -ne 0) { throw "Preview build failed with exit code $LASTEXITCODE." }

& $executable $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw "Preview rendering failed with exit code $LASTEXITCODE." }
Write-Host "Previews: $OutputDirectory"
