# Compare the original ISA stream with the driver's ISA stream.
#
# Produces a Markdown report with both instructions streams numbered and a
# per-category count. The categories separate the semantic core from lowering
# overhead (dispatch state, convergence bookkeeping, scheduling).
param(
    [string]$Arch = "gfx1200",
    [string]$Fixture = "loop",
    [string]$LlvmObjdump = "C:\Program Files\AMD\ROCm\7.1\bin\llvm-objdump.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$cli = Join-Path $root "bin\debug\shader-recomp.exe"
$probe = Join-Path $root "bin\debug\SR.ISAProbe.exe"
$corpus = Join-Path $root "tests\corpus"
$outDir = Join-Path $root "tests\artifacts\roundtrip"
New-Item -ItemType Directory -Force $outDir | Out-Null

$targets = @{
    "gfx10"   = @{ cli = "gfx10"; cpu = "gfx1010" }
    "gfx1013" = @{ cli = "gfx1013"; cpu = "gfx1013" }
    "gfx1030" = @{ cli = "gfx1030"; cpu = "gfx1030" }
    "gfx1100" = @{ cli = "gfx1100"; cpu = "gfx1100" }
    "gfx1150" = @{ cli = "gfx1150"; cpu = "gfx1150" }
    "gfx1200" = @{ cli = "gfx1200"; cpu = "gfx1200" }
}

$info = $targets[$Arch]
if ($null -eq $info) { throw "unknown architecture '$Arch'" }

$input = Join-Path $corpus "$Arch\$Fixture.hsaco"
if (-not (Test-Path $input)) { throw "missing fixture $input" }

$spv = Join-Path $outDir "$Arch-$Fixture.spv"
& $cli --arch $info.cli --spirv $spv $input | Out-Null
if ($LASTEXITCODE -ne 0) { throw "recompile failed" }

$driverIsa = Join-Path $outDir "$Arch-$Fixture.driver.isa"
& $probe $spv --disassembly 2>&1 | Set-Content $driverIsa

$originalIsa = Join-Path $outDir "$Arch-$Fixture.original.isa"
& $LlvmObjdump -d --triple=amdgcn-amd-amdhsa "--mcpu=$($info.cpu)" $input 2>&1 | Set-Content $originalIsa

function Get-Instructions([string[]]$lines) {
    $result = New-Object System.Collections.Generic.List[object]
    foreach ($line in $lines) {
        $text = ($line -split ';')[0]
        $text = ($text -split '//')[0].Trim()
        if ($text.Length -eq 0 -or $text.EndsWith(':')) { continue }
        $token = ($text -split '\s+')[0].ToLowerInvariant()
        if ($token -match '^[sv]_') {
            $result.Add([pscustomobject]@{ Mnemonic = $token; Text = $text })
        }
    }
    return $result
}

function Get-Category([string]$mnemonic) {
    if ($mnemonic -eq 's_endpgm') { return 'end' }
    if ($mnemonic -match '^s_(delay_alu|nop|waitcnt|wait|sleep)') { return 'scheduling' }
    if ($mnemonic -match '^s_(and|or|not1)_b64|^s_mov_b64|^s_cbranch_vcc') { return 'convergence' }
    if ($mnemonic -match '^(s|v)_cmp') { return 'compare' }
    if ($mnemonic -match '^s_cselect|^v_cndmask') { return 'select' }
    if ($mnemonic -match '^(s|v)_(branch|cbranch)') { return 'branch' }
    if ($mnemonic -match '^(s|v)_mov') { return 'move' }
    if ($mnemonic -match '^s_(add|sub|mul|min|max|abs|and|or|xor|not|lshl|lshr|ashr|bfe|bfi|bfm|cmov|cselect)') {
        return 'arithmetic'
    }
    if ($mnemonic -match '^v_(add|sub|mul|min|max|fma|mad|and|or|xor|not|cndmask|movrel)') {
        return 'arithmetic'
    }
    return 'other'
}

$original = Get-Instructions (Get-Content $originalIsa)
$driver = Get-Instructions (Get-Content $driverIsa)

$categories = @('arithmetic', 'compare', 'select', 'move', 'branch', 'convergence', 'scheduling', 'end', 'other')
$summary = @{}
foreach ($category in $categories) {
    $summary[$category] = [pscustomobject]@{
        Original = @($original | Where-Object { (Get-Category $_.Mnemonic) -eq $category }).Count
        Driver   = @($driver | Where-Object { (Get-Category $_.Mnemonic) -eq $category }).Count
    }
}

$report = Join-Path $outDir "$Arch-$Fixture.compare.md"
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("# ISA stream comparison: $Arch/$Fixture")
[void]$sb.AppendLine()
[void]$sb.AppendLine("Original: $($original.Count) instructions. Driver: $($driver.Count) instructions.")
[void]$sb.AppendLine()
[void]$sb.AppendLine('| Category | Original | Driver | Delta |')
[void]$sb.AppendLine('|---|---:|---:|---:|')
foreach ($category in $categories) {
    $delta = $summary[$category].Driver - $summary[$category].Original
    [void]$sb.AppendLine("| $category | $($summary[$category].Original) | $($summary[$category].Driver) | $delta |")
}
[void]$sb.AppendLine()
[void]$sb.AppendLine('## Original ISA')
[void]$sb.AppendLine('```')
$index = 0
foreach ($instruction in $original) {
    $index++
    [void]$sb.AppendLine(("{0,3}: {1}" -f $index, $instruction.Text))
}
[void]$sb.AppendLine('```')
[void]$sb.AppendLine()
[void]$sb.AppendLine('## Driver ISA')
[void]$sb.AppendLine('```')
$index = 0
foreach ($instruction in $driver) {
    $index++
    [void]$sb.AppendLine(("{0,3}: {1}" -f $index, $instruction.Text))
}
[void]$sb.AppendLine('```')
[void]$sb.AppendLine()
[void]$sb.AppendLine('The driver stream is the compiler lowering of the emitted SPIR-V. Extra')
[void]$sb.AppendLine('moves, compares, branches and convergence instructions are dispatcher')
[void]$sb.AppendLine('overhead, not lost semantics. Structured emission should remove them.')
$sb.ToString() | Set-Content $report

Write-Host "$Arch/$Fixture original=$($original.Count) driver=$($driver.Count) -> $report"
foreach ($category in $categories) {
    if ($summary[$category].Original -ne 0 -or $summary[$category].Driver -ne 0) {
        Write-Host ("  {0,-12} original={1} driver={2}" -f $category, $summary[$category].Original, $summary[$category].Driver)
    }
}
