param(
	[string]$ReShadePath = '.deps\reshade'
)

# Renders the menu with preview_ui.ps1 and writes the menu pictures shown in the README and on the release page to
# images\. The settings and gestures pictures get a ring around each control their tips mention; positions come from
# controls.json, never by hand.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$preview = Join-Path ([System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))) 'build\reshade-openxr-ui\preview'
$images = Join-Path $PSScriptRoot 'images'
& (Join-Path $PSScriptRoot 'preview_ui.ps1') -ReShadePath $ReShadePath -OutputDirectory $preview

function Save-RingedPicture([string]$Source, [string]$Destination, [object[]]$Rings)
{
	$bitmap = [System.Drawing.Bitmap]::FromFile((Join-Path $preview $Source))
	try
	{
		$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
		$pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 255, 196, 0)), 4
		try
		{
			$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
			foreach ($ring in $Rings)
			{
				$padding = 6
				$left = [float]($ring[0] - $padding)
				$top = [float]($ring[1] - $padding)
				$right = [float]($ring[2] + $padding)
				$bottom = [float]($ring[3] + $padding)
				$radius = [float][Math]::Min(2 * 14, [Math]::Min($right - $left, $bottom - $top))
				$path = New-Object System.Drawing.Drawing2D.GraphicsPath
				$path.AddArc($left, $top, $radius, $radius, 180, 90)
				$path.AddArc($right - $radius, $top, $radius, $radius, 270, 90)
				$path.AddArc($right - $radius, $bottom - $radius, $radius, $radius, 0, 90)
				$path.AddArc($left, $bottom - $radius, $radius, $radius, 90, 90)
				$path.CloseFigure()
				$graphics.DrawPath($pen, $path)
				$path.Dispose()
			}
		}
		finally
		{
			$pen.Dispose()
			$graphics.Dispose()
		}
		$bitmap.Save((Join-Path $images $Destination), [System.Drawing.Imaging.ImageFormat]::Png)
	}
	finally
	{
		$bitmap.Dispose()
	}
}

Copy-Item -LiteralPath (Join-Path $preview 'violet-night-effects.png') -Destination (Join-Path $images 'menu-effects.png') -Force

$controls = Get-Content -LiteralPath (Join-Path $preview 'controls.json') -Raw | ConvertFrom-Json
Save-RingedPicture 'violet-night-settings-readme.png' 'menu-settings.png' @(
	$controls.settings.always_show_statistics,
	$controls.settings.pin)

# The drop-down's box covers only its right part; its row starts where the full-width toggle rows of the other
# column start, shifted to the left column.
$gestures = $controls.gestures
$rowLeft = $gestures.arm_swing_walk[0] - ($gestures.arm_swing_walk[2] - $gestures.preset_gesture[2])
Save-RingedPicture 'violet-night-gestures.png' 'menu-gestures.png' @(
	@($rowLeft, $gestures.preset_gesture[1], $gestures.preset_gesture[2], $gestures.preset_gesture[3]),
	$gestures.arm_swing_walk)

Write-Host "Menu pictures: $images"