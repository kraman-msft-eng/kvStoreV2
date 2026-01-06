# Deploy updated KVPlayground client to Linux Azure VM

$sshKey = "C:\Users\kraman\Downloads\kvClient.pem"
$linuxVM = "20.115.133.84"

Write-Host "===== Deploying KVPlayground to Linux VM =====" -ForegroundColor Cyan
Write-Host "Target: $linuxVM (Private IP: 10.1.1.4)" -ForegroundColor Yellow
Write-Host ""

# Deploy Client/KVPlayground to Linux VM
Write-Host "Copying KVPlayground binary..." -ForegroundColor Yellow
scp -i $sshKey C:\Users\kraman\source\KVStoreV2\build\KVPlayground\KVPlayground azureuser@${linuxVM}:~/kvgrpc/
if ($LASTEXITCODE -eq 0) {
    Write-Host "  ✓ Client deployed successfully" -ForegroundColor Green
} else {
    Write-Host "  ✗ Client deployment failed" -ForegroundColor Red
    exit 1
}
Write-Host ""

# Deploy benchmark sweep script
Write-Host "Copying benchmark_sweep.sh script..." -ForegroundColor Yellow
scp -i $sshKey C:\Users\kraman\source\KVStoreV2\scripts\analysis\benchmark_sweep.sh azureuser@${linuxVM}:~/kvgrpc/
if ($LASTEXITCODE -eq 0) {
    Write-Host "  ✓ Benchmark script deployed successfully" -ForegroundColor Green
    # Fix line endings and make executable
    ssh -i $sshKey azureuser@${linuxVM} "cd ~/kvgrpc && sed -i 's/\r$//' benchmark_sweep.sh && chmod +x benchmark_sweep.sh"
    Write-Host "  ✓ Line endings fixed and script made executable" -ForegroundColor Green
} else {
    Write-Host "  ✗ Benchmark script deployment failed" -ForegroundColor Red
}
Write-Host ""

Write-Host "===== Deployment Complete =====" -ForegroundColor Green
Write-Host "Location: azureuser@${linuxVM}:~/kvgrpc/KVPlayground" -ForegroundColor Gray
Write-Host "Benchmark: azureuser@${linuxVM}:~/kvgrpc/benchmark_sweep.sh" -ForegroundColor Gray
Write-Host ""
Write-Host "To test with E2E latency metrics, run: .\run_azure_linux.ps1" -ForegroundColor Cyan
Write-Host "To run benchmark sweep, run: .\run\benchmark_run.ps1" -ForegroundColor Cyan
