param(
    [string]$ProcessName = 'DOOMx64vk',
    [int]$WaitSeconds = 180
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $repo 'build\doom-dumps'
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

$pkg = Get-AppxPackage Microsoft.WinDbg
if (-not $pkg) { throw 'WinDbg is not installed (winget install Microsoft.WinDbg).' }
$cdb = Join-Path $pkg.InstallLocation 'amd64\cdb.exe'

$log = Join-Path $outDir 'cdb.log'
$dump = (Join-Path $outDir 'crash.dmp').Replace('\', '/')
$cmds = Join-Path $outDir 'cdb-commands.txt'

# Log every access violation with a stack and module list; write a dump on the last one before exit.
@"
.symopt+ 0x4
sxn ld
sxe -c2 ".echo ==== SECOND CHANCE ====;.lastevent;.dump /ma /o $dump;q" av
sxe -c ".echo ==== ACCESS VIOLATION ====;.lastevent;r;kb 40;.echo ==== MODULES ====;lm;.dump /mi /o $dump;.echo ==== END ====;g" av
g
"@ | Set-Content -Path $cmds -Encoding ASCII

Write-Host "Waiting up to $WaitSeconds s for $ProcessName.exe. Start DOOM from the mod launcher now."
$deadline = (Get-Date).AddSeconds($WaitSeconds)
$proc = $null
while ((Get-Date) -lt $deadline) {
    $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc) { break }
    Start-Sleep -Milliseconds 20
}
if (-not $proc) { Write-Host "$ProcessName.exe did not start in time."; exit 2 }

Write-Host "Attaching to PID $($proc.Id). Log: $log"
& $cdb -p $proc.Id -logo $log -cf $cmds
Write-Host "Debugger finished. Log: $log"
