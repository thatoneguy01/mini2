# Run 9-node basecamp cluster and launch client
param(
    [string]$ConfigPath = "config/grid_nodes.csv",
    [string]$LogDir = "logs",
    [int]$LogInterval = 1000,
    [int]$StartupDelay = 2
)

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
        $cppExe = Join-Path $ScriptDir "build\basecamp_cpp_server.exe"
        if (-not (Test-Path $cppExe)) {
            Write-Host "  ERROR: $cppExe not found. Run CMake build first." -ForegroundColor Red
            continue
        }
        $proc = Start-Process -FilePath $cppExe `
            -ArgumentList @(
                "--node-id", $nodeId,
                "--config", $ConfigPath,
                "--log-file", $logFile,
                "--log-interval", $LogInterval
            ) `
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

$cppClient = Join-Path $ScriptDir "build\basecamp_cpp_client.exe"
if (Test-Path $cppClient) {
    # Run C++ client
    & $cppClient --config $ConfigPath
} else {
    Write-Host "ERROR: $cppClient not found" -ForegroundColor Red
}

Write-Host ""
Write-Host "Client completed. Cluster is still running." -ForegroundColor Green
Write-Host "Press Ctrl+C to stop all nodes." -ForegroundColor Yellow
Write-Host "Log files: $LogDir/*.log" -ForegroundColor Gray

# Keep script alive and monitor nodes
try {
    while ($true) {
        Start-Sleep -Seconds 5
        foreach ($nodeId in $nodeOrder) {
            $proc = $processes[$nodeId]
            if ($proc.HasExited) {
                Write-Host "Node $nodeId has exited (exit code: $($proc.ExitCode))" -ForegroundColor Red
            }
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
