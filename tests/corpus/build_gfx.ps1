# Assemble every gfx corpus directory with the matching AMDGPU target.
param(
    [string]$LlvmMc = "C:\Program Files\AMD\ROCm\7.1\bin\llvm-mc.exe"
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

$targets = @{
    "gfx10"   = "gfx1010"
    "gfx1013" = "gfx1013"
    "gfx1030" = "gfx1030"
    "gfx1100" = "gfx1100"
    "gfx1150" = "gfx1150"
    "gfx1200" = "gfx1200"
}

foreach ($arch in ($targets.Keys | Sort-Object)) {
    $directory = Join-Path $here $arch
    if (-not (Test-Path $directory)) {
        continue
    }

    $cpu = $targets[$arch]
    Get-ChildItem $directory -Filter *.s | ForEach-Object {
        $output = [IO.Path]::ChangeExtension($_.FullName, ".hsaco")
        & $LlvmMc -triple=amdgcn-amd-amdhsa "-mcpu=$cpu" -filetype=obj $_.FullName -o $output
        if ($LASTEXITCODE -ne 0) {
            throw "llvm-mc failed for $($_.FullName) ($cpu)"
        }
        Write-Host "$arch/$($_.Name) -> $(Split-Path -Leaf $output) ($((Get-Item $output).Length) bytes)"
    }
}
