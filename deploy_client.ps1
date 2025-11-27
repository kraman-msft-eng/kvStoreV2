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

Write-Host "===== Deployment Complete =====" -ForegroundColor Green
Write-Host "Location: azureuser@${linuxVM}:~/kvgrpc/KVPlayground" -ForegroundColor Gray
Write-Host ""
Write-Host "To test with E2E latency metrics, run: .\run_azure_linux.ps1" -ForegroundColor Cyan
