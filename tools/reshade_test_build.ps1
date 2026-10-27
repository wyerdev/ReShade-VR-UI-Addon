param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Deploy', 'Restore')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$testBuild = Join-Path $repoRoot '.deps\reshade\bin\x64\Release\ReShade64.dll'
$installDir = Join-Path $env:ProgramData 'ReShade'
$installed = Join-Path $installDir 'ReShade64.dll'
$backup = Join-Path $installDir 'ReShade64.dll.BAK'

function Test-IsAdministrator {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdministrator)) {
    Write-Host 'Asking for administrator approval to change C:\ProgramData\ReShade ...'
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action $Action"
    $process = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs -Wait -PassThru
    exit $process.ExitCode
}

function Get-FileHashValue([string]$path) {
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}

function Finish([int]$code) {
    # The elevated window closes on exit, so keep it open long enough to read.
    Write-Host ''
    Read-Host 'Press Enter to close'
    exit $code
}

try {
    if (-not (Test-Path -LiteralPath $installed)) {
        Write-Host "No installed ReShade found at $installed."
        Finish 1
    }

    if ($Action -eq 'Deploy') {
        if (-not (Test-Path -LiteralPath $testBuild)) {
            Write-Host "Test build not found: $testBuild"
            Finish 1
        }

        $testHash = Get-FileHashValue $testBuild
        if (Test-Path -LiteralPath $backup) {
            Write-Host "Original ReShade is already backed up ($backup); keeping that backup."
        }
        elseif ((Get-FileHashValue $installed) -eq $testHash) {
            Write-Host 'The installed ReShade64.dll is already the test build and no backup exists, so the original cannot be backed up.'
            Write-Host 'Reinstall ReShade to get the original back.'
            Finish 1
        }
        else {
            Copy-Item -LiteralPath $installed -Destination $backup
            Write-Host "Backed up the original ReShade to $backup."
        }

        Copy-Item -LiteralPath $testBuild -Destination $installed -Force
        Write-Host 'Test ReShade build installed. Close any running game first if this failed with "file in use".'
        Finish 0
    }

    if (-not (Test-Path -LiteralPath $backup)) {
        Write-Host 'No backup found, so the installed ReShade is already the original. Nothing to restore.'
        Finish 0
    }

    Move-Item -LiteralPath $backup -Destination $installed -Force
    Write-Host 'Original ReShade restored.'
    Finish 0
}
catch {
    Write-Host "Failed: $($_.Exception.Message)"
    Write-Host 'If a game is running, close it and try again.'
    Finish 1
}
