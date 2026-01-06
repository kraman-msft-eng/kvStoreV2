# Run KVStore Benchmark Sweep and Generate Charts
# 
# This script:
# 1. Runs the benchmark sweep on the Linux client VM
# 2. Downloads the results CSV
# 3. Generates interactive HTML charts
# 4. Opens the charts in browser
#
# Prerequisites:
# - Linux VM deployed with KVPlayground and benchmark_sweep.sh
# - KVStore gRPC server running on SF cluster (ports 8085, 8086)
# - Python 3 installed locally for chart generation

param(
    [string]$SshKey = "C:\Users\kraman\Downloads\kvClient.pem",
    [string]$LinuxVM = "20.115.133.84",
    [switch]$SkipBenchmark,  # Skip running benchmark, just regenerate charts from existing CSV
    [switch]$OpenChart       # Open chart in browser after generation
)

$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$AnalysisDir = Join-Path (Split-Path -Parent $ScriptRoot) "analysis"
$ResultsCSV = Join-Path $AnalysisDir "benchmark_results.csv"
$ChartHTML = Join-Path $AnalysisDir "benchmark_charts.html"
$ChartGenerator = Join-Path $AnalysisDir "generate_charts.py"

Write-Host "===== KVStore Benchmark Runner =====" -ForegroundColor Cyan
Write-Host "Linux VM: $LinuxVM" -ForegroundColor Yellow
Write-Host "Analysis Dir: $AnalysisDir" -ForegroundColor Yellow
Write-Host ""

# Step 1: Run benchmark on Linux VM (unless skipped)
if (-not $SkipBenchmark) {
    Write-Host "Step 1: Running benchmark sweep on Linux VM..." -ForegroundColor Cyan
    Write-Host "  This will run 9 test configurations (C=1,2,4,6,8,16,32,64,80)" -ForegroundColor Gray
    Write-Host "  Estimated time: 3-5 minutes" -ForegroundColor Gray
    Write-Host ""
    
    $startTime = Get-Date
    ssh -i $SshKey azureuser@${LinuxVM} "cd ~/kvgrpc && ./benchmark_sweep.sh"
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ✗ Benchmark failed!" -ForegroundColor Red
        exit 1
    }
    
    $elapsed = (Get-Date) - $startTime
    Write-Host ""
    Write-Host "  ✓ Benchmark completed in $([math]::Round($elapsed.TotalMinutes, 1)) minutes" -ForegroundColor Green
    Write-Host ""
    
    # Step 2: Download results CSV
    Write-Host "Step 2: Downloading results CSV..." -ForegroundColor Cyan
    scp -i $SshKey azureuser@${LinuxVM}:/tmp/benchmark_sweep/benchmark_results.csv $ResultsCSV
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ✗ Failed to download results!" -ForegroundColor Red
        exit 1
    }
    Write-Host "  ✓ Results saved to: $ResultsCSV" -ForegroundColor Green
    Write-Host ""
} else {
    Write-Host "Step 1: Skipping benchmark (using existing CSV)" -ForegroundColor Yellow
    if (-not (Test-Path $ResultsCSV)) {
        Write-Host "  ✗ No existing CSV found at: $ResultsCSV" -ForegroundColor Red
        exit 1
    }
    Write-Host "  Using: $ResultsCSV" -ForegroundColor Gray
    Write-Host ""
}

# Step 3: Generate charts
Write-Host "Step 3: Generating interactive charts..." -ForegroundColor Cyan

if (-not (Test-Path $ChartGenerator)) {
    Write-Host "  ✗ Chart generator not found: $ChartGenerator" -ForegroundColor Red
    exit 1
}

python $ChartGenerator $ResultsCSV $ChartHTML

if ($LASTEXITCODE -ne 0) {
    Write-Host "  ✗ Chart generation failed!" -ForegroundColor Red
    exit 1
}
Write-Host "  ✓ Charts saved to: $ChartHTML" -ForegroundColor Green
Write-Host ""

# Also copy to docs folder
$DocsChart = Join-Path (Split-Path -Parent (Split-Path -Parent $ScriptRoot)) "docs\benchmark_charts.html"
Copy-Item $ChartHTML $DocsChart -Force
Write-Host "  ✓ Also copied to: $DocsChart" -ForegroundColor Green
Write-Host ""

# Step 4: Display summary
Write-Host "===== Benchmark Summary =====" -ForegroundColor Cyan
$csv = Import-Csv $ResultsCSV
$maxTPS = ($csv | Measure-Object -Property tps -Maximum).Maximum
$maxTokens = ($csv | Measure-Object -Property tokens_per_sec -Maximum).Maximum
$optimalRow = $csv | Where-Object { [double]$_.tps -eq $maxTPS } | Select-Object -First 1

Write-Host "Peak Throughput: $([math]::Round([double]$maxTokens / 1000, 1))K tokens/sec" -ForegroundColor Green
Write-Host "Peak TPS: $maxTPS iterations/sec" -ForegroundColor Green
Write-Host "Optimal Concurrency: $($optimalRow.total_concurrency)" -ForegroundColor Green
Write-Host ""

# Step 5: Open chart (if requested)
if ($OpenChart) {
    Write-Host "Opening chart in browser..." -ForegroundColor Cyan
    Start-Process $ChartHTML
}

Write-Host "===== Done =====" -ForegroundColor Green
Write-Host ""
Write-Host "To view charts, open: $ChartHTML" -ForegroundColor Gray
Write-Host "To re-run benchmark: .\benchmark_run.ps1" -ForegroundColor Gray
Write-Host "To regenerate charts only: .\benchmark_run.ps1 -SkipBenchmark" -ForegroundColor Gray
