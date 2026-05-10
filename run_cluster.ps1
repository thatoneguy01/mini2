# Run 9-node basecamp cluster and launch client
param(
    [string]$ConfigPath = "config/grid_nodes.csv",
    [string]$LogDir = "logs",
    [int]$StealBelow = 50,
    [int]$StealRatio = 2,
    [int]$MaxSteal = 500,
    [float]$ProcessMs = 2,
    [int]$RebalanceMs = 1,
    [int]$LogInterval = 1000,
    [int]$StartupDelay = 2
)

function Get-LatestQueueSizesFromLogs {
    param(
        [string[]]$NodeIds,
        [string]$LogDirectory
    )

    $queueSizes = @{}
    foreach ($nodeId in $NodeIds) {
        $logPath = Join-Path $LogDirectory "node_$nodeId.log"
        if (-not (Test-Path $logPath)) {
            return $null
        }

        $lastLine = Get-Content $logPath -Tail 1 -ErrorAction SilentlyContinue
        if ([string]::IsNullOrWhitespace($lastLine)) {
            return $null
        }

        $parts = $lastLine.Split(',')
        if ($parts.Count -lt 3) {
            return $null
        }

        $queueSizes[$nodeId] = [int]$parts[2].Trim()
    }

    return $queueSizes
}

# Resolve paths relative to this script's directory when given as relative
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if (-not [System.IO.Path]::IsPathRooted($ConfigPath)) {
    $ConfigPath = Join-Path $ScriptDir $ConfigPath
}
if (-not [System.IO.Path]::IsPathRooted($LogDir)) {
    $LogDir = Join-Path $ScriptDir $LogDir
}

# Create logs directory
if (-not (Test-Path $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir | Out-Null
    Write-Host "Created $LogDir directory"
}

# Ensure Python gRPC stubs exist before launching Python workers
$pythonGeneratedDir = Join-Path $ScriptDir "python\generated"
$pythonProtoGen = Join-Path $ScriptDir "python\build_proto.ps1"
if (-not (Test-Path (Join-Path $pythonGeneratedDir "basecamp_pb2.py"))) {
    if (Test-Path $pythonProtoGen) {
        Write-Host "Generating Python gRPC stubs..." -ForegroundColor Yellow
        & $pythonProtoGen
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: Python protobuf generation failed." -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "ERROR: Python protobuf generation script not found: $pythonProtoGen" -ForegroundColor Red
        exit 1
    }
}

# Read grid configuration
$nodes = @{}
$nodeOrder = @()
if (-not (Test-Path $ConfigPath)) {
    Write-Host "ERROR: Config file not found: $ConfigPath" -ForegroundColor Red
    exit 1
}
$csvContent = Get-Content $ConfigPath | Select-Object -Skip 1
foreach ($line in $csvContent) {
    if ([string]::IsNullOrWhitespace($line)) { continue }
    $parts = $line.Split(',')
    if ($parts.Count -lt 7) { continue }
    
    $nodeId = $parts[0].Trim()
    $nodeHost = $parts[1].Trim()
    $nodePort = $parts[2].Trim()
    $nodeLanguage = $parts[3].Trim()
    
    $nodes[$nodeId] = @{
        host = $nodeHost
        port = $nodePort
        language = $nodeLanguage
    }
    $nodeOrder += $nodeId
}

Write-Host "Loaded $($nodes.Count) nodes from $ConfigPath"
Write-Host "Nodes: $($nodeOrder -join ', ')"
Write-Host ""

# Start all nodes
$processes = @{}
foreach ($nodeId in $nodeOrder) {
    $node = $nodes[$nodeId]
    $logFile = Join-Path $LogDir "node_$nodeId.log"
    
    Write-Host "Starting Node $nodeId ($($node.language), :$($node.port))..." -ForegroundColor Cyan
    
    if ($node.language -eq "cpp" -or $node.language -eq "C++") {
        # Start C++ server
        $cppExe = Join-Path $ScriptDir "build\Release\basecamp_cpp_server.exe"
        if (-not (Test-Path $cppExe)) {
            Write-Host "  ERROR: $cppExe not found. Run CMake build first." -ForegroundColor Red
            continue
        }
        $stdoutLog = Join-Path $LogDir "node_$nodeId.stdout.log"
        $stderrLog = Join-Path $LogDir "node_$nodeId.stderr.log"
        $proc = Start-Process -FilePath $cppExe `
            -ArgumentList @(
                "--node-id", $nodeId,
                "--config", $ConfigPath,
                "--steal-below", $StealBelow,
                "--steal-ratio", $StealRatio,
                "--max-steal", $MaxSteal,
                "--process-ms", $ProcessMs,
                "--rebalance-ms", $RebalanceMs,
                "--log-file", $logFile,
                "--log-interval", $LogInterval
            ) `
            -RedirectStandardOutput $stdoutLog `
            -RedirectStandardError $stderrLog `
            -PassThru `
            -WindowStyle Minimized
    } else {
        # Start Python server
        $pythonExe = "python"
        $pythonScript = Join-Path $ScriptDir "python\node_server.py"
        $proc = Start-Process -FilePath $pythonExe `
            -ArgumentList @(
                $pythonScript,
                "--node-id", $nodeId,
                "--config", $ConfigPath,
                "--steal-below", $StealBelow,
                "--steal-ratio", $StealRatio,
                "--max-steal", $MaxSteal,
                "--process-ms", $ProcessMs,
                "--rebalance-ms", $RebalanceMs,
                "--log-file", $logFile,
                "--log-interval", $LogInterval
            ) `
            -PassThru `
            -WindowStyle Minimized
    }
    
    $processes[$nodeId] = $proc
    Write-Host "  Started (PID: $($proc.Id))"
}

Write-Host ""
Write-Host "Waiting $StartupDelay seconds for nodes to initialize..." -ForegroundColor Yellow
Start-Sleep -Seconds $StartupDelay

# Launch client
Write-Host ""
Write-Host "Launching client..." -ForegroundColor Green

$cppClient = Join-Path $ScriptDir "build\Release\basecamp_cpp_client.exe"
if (Test-Path $cppClient) {
    # Run C++ client
    & $cppClient --config $ConfigPath
} else {
    Write-Host "ERROR: $cppClient not found" -ForegroundColor Red
}

Write-Host ""
Write-Host "Client completed. Monitoring node queues for shutdown..." -ForegroundColor Green
Write-Host "Cluster will stop when every node reports queue_size = 0." -ForegroundColor Yellow
Write-Host "Log files: $LogDir/*.log" -ForegroundColor Gray

$idlePollSeconds = [Math]::Max(1, [int][Math]::Ceiling($LogInterval / 1000.0))

# Keep script alive and monitor nodes until all queues are empty
try {
    while ($true) {
        Start-Sleep -Seconds $idlePollSeconds

        $queueSizes = Get-LatestQueueSizesFromLogs -NodeIds $nodeOrder -LogDirectory $LogDir
        if ($null -eq $queueSizes) {
            continue
        }

        $allEmpty = $true
        foreach ($nodeId in $nodeOrder) {
            if ($queueSizes[$nodeId] -ne 0) {
                $allEmpty = $false
                break
            }
        }

        if ($allEmpty) {
            Write-Host "All nodes report queue_size = 0. Stopping cluster..." -ForegroundColor Green
            break
        }
    }
} finally {
    Write-Host ""
    Write-Host "Terminating all nodes..." -ForegroundColor Yellow
    foreach ($nodeId in $nodeOrder) {
        $proc = $processes[$nodeId]
        if ($proc -and -not $proc.HasExited) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            Write-Host "Stopped Node $nodeId"
        }
    }
    Write-Host "All nodes stopped." -ForegroundColor Green
}
