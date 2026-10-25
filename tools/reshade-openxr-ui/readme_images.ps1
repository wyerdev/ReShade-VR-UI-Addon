param(
	[string]$ReShadePath = '.deps\reshade'
)

# Renders the menu with preview_ui.ps1 and writes the menu pictures shown in the README and on the release page to
# images\. The settings picture gets a ring around each control their tips mention; positions come from
# controls.json, never by hand.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$preview = Join-Path ([System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))) 'build\reshade-openxr-ui\preview'
$images = Join-Path $PSScriptRoot 'images'
& (Join-Path $PSScriptRoot 'preview_ui.ps1') -ReShadePath $ReShadePath -OutputDirectory $preview

Copy-Item -LiteralPath (Join-Path $preview 'violet-night-effects.png') -Destination (Join-Path $images 'menu-effects.png') -Force

$settings = (Get-Content -LiteralPath (Join-Path $preview 'controls.json') -Raw | ConvertFrom-Json).settings
# The drop-down's box covers only its right part; its row starts at the left edge of the full-width toggle row above.
$rowLeft = $settings.starts_open[0]
$rings = @(
	@($rowLeft, $settings.preset_gesture[1], $settings.preset_gesture[2], $settings.preset_gesture[3]),
	$settings.always_show_statistics,
	$settings.pin)

$bitmap = [System.Drawing.Bitmap]::FromFile((Join-Path $preview 'violet-night-settings-readme.png'))
try
{
	$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
	$pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 255, 196, 0)), 4
	try
	{
		$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
		foreach ($ring in $rings)
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
	$bitmap.Save((Join-Path $images 'menu-settings.png'), [System.Drawing.Imaging.ImageFormat]::Png)
}
finally
{
	$bitmap.Dispose()
}
Write-Host "Menu pictures: $images"
