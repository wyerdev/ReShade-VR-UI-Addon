# Checks that git-read-only.ps1 allows read-only Git use and blocks Git changes.
$ErrorActionPreference = 'Stop'
$guard = Join-Path $PSScriptRoot 'git-read-only.ps1'

$cases = @(
    @{ Expect = 'allow'; Tool = 'run_in_terminal'; Input = @{ command = 'git diff --check' } }
    @{ Expect = 'allow'; Tool = 'run_in_terminal'; Input = @{ command = 'git -C .deps\reshade status --short' } }
    @{ Expect = 'allow'; Tool = 'run_in_terminal'; Input = @{ command = 'git --no-pager log -n 5; git show HEAD --stat' } }
    @{ Expect = 'allow'; Tool = 'run_in_terminal'; Input = @{ command = '.\build.bat' } }
    @{ Expect = 'allow'; Tool = 'run_in_terminal'; Input = @{ command = 'Get-Content .github\copilot-instructions.md' } }
    @{ Expect = 'allow'; Tool = 'grep_search'; Input = @{ query = 'git commit' } }
    @{ Expect = 'allow'; Tool = 'replace_string_in_file'; Input = @{ filePath = 'A:\repo\.github\hooks\x.json' } }
    @{ Expect = 'deny'; Tool = 'run_in_terminal'; Input = @{ command = 'git add .' } }
    @{ Expect = 'deny'; Tool = 'run_in_terminal'; Input = @{ command = 'git status; git commit -m test' } }
    @{ Expect = 'deny'; Tool = 'run_in_terminal'; Input = @{ command = 'git -c user.name=x push origin main' } }
    @{ Expect = 'deny'; Tool = 'run_in_terminal'; Input = @{ command = '& "C:\Program Files\Git\cmd\git.exe" stage file.cpp' } }
    @{ Expect = 'deny'; Tool = 'run_in_terminal'; Input = @{ command = 'git restore --staged file.cpp' } }
    @{ Expect = 'deny'; Tool = 'run_in_terminal'; Input = @{ command = 'git reset --hard' } }
    @{ Expect = 'deny'; Tool = 'send_to_terminal'; Input = @{ command = 'git stash' } }
    @{ Expect = 'deny'; Tool = 'create_and_run_task'; Input = @{ task = @{ label = 't'; command = 'git'; args = @('commit', '-m', 'x') } } }
    @{ Expect = 'deny'; Tool = 'run_vscode_command'; Input = @{ commandId = 'git.commit' } }
    @{ Expect = 'deny'; Tool = 'create_file'; Input = @{ filePath = 'A:\repo\.git\index' } }
)

$failures = 0
foreach ($case in $cases) {
    $json = @{ hook_event_name = 'PreToolUse'; tool_name = $case.Tool; tool_input = $case.Input } | ConvertTo-Json -Compress -Depth 6
    $output = ($json | powershell -NoProfile -ExecutionPolicy Bypass -File $guard) -join ''
    $actual = if ($output -match '"permissionDecision":"deny"') { 'deny' } else { 'allow' }
    $label = "$($case.Tool): $(($case.Input | ConvertTo-Json -Compress -Depth 6))"
    if ($actual -eq $case.Expect) {
        Write-Host "PASS $($case.Expect) $label"
    }
    else {
        Write-Host "FAIL expected $($case.Expect), got $actual $label"
        $failures++
    }
}

if ($failures) { throw "$failures Git guard case(s) failed." }
Write-Host "All $($cases.Count) Git guard cases passed."
