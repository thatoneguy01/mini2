# Experiment runner: calls run_cluster.ps1 with parameter combinations,
# runs visualize_queues.py, and archives logs + generated graphs per run.

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

# Parameter grids (adjust as needed)
$StealBelowVals = @(64, 128, 256, 512)
$StealRatioVals = @(10, 4, 2, 0)
$MaxStealVals   = @(256, 512, 1024)
$ProcessMsVals  = @(0.1, 0.5, 1, 2)
$RebalanceMsVals= @(1, 2, 5, 10, 20)

$LogDir = Join-Path $ScriptDir "logs"
$RunCounter = 0

function Format-ProcessMs {
    param($pm)
    # Format similarly to existing folders: small values become two-digit (0.1 -> 01)
    if ($pm -lt 1) {
        $n = [int]([Math]::Round($pm * 10))
        return "{0:D2}" -f $n
    } else {
        # replace . with p to avoid ambiguity (e.g. 1.5 -> 1p5)
        return $pm.ToString().Replace('.', 'p')
    }
}

foreach ($sb in $StealBelowVals) {
    foreach ($sr in $StealRatioVals) {
        $maxStealSelection = if ($sr -eq 0) { $MaxStealVals } else { @(0) }
        foreach ($ms in $maxStealSelection) {
            foreach ($pm in $ProcessMsVals) {
                foreach ($rb in $RebalanceMsVals) {

                    # If steal_ratio != 0 then max-steal is ignored by the server, so only one placeholder run is needed.
                    $pmStr = Format-ProcessMs -pm $pm
                    $folderName = "test_${sb}_${sr}_${ms}_${pmStr}_${rb}"
                    $dest = Join-Path $LogDir $folderName

                    if (Test-Path $dest) {
                        Write-Host ("`n=== Skipping existing run: {0} ===" -f $folderName) -ForegroundColor DarkYellow
                        continue
                    }

                    $RunCounter += 1
                    Write-Host ("`n=== Run #{0}: {1} ===" -f $RunCounter, $folderName) -ForegroundColor Cyan

                    # Clean logs dir before run
                    if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir | Out-Null }
                    Get-ChildItem -Path $LogDir -Filter "node_*.log" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
                    Get-ChildItem -Path $LogDir -Filter "*_queues.png" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
                    Get-ChildItem -Path $LogDir -Filter "*_jobs.png" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

                    # Build argument list and run the cluster script (it blocks until cluster stops)
                    $runCluster = Join-Path $ScriptDir "run_cluster.ps1"
                    if (-not (Test-Path $runCluster)) {
                        Write-Host "run_cluster.ps1 not found in $ScriptDir" -ForegroundColor Red
                        exit 1
                    }

                    $clusterArgs = @{ 
                        ConfigPath   = (Join-Path $ScriptDir "config\grid_nodes.csv")
                        LogDir       = $LogDir
                        StealBelow   = $sb
                        StealRatio   = $sr
                        MaxSteal     = $ms
                        ProcessMs    = $pm
                        RebalanceMs  = $rb
                        LogInterval  = 1000
                        StartupDelay = 2
                    }

                    & $runCluster @clusterArgs

                    # After cluster run completes, generate graphs (visualize_queues will create two PNGs)
                    Write-Host "Generating graphs..." -ForegroundColor Yellow
                    $vis = Join-Path $ScriptDir "python\visualize_queues.py"
                    if (-not (Test-Path $vis)) {
                        Write-Host "visualize_queues.py not found: $vis" -ForegroundColor Red
                    } else {
                        & python $vis --log-dir $LogDir --output $dest
                    }

                    # Create destination folder and move logs + graphs
                    if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Path $dest | Out-Null }

                    # Move node logs
                    Get-ChildItem -Path $LogDir -Filter "node_*.log" -ErrorAction SilentlyContinue | ForEach-Object {
                        try { Move-Item -Path $_.FullName -Destination $dest -Force } catch { }
                    }

                    # Move graphs (queues + jobs)
                    $qpng = Join-Path $LogDir "${folderName}_queues.png"
                    $jpng = Join-Path $LogDir "${folderName}_jobs.png"
                    if (Test-Path $qpng) { Move-Item -Path $qpng -Destination $dest -Force }
                    if (Test-Path $jpng) { Move-Item -Path $jpng -Destination $dest -Force }

                    Write-Host "Archived run outputs to $dest" -ForegroundColor Green

                }
            }
        }
    }
}

Write-Host "`nAll experiments queued (completed runs archived; existing folders skipped)." -ForegroundColor Cyan
