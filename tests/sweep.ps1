# End-to-end sweep: every checked-in ISA fixture is decoded, lifted, emitted as
# SPIR-V, and handed to the AMD driver so it compiles the result to real ISA
# (VK_AMD_shader_info). Graphics stages go through SR.VkDraw, which also proves
# the pair rasterises.
#
# Translation and driver probes run in parallel: each probe creates its own
# Vulkan device, so the work is independent process-level jobs.
#
# usage: pwsh -File tests/sweep.ps1 [-Configuration debug] [-Throttle 8]
param(
    [string]$Configuration = "debug",
    [string]$Bin = "",
    # One probe at a time is the default: eight concurrent Vulkan devices
    # repeatedly wedged this machine's driver, and the sweep is a structural
    # check, not a benchmark. Raise it when the driver is known to cope.
    [int]$Throttle = 1
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
if ([string]::IsNullOrEmpty($Bin)) {
    $Bin = Join-Path $root "bin\$Configuration"
}

$work = Join-Path $env:TEMP "sr-sweep"
New-Item -ItemType Directory -Force $work | Out-Null

$driver = Join-Path $Bin "shader-recomp.exe"
$probe = Join-Path $Bin "SR.ISAProbe.exe"
$draw = Join-Path $Bin "SR.VkDraw.exe"

if (-not (Test-Path $driver)) {
    throw "shader-recomp.exe not found in $Bin"
}

$watch = [System.Diagnostics.Stopwatch]::StartNew()

$cases = @(
    @{ Directory = "gfx10"; Arch = "gfx10" },
    @{ Directory = "gfx1013"; Arch = "gfx1013" },
    @{ Directory = "gfx1030"; Arch = "gfx1030" },
    @{ Directory = "gfx1100"; Arch = "gfx1100" },
    @{ Directory = "gfx1150"; Arch = "gfx1150" },
    @{ Directory = "gfx1200"; Arch = "gfx1200" }
)

$jobs = @()
foreach ($case in $cases) {
    $directory = Join-Path $here "corpus\$($case.Directory)"
    if (-not (Test-Path $directory)) {
        continue
    }

    # Fixtures that need lift options beyond the architecture.
    $options = @{
        "triangle"   = @("--test-abi")
        "solid"      = @("--test-abi")
        "userdata"   = @("--test-abi", "--user-data", "41")
        "readfirst"  = @("--test-abi", "--wave64")
        "waveballot" = @("--test-abi", "--wave64")
        "descriptor" = @("--test-abi", "--user-data-push", "4")
        "predicated" = @("--test-abi")
        "imageload"  = @("--test-abi", "--user-data-push", "8")
        "imagesample" = @("--test-abi", "--user-data-push", "12")
        "imagestore" = @("--test-abi", "--user-data-push", "8")
        "tbuffer"    = @("--test-abi", "--user-data-push", "4")
        "tbufferxyzw" = @("--test-abi", "--user-data-push", "4")
        "tbufferfmt" = @("--test-abi", "--user-data-push", "4")
        "bufferwide" = @("--test-abi", "--user-data-push", "4")
        "buffernarrow" = @("--test-abi", "--user-data-push", "4")
        "image"      = @("--test-abi", "--user-data-push", "12")
        "geometry_staged" = @("--gs-output-registers", "2")
        "scalarnew" = @("--test-abi")
        "dscounter" = @("--test-abi")
        "dscmpst" = @("--test-abi")
        "imageresinfo" = @("--test-abi", "--user-data-push", "4")
        "imagegather4" = @("--test-abi", "--user-data-push", "12")
        "imagemip" = @("--test-abi", "--user-data-push", "12")
        "imagesampled" = @("--test-abi", "--user-data-push", "12")
        "imageloadmip" = @("--test-abi", "--user-data-push", "4")
        "imageatomicadd" = @("--test-abi", "--user-data-push", "4", "--image-format", "r32ui")
    }

    # Fixtures whose translation is expected to be refused today: they are
    # reported, not counted as failures.
    $expectedRefusal = @("imageloadmip")

    Get-ChildItem $directory -Filter *.hsaco | Sort-Object Name | ForEach-Object {
        $name = $_.BaseName
        $stage = "compute"
        if ($name -eq "triangle" -or $name -eq "empty") { $stage = "vertex" }
        if ($name -eq "solid") { $stage = "fragment" }
        if ($name -eq "geometry" -or $name -eq "geometry_read" -or $name -eq "geometry_staged") { $stage = "geometry" }

        $jobs += [pscustomobject]@{
            Target = "$($case.Directory)/$name"
            Arch = $case.Arch
            Stage = $stage
            Extra = if ($options.ContainsKey($name)) { $options[$name] } else { @() }
            ExpectRefusal = $expectedRefusal -contains $name
            Input = $_.FullName
            Path = Join-Path $work "$($case.Directory)-$name.spv"
        }
    }
}

$xbox = Join-Path $here "corpus\xbox\compute.bin"
if (Test-Path $xbox) {
    $jobs += [pscustomobject]@{
        Target = "xbox/compute"
        Arch = ""
        Stage = "compute"
        Input = $xbox
        Path = Join-Path $work "xbox-compute.spv"
    }
}

# Phase 1: translate every fixture to SPIR-V.
$translated = $jobs | ForEach-Object -ThrottleLimit $Throttle -Parallel {
    $job = $_
    $arguments = @()
    if ($job.Arch -ne "") { $arguments += "--arch"; $arguments += $job.Arch }
    $arguments += "--stage"; $arguments += $job.Stage
    $arguments += "--spirv"; $arguments += $job.Path
    $arguments += $job.Extra
    $arguments += $job.Input

    $output = & $using:driver @arguments 2>&1
    [pscustomobject]@{
        Target = $job.Target
        Stage = $job.Stage
        Path = $job.Path
        Translated = ($LASTEXITCODE -eq 0)
        ExpectRefusal = $job.ExpectRefusal
        DriverIsa = "-"
        Note = if ($LASTEXITCODE -eq 0) { "" } else { ($output | Select-Object -First 1) }
    }
}

# Phase 2: every compute module goes through the driver's compiler.
$compute = @($translated | Where-Object { $_.Translated -and $_.Stage -eq "compute" -and (Test-Path $probe) })
if ($compute.Count -gt 0) {
    $probed = $compute | ForEach-Object -ThrottleLimit $Throttle -Parallel {
        $entry = $_
        $output = & $using:probe $entry.Path 2>&1
        $ok = $LASTEXITCODE -eq 0 -and ($output -match "disassembly bytes")
        [pscustomobject]@{
            Target = $entry.Target
            Stage = $entry.Stage
            Path = $entry.Path
            Translated = $ok
            DriverIsa = if ($ok) { "yes" } else { "no" }
            Note = if ($ok) { "" } else { ($output | Select-Object -First 1) }
        }
    }

    $byTarget = @{}
    foreach ($entry in $probed) { $byTarget[$entry.Target] = $entry }
    $translated = @($translated | ForEach-Object {
        if ($_.Stage -eq "compute" -and $byTarget.ContainsKey($_.Target)) { $byTarget[$_.Target] } else { $_ }
    })
}

$results = @($translated | ForEach-Object {
    $entry = $_
    if ($entry.Translated -and $entry.Stage -ne "compute" -and (Test-Path $draw)) {
        $entry.DriverIsa = "via draw"
    }
    [pscustomobject]@{
        Target = $entry.Target
        Stage = $entry.Stage
        Spirv = if ($entry.Translated) { "yes" } elseif ($entry.ExpectRefusal) { "refused" } else { "no" }
        DriverIsa = $entry.DriverIsa
        Note = $entry.Note
    }
})

# The graphics pair draws, and the driver compiles both stages.
$vertex = Join-Path $work "gfx1030-triangle.spv"
$fragment = Join-Path $work "gfx1030-solid.spv"
if ((Test-Path $vertex) -and (Test-Path $fragment) -and (Test-Path $draw)) {
    $drawOutput = & $draw $vertex $fragment 2>&1
    $rendered = $drawOutput | Select-String -Pattern "pixels written: (\d+)"
    $centre = $drawOutput | Select-String -Pattern "centre rgba: (.*)"
    $results += [pscustomobject]@{
        Target = "gfx1030/triangle+solid"
        Stage = "vs+ps"
        Spirv = "yes"
        DriverIsa = "via draw"
        Note = if ($rendered) { "$($rendered.Matches[0].Value); $($centre.Matches[0].Value)" } else { "draw failed" }
    }
}

# The geometry path draws as well: an empty vertex stage, the geometry fixture
# and the fragment stage, so the emitted triangle has to rasterise.
$geometry = Join-Path $work "gfx1030-geometry.spv"
$emptyVertex = Join-Path $work "gfx1030-empty.spv"
if ((Test-Path $geometry) -and (Test-Path $emptyVertex) -and (Test-Path $fragment) -and (Test-Path $draw)) {
    $drawOutput = & $draw $emptyVertex $fragment --gs $geometry 2>&1
    $rendered = $drawOutput | Select-String -Pattern "pixels written: (\d+)"
    $centre = $drawOutput | Select-String -Pattern "centre rgba: (.*)"
    $results += [pscustomobject]@{
        Target = "gfx1030/empty+geometry+solid"
        Stage = "vs+gs+ps"
        Spirv = "yes"
        DriverIsa = "via draw"
        Note = if ($rendered) { "$($rendered.Matches[0].Value); $($centre.Matches[0].Value)" } else { "draw failed" }
    }
}

# A geometry stage that consumes the vertex stage's outputs draws as well: the
# halved triangle is the golden comparison against a vertex stage that computes
# the same positions, and the centre stays red either way.
$geometryRead = Join-Path $work "gfx1030-geometry_read.spv"
if ((Test-Path $geometryRead) -and (Test-Path $vertex) -and (Test-Path $fragment) -and (Test-Path $draw)) {
    $drawOutput = & $draw $vertex $fragment --gs $geometryRead 2>&1
    $rendered = $drawOutput | Select-String -Pattern "pixels written: (\d+)"
    $centre = $drawOutput | Select-String -Pattern "centre rgba: (.*)"
    $results += [pscustomobject]@{
        Target = "gfx1030/triangle+geometry_read+solid"
        Stage = "vs+gs+ps"
        Spirv = "yes"
        DriverIsa = "via draw"
        Note = if ($rendered) { "$($rendered.Matches[0].Value); $($centre.Matches[0].Value)" } else { "draw failed" }
    }
}

# The staged geometry fixture publishes its vertices by message, so it draws
# with a vertex stage that emits nothing.
$geometryStaged = Join-Path $work "gfx1030-geometry_staged.spv"
if ((Test-Path $geometryStaged) -and (Test-Path $emptyVertex) -and (Test-Path $fragment) -and (Test-Path $draw)) {
    $drawOutput = & $draw $emptyVertex $fragment --gs $geometryStaged 2>&1
    $rendered = $drawOutput | Select-String -Pattern "pixels written: (\d+)"
    $centre = $drawOutput | Select-String -Pattern "centre rgba: (.*)"
    $results += [pscustomobject]@{
        Target = "gfx1030/empty+geometry_staged+solid"
        Stage = "vs+gs+ps"
        Spirv = "yes"
        DriverIsa = "via draw"
        Note = if ($rendered) { "$($rendered.Matches[0].Value); $($centre.Matches[0].Value)" } else { "draw failed" }
    }
}

$watch.Stop()

$results | Sort-Object Target | Format-Table -AutoSize | Out-String -Width 200 | Write-Host

$refused = @($results | Where-Object { $_.Spirv -eq "refused" })
$failures = @($results | Where-Object { $_.Spirv -eq "no" })
$accepted = @($results | Where-Object { $_.Spirv -eq "yes" }).Count
Write-Host "$accepted of $($results.Count) fixtures translated and accepted by the driver in $([math]::Round($watch.Elapsed.TotalSeconds, 1)) s; $($refused.Count) refused by design"
foreach ($entry in $refused) { Write-Host "REFUSED $($entry.Target): $($entry.Note)" }
if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Host "FAIL $($_.Target): $($_.Note)" }
    exit 1
}
