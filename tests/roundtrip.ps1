# Round-trip check: gfx ISA -> IL -> SPIR-V -> driver compiler -> gfx ISA.
#
# For every corpus target and fixture:
#   1. recompile the .hsaco to SPIR-V with shader-recomp
#   2. compile that SPIR-V with the installed Vulkan driver (VK_AMD_shader_info)
#   3. disassemble the original .hsaco with llvm-objdump
#   4. compare the opcode classes the source uses against the driver's ISA
#
# A missing source op class is a structural failure. Extra instructions are
# reported as dispatcher/compiler overhead, not as divergence: semantic
# equivalence needs execution (docs/DIVERGENCE_TESTING.md, T3/T4).
param(
    [string]$Arch = "all",
    [string]$LlvmObjdump = "C:\Program Files\AMD\ROCm\7.1\bin\llvm-objdump.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$cli = Join-Path $root "bin\debug\shader-recomp.exe"
$probe = Join-Path $root "bin\debug\SR.ISAProbe.exe"
$corpus = Join-Path $root "tests\corpus"
$outDir = Join-Path $root "tests\artifacts\roundtrip"
New-Item -ItemType Directory -Force $outDir | Out-Null

$targets = [ordered]@{
    "gfx10"   = @{ cli = "gfx10"; cpu = "gfx1010" }
    "gfx1013" = @{ cli = "gfx1013"; cpu = "gfx1013" }
    "gfx1030" = @{ cli = "gfx1030"; cpu = "gfx1030" }
    "gfx1100" = @{ cli = "gfx1100"; cpu = "gfx1100" }
    "gfx1150" = @{ cli = "gfx1150"; cpu = "gfx1150" }
    "gfx1200" = @{ cli = "gfx1200"; cpu = "gfx1200" }
}

function Get-Mnemonics([string[]]$lines) {
    $result = New-Object System.Collections.Generic.List[string]
    foreach ($line in $lines) {
        $text = ($line -split ';')[0]
        $text = ($text -split '//')[0].Trim()
        if ($text.Length -eq 0 -or $text.EndsWith(':')) { continue }
        $token = ($text -split '\s+')[0].ToLowerInvariant()
        if ($token -match '^[sv]_') {
            $token = $token -replace '_e(32|64)$', ''
            $result.Add($token)
        }
    }
    return $result
}

function Get-Class([string]$mnemonic) {
    if ($mnemonic -match '^s_add') { return 'add' }
    if ($mnemonic -match '^s_sub') { return 'sub' }
    if ($mnemonic -match '^s_cmp') { return 'cmp' }
    if ($mnemonic -match '^s_cbranch') { return 'branch' }
    if ($mnemonic -eq 's_branch') { return 'branch' }
    if ($mnemonic -eq 's_endpgm') { return 'endpgm' }
    if ($mnemonic -match '^v_add_f32') { return 'v_add_f32' }
    if ($mnemonic -match '^v_mov_b32') { return 'v_mov_b32' }
    if ($mnemonic -match '^s_mov_b32') { return 's_mov_b32' }
    return $mnemonic
}

$selected = if ($Arch -eq "all") { $targets.Keys } else { @($Arch) }
$failures = 0

foreach ($arch in $selected) {
    $info = $targets[$arch]
    if ($null -eq $info) {
        Write-Error "unknown architecture '$arch'"
        continue
    }

    foreach ($fixture in @("loop")) {
        $input = Join-Path $corpus "$arch\$fixture.hsaco"
        if (-not (Test-Path $input)) { continue }

        $spv = Join-Path $outDir "$arch-$fixture.spv"
        & $cli --arch $info.cli --spirv $spv $input | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "$arch/$fixture : recompile FAILED"
            $failures++
            continue
        }

        $driverIsa = Join-Path $outDir "$arch-$fixture.driver.isa"
        & $probe $spv --disassembly 2>&1 | Set-Content $driverIsa
        if ($LASTEXITCODE -ne 0) {
            Write-Host "$arch/$fixture : driver compile FAILED"
            $failures++
            continue
        }

        $originalIsa = Join-Path $outDir "$arch-$fixture.original.isa"
        & $LlvmObjdump -d --triple=amdgcn-amd-amdhsa "--mcpu=$($info.cpu)" $input 2>&1 | Set-Content $originalIsa

        $source = Get-Content (Join-Path $corpus "$arch\$fixture.s")
        $expected = @()
        foreach ($line in $source) {
            $text = ($line -split ';')[0].Trim()
            if ($text -match '^\s*([sv]_[a-z0-9_]+)') { $expected += Get-Class $Matches[1].ToLowerInvariant() }
        }
        $expected = $expected | Select-Object -Unique

        $driverMnemonics = Get-Mnemonics (Get-Content $driverIsa)
        $driverClasses = $driverMnemonics | ForEach-Object { Get-Class $_ } | Select-Object -Unique
        $missing = @($expected | Where-Object { $_ -notin $driverClasses })

        $originalCount = (Get-Mnemonics (Get-Content $originalIsa)).Count
        $driverCount = $driverMnemonics.Count

        $verdict = if ($missing.Count -eq 0) { "structural-ok" } else { "MISSING: $($missing -join ',')" }
        if ($missing.Count -ne 0) { $failures++ }

        Write-Host ("{0}/{1}: driver={2} original={3} {4}" -f $arch, $fixture, $driverCount, $originalCount, $verdict)
    }
}

if ($failures -ne 0) {
    Write-Host "$failures round-trip checks failed"
    exit 1
}

Write-Host "round-trip structural checks passed; artifacts in $outDir"
