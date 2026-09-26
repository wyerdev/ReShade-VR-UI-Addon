# PreToolUse hook: the agent may only run read-only Git commands. Staging,
# committing, pushing and every other Git change are left to the user.
$ErrorActionPreference = 'Stop'

$readOnlyCommands = @(
    'status', 'diff', 'log', 'show', 'blame', 'annotate', 'grep', 'shortlog', 'describe',
    'rev-parse', 'rev-list', 'ls-files', 'ls-tree', 'ls-remote', 'cat-file', 'show-ref',
    'for-each-ref', 'name-rev', 'merge-base', 'check-ignore', 'check-attr', 'cherry',
    'range-diff', 'diff-tree', 'diff-files', 'diff-index', 'count-objects', 'whatchanged',
    'var', 'version', 'help')
$globalOptionsWithValue = @('-C', '-c', '--git-dir', '--work-tree', '--namespace', '--config-env', '--super-prefix')

function Write-Deny([string]$Reason) {
    @{
        hookSpecificOutput = @{
            hookEventName            = 'PreToolUse'
            permissionDecision       = 'deny'
            permissionDecisionReason = $Reason
        }
    } | ConvertTo-Json -Compress -Depth 3
    exit 0
}

# Returns strings found under property names matching $KeyPattern, at any depth.
function Get-Strings($Node, [string]$KeyPattern, [bool]$Take = $false) {
    if ($null -eq $Node) { return }
    if ($Node -is [string]) { if ($Take) { $Node }; return }
    if ($Node -is [System.Management.Automation.PSCustomObject]) {
        foreach ($property in $Node.PSObject.Properties) {
            Get-Strings $property.Value $KeyPattern ($Take -or $property.Name -match $KeyPattern)
        }
        return
    }
    if ($Node -is [System.Collections.IEnumerable]) {
        foreach ($item in $Node) { Get-Strings $item $KeyPattern $Take }
    }
}

function Get-GitSubcommands([string]$Text) {
    $pattern = '(?i)(?<![\w.\-])git(?:\.exe)?(?=["'']?(?:\s|$))'
    foreach ($match in [regex]::Matches($Text, $pattern)) {
        $rest = $Text.Substring($match.Index + $match.Length)
        $segment = ($rest -split '[;|&\r\n(){}]', 2)[0]
        $tokens = @($segment -split '\s+' | ForEach-Object { $_.Trim('"', "'", '`') } | Where-Object { $_ })
        $skipNext = $false
        foreach ($token in $tokens) {
            if ($skipNext) { $skipNext = $false; continue }
            if ($token.StartsWith('-')) {
                if ($globalOptionsWithValue -contains $token) { $skipNext = $true }
                continue
            }
            $token.ToLowerInvariant()
            break
        }
    }
}

$raw = [Console]::In.ReadToEnd()
try {
    $hookInput = $raw | ConvertFrom-Json
    $toolName = [string]$hookInput.tool_name
    $toolInput = $hookInput.tool_input

    $commandText = (@(Get-Strings $toolInput '^(command|args|commandLine)$') -join ' ')
    foreach ($subcommand in @(Get-GitSubcommands $commandText)) {
        if ($readOnlyCommands -notcontains $subcommand) {
            Write-Deny "Blocked 'git $subcommand': the agent may only run read-only Git commands. Staging, committing and pushing are done by the user."
        }
    }

    foreach ($commandId in @(Get-Strings $toolInput '^commandId$')) {
        if ($commandId -match '^git\.') {
            Write-Deny "Blocked VS Code command '$commandId': Git changes are done by the user."
        }
    }

    if ($toolName -match '(?i)create|replace|edit|write|rename|delete|insert|patch|move') {
        foreach ($path in @(Get-Strings $toolInput '(?i)path|uri|dir')) {
            if ($path -match '(?i)(^|[\\/])\.git([\\/]|$)') {
                Write-Deny "Blocked a file change inside a .git folder: Git data is changed only by the user."
            }
        }
    }
}
catch {
    if ($raw -match '(?i)git') {
        Write-Deny "The Git guard could not check this tool call, so it was blocked: $($_.Exception.Message)"
    }
}
exit 0
