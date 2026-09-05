[CmdletBinding()]
param(
    [ValidateSet('Setup', 'Update', 'Status')]
    [string]$Action = 'Setup',

    [string]$ReShadePath = '.deps\reshade',

    [string]$RepositoryUrl = 'https://github.com/crosire/reshade.git'
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = $PSScriptRoot
$resolvedReShadePath = if ([System.IO.Path]::IsPathRooted($ReShadePath)) {
    [System.IO.Path]::GetFullPath($ReShadePath)
}
else {
    [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $ReShadePath))
}
$dependencyNotePath = Join-Path $repositoryRoot 'RESHADE-dependency.md'

function Invoke-Git {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & git @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorActionPreference
    if ($exitCode -ne 0) {
        throw "Git command failed ($exitCode): git $($Arguments -join ' ')`n$($output -join [Environment]::NewLine)"
    }
    return $output
}

function Test-ReShadeCheckout {
    $requiredPaths = @(
        'include\reshade.hpp',
        'deps\imgui\imgui.cpp',
        'deps\openxr\include\openxr\openxr.h',
        'deps\openvr\headers\openvr.h'
    )

    foreach ($relativePath in $requiredPaths) {
        $path = Join-Path $resolvedReShadePath $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "ReShade checkout is missing required content: $path. Run setup-reshade.ps1 -Action Update."
        }
    }
}

function Write-DependencyNote {
    Test-ReShadeCheckout

    $remote = (Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'remote', 'get-url', 'origin') | Select-Object -First 1).ToString().Trim()
    $branch = (Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'branch', '--show-current') | Select-Object -First 1).ToString().Trim()
    if (-not $branch) {
        $branch = 'detached HEAD'
    }
    $commit = (Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'rev-parse', 'HEAD') | Select-Object -First 1).ToString().Trim()
    $commitDate = (Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'show', '-s', '--format=%cI', 'HEAD') | Select-Object -First 1).ToString().Trim()
    $recordedAt = (Get-Date).ToUniversalTime().ToString('o')
    $submodules = Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'submodule', 'status', '--recursive')
    $checkoutLabel = if ([System.IO.Path]::IsPathRooted($ReShadePath)) {
        'external checkout (path supplied at runtime)'
    }
    else {
        $ReShadePath
    }

    $note = @"
# ReShade Dependency Snapshot

This file records the ReShade source used by the local ReShade VR UI Add-on build. It is
informational, not a version pin. `setup-reshade.ps1 -Action Update` fetches
the latest commit on the checkout's upstream default branch and refreshes this
record.

- Repository: $remote
- Checkout: $checkoutLabel
- Branch: $branch
- Commit: $commit
- Commit date: $commitDate
- Snapshot recorded: $recordedAt

## Submodules

~~~text
$($submodules -join [Environment]::NewLine)
~~~

The plugin repository does not copy, modify, bundle, or publish ReShade. The
checkout is used only as a local build dependency and official ReShade remains
the runtime dependency for installed releases.
"@
    Set-Content -LiteralPath $dependencyNotePath -Value $note -Encoding ASCII
    Write-Host "Recorded ReShade dependency snapshot: $dependencyNotePath"
    Write-Host "ReShade commit: $commit"
}

function Ensure-ReShadeCheckout {
    if (Test-Path -LiteralPath $resolvedReShadePath) {
        if (-not (Test-Path -LiteralPath (Join-Path $resolvedReShadePath '.git'))) {
            throw "The requested ReShade path exists but is not a Git checkout: $resolvedReShadePath"
        }
        return
    }

    New-Item -ItemType Directory -Path (Split-Path $resolvedReShadePath -Parent) -Force | Out-Null
    Write-Host "Cloning latest ReShade source from $RepositoryUrl"
    & git clone --recurse-submodules $RepositoryUrl $resolvedReShadePath
    if ($LASTEXITCODE -ne 0) {
        throw "ReShade clone failed with exit code $LASTEXITCODE."
    }
}

function Update-ReShadeCheckout {
    Ensure-ReShadeCheckout

    $status = Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'status', '--porcelain')
    if ($status) {
        throw "Refusing to update a dirty ReShade checkout: $resolvedReShadePath"
    }

    Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'fetch', '--prune', 'origin') | Out-Null
    Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'remote', 'set-head', 'origin', '-a') | Out-Null
    $remoteHead = (Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'symbolic-ref', '--short', 'refs/remotes/origin/HEAD') | Select-Object -First 1).ToString().Trim()
    if (-not $remoteHead) {
        throw "Could not determine the upstream default branch for $RepositoryUrl"
    }
    $branch = $remoteHead -replace '^origin/', ''
    $currentBranch = (Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'branch', '--show-current') | Select-Object -First 1).ToString().Trim()
    if ($currentBranch -ne $branch) {
        Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'switch', $branch) | Out-Null
    }
    Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'pull', '--ff-only', '--recurse-submodules', 'origin', $branch) | Out-Null
    Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'submodule', 'update', '--init', '--recursive') | Out-Null
    Write-Host "Updated ReShade checkout to the latest $branch revision."
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git was not found on PATH.'
}

switch ($Action) {
    'Setup' {
        Ensure-ReShadeCheckout
        Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'submodule', 'update', '--init', '--recursive') | Out-Null
        Write-DependencyNote
    }
    'Update' {
        Update-ReShadeCheckout
        Write-DependencyNote
    }
    'Status' {
        Ensure-ReShadeCheckout
        Write-DependencyNote
        $status = Invoke-Git -Arguments @('-C', $resolvedReShadePath, 'status', '--short', '--branch')
        $status | ForEach-Object { Write-Host $_ }
    }
}
