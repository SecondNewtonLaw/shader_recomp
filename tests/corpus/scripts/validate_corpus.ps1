[CmdletBinding()]
param(
    [string]$CorpusRoot = $null
)

$ErrorActionPreference = 'Stop'

if (-not $CorpusRoot) {
    $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    $CorpusRoot = (Resolve-Path (Join-Path $scriptDirectory '..')).Path
}

function Get-Hash([string]$Path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try {
            $hash = $sha.ComputeHash($stream)
            return ([System.BitConverter]::ToString($hash) -replace '-', '').ToLowerInvariant()
        }
        finally {
            $stream.Dispose()
        }
    }
    finally {
        $sha.Dispose()
    }
}

$manifestPath = Join-Path $CorpusRoot 'manifest.json'
$indexPath = Join-Path $CorpusRoot 'compiled\index.json'

if (-not (Test-Path $manifestPath)) { throw "missing manifest: $manifestPath" }
if (-not (Test-Path $indexPath)) { throw "missing compiled index: $indexPath" }

$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$index = Get-Content $indexPath -Raw | ConvertFrom-Json

$errors = @()
$checked = 0

if ($manifest.targets.Count -ne $index.targets.Count) {
    $errors += "target count differs: manifest=$($manifest.targets.Count) index=$($index.targets.Count)"
}

foreach ($target in $manifest.targets) {
    $source = Join-Path $CorpusRoot $target.source
    if (-not (Test-Path $source)) {
        $errors += "missing source: $($target.source)"
        continue
    }

    $record = $index.targets | Where-Object { $_.name -eq $target.name } | Select-Object -First 1
    if (-not $record) {
        $errors += "missing index record: $($target.name)"
        continue
    }

    $sourceHash = Get-Hash $source
    if ($record.sourceSha256 -ne $sourceHash) {
        $errors += "stale source hash: $($target.name)"
    }

    foreach ($field in @('dxil', 'dxbc', 'dxilAsm', 'dxbcAsm')) {
        if (-not $record.$field) { continue }

        $path = Join-Path (Join-Path $CorpusRoot 'compiled') $record.$field
        if (-not (Test-Path $path)) {
            $errors += "missing artifact: $($record.$field)"
            continue
        }

        $hashField = "${field}Sha256"
        if ($record.$hashField -and $record.$hashField -ne (Get-Hash $path)) {
            $errors += "stale artifact hash: $($record.$field)"
        }
    }

    $checked++
}

Write-Host "checked targets: $checked"
Write-Host "errors:         $($errors.Count)"

if ($errors.Count -gt 0) {
    foreach ($errorMessage in $errors) {
        Write-Host ''
        Write-Host $errorMessage
    }
    exit 1
}
