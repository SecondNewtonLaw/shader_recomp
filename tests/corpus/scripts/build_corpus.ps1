[CmdletBinding()]
param(
    [string]$CorpusRoot = $null,
    [string]$Dxc = $env:DXC,
    [string]$Fxc = $env:FXC,
    [switch]$SkipDxil,
    [switch]$SkipDxbc,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

if (-not $CorpusRoot) {
    $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    $CorpusRoot = (Resolve-Path (Join-Path $scriptDirectory '..')).Path
}

function Find-Dxc {
    if ($Dxc) {
        if (-not (Test-Path $Dxc)) { throw "dxc not found at '$Dxc'" }
        return (Resolve-Path $Dxc).Path
    }

    $command = Get-Command dxc -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    throw 'dxc not found. Pass -Dxc <path> or set DXC.'
}

function Find-Fxc {
    if ($Fxc) {
        if (-not (Test-Path $Fxc)) { throw "fxc not found at '$Fxc'" }
        return (Resolve-Path $Fxc).Path
    }

    $roots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "$env:ProgramFiles\Windows Kits\10\bin"
    )

    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $candidate = Get-ChildItem -Path $root -Recurse -Filter fxc.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }

    throw 'fxc not found. Pass -Fxc <path> or set FXC.'
}

function Find-LinalgInclude([string]$DxcPath) {
    if ($env:DXC_LINALG_INCLUDE) {
        if (Test-Path (Join-Path $env:DXC_LINALG_INCLUDE 'dx\linalg.h')) {
            return $env:DXC_LINALG_INCLUDE
        }
        throw "DXC_LINALG_INCLUDE does not contain dx/linalg.h: $env:DXC_LINALG_INCLUDE"
    }

    $probe = Split-Path $DxcPath
    for ($i = 0; $i -lt 6 -and $probe; ++$i) {
        $candidate = Join-Path $probe 'include\hlsl'
        if (Test-Path (Join-Path $candidate 'dx\linalg.h')) { return $candidate }
        $probe = Split-Path $probe
    }

    return $null
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
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json

$compiledRoot = Join-Path $CorpusRoot 'compiled'
$dxilRoot = Join-Path $compiledRoot 'dxil'
$dxilAsmRoot = Join-Path $compiledRoot 'dxil-asm'
$dxbcRoot = Join-Path $compiledRoot 'dxbc'
$dxbcAsmRoot = Join-Path $compiledRoot 'dxbc-asm'

$dxcPath = $null
$dxcVersion = 'unavailable'
$linalgInclude = $null
if (-not $SkipDxil) {
    $dxcPath = Find-Dxc
    $dxcVersion = (& $dxcPath --version 2>&1 | Out-String).Trim()
    $linalgInclude = Find-LinalgInclude $dxcPath

    $needsLinalg = @($manifest.targets | Where-Object { $_.linalg }).Count -gt 0
    if ($needsLinalg -and -not $linalgInclude) {
        throw 'This corpus contains SM 6.10 WMMA targets. Pass -Dxc pointing at DXC 1.10+ with include/hlsl/dx/linalg.h, or set DXC_LINALG_INCLUDE.'
    }
}

$fxcPath = $null
$fxcVersion = 'unavailable'
if (-not $SkipDxbc) {
    $fxcPath = Find-Fxc
    $fxcVersion = (Get-Item $fxcPath).VersionInfo.FileVersion
}

if ($Clean -and (Test-Path $compiledRoot)) {
    Remove-Item -Recurse -Force $compiledRoot
}

foreach ($directory in @($dxilRoot, $dxilAsmRoot, $dxbcRoot, $dxbcAsmRoot)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

$index = [ordered]@{
    version = 1
    toolchain = [ordered]@{
        dxc = $dxcVersion
        fxc = $fxcVersion
    }
    targets = @()
}

$built = 0
$skipped = 0
$failures = @()

foreach ($target in $manifest.targets) {
    $source = Join-Path $CorpusRoot $target.source
    if (-not (Test-Path $source)) {
        $failures += "missing source: $($target.source)"
        continue
    }

    $record = [ordered]@{
        name = $target.name
        source = $target.source
        sourceSha256 = Get-Hash $source
        features = @($target.features)
        dxil = $null
        dxbc = $null
    }

    $dxilProfile = $target.dxilProfile
    if ($dxilProfile -and -not $SkipDxil) {
        if ($target.linalg -and -not $linalgInclude) {
            Write-Warning "skipping $($target.name): DXC has no dx/linalg.h for SM 6.10 matrices"
            $skipped++
        }
        else {
            $dxilRelative = "dxil/$($target.name).dxil"
            $asmRelative = "dxil-asm/$($target.name).asm"
            $dxilPath = Join-Path $compiledRoot $dxilRelative
            $asmPath = Join-Path $compiledRoot $asmRelative

            New-Item -ItemType Directory -Force -Path (Split-Path $dxilPath) | Out-Null
            New-Item -ItemType Directory -Force -Path (Split-Path $asmPath) | Out-Null

            $arguments = @('-nologo', '-WX', '-T', $dxilProfile, '-Fo', $dxilPath, '-Fc', $asmPath)
            if ($dxilProfile -notlike 'lib_*' -and $target.entry) {
                $arguments += @('-E', $target.entry)
            }
            if ($target.enable16Bit) {
                $arguments += '-enable-16bit-types'
            }
            if ($target.linalg) {
                $arguments += @('-I', $linalgInclude)
            }
            $arguments += $source

            Write-Host "dxil $($target.name) [$dxilProfile]"
            $output = & $dxcPath @arguments 2>&1
            if ($LASTEXITCODE -ne 0) {
                $failures += "dxil $($target.name): $($output | Out-String)"
                continue
            }

            $record.dxil = $dxilRelative
            $record.dxilAsm = $asmRelative
            $record.dxilSha256 = Get-Hash $dxilPath
        }
    }

    $dxbcProfile = $target.dxbcProfile
    if ($dxbcProfile -and -not $SkipDxbc) {
        $dxbcRelative = "dxbc/$($target.name).dxbc"
        $asmRelative = "dxbc-asm/$($target.name).asm"
        $dxbcPath = Join-Path $compiledRoot $dxbcRelative
        $asmPath = Join-Path $compiledRoot $asmRelative

        New-Item -ItemType Directory -Force -Path (Split-Path $dxbcPath) | Out-Null
        New-Item -ItemType Directory -Force -Path (Split-Path $asmPath) | Out-Null

        $arguments = @('/nologo', '/WX', '/T', $dxbcProfile, '/E', $target.entry, '/Fo', $dxbcPath, '/Fc', $asmPath, $source)

        Write-Host "dxbc $($target.name) [$dxbcProfile]"
        $output = & $fxcPath @arguments 2>&1
        if ($LASTEXITCODE -ne 0) {
            $failures += "dxbc $($target.name): $($output | Out-String)"
            continue
        }

        $record.dxbc = $dxbcRelative
        $record.dxbcAsm = $asmRelative
        $record.dxbcSha256 = Get-Hash $dxbcPath
    }

    if ($record.dxil -or $record.dxbc) {
        $built++
        $index.targets += $record
    }
    else {
        $skipped++
    }
}

$indexPath = Join-Path $compiledRoot 'index.json'
$index | ConvertTo-Json -Depth 8 | Set-Content -Path $indexPath -Encoding utf8

Write-Host ''
Write-Host "built:   $built"
Write-Host "skipped: $skipped"
Write-Host "failed:  $($failures.Count)"
Write-Host "index:   $indexPath"

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) {
        Write-Host ''
        Write-Host $failure
    }
    exit 1
}
