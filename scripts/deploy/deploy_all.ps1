# Deploy updated binaries to both Azure VMs

$sshKey = "C:\Users\kraman\Downloads\kvClient.pem"
$windowsVM = "172.179.242.186"
$linuxVM = "20.115.133.84"

Write-Host "===== Deploying KVStore Updates =====" -ForegroundColor Cyan
Write-Host ""

# 1. Deploy Server to Windows VM
Write-Host "[1/3] Deploying KVStoreServer to Windows VM ($windowsVM)..." -ForegroundColor Yellow
scp C:\Users\kraman\source\KVStoreV2\KVService\build\Release\KVStoreServer.exe azureuser@${windowsVM}:~/kvstore/
if ($LASTEXITCODE -eq 0) {
    Write-Host "  ✓ Server deployed successfully" -ForegroundColor Green
} else {
    Write-Host "  ✗ Server deployment failed" -ForegroundColor Red
    exit 1
}
Write-Host ""

# 2. Deploy Client/KVPlayground to Linux VM
Write-Host "[2/3] Deploying KVPlayground to Linux VM ($linuxVM)..." -ForegroundColor Yellow
scp -i $sshKey C:\Users\kraman\source\KVStoreV2\build\KVPlayground\KVPlayground azureuser@${linuxVM}:~/kvgrpc/
if ($LASTEXITCODE -eq 0) {
    Write-Host "  ✓ Client deployed successfully" -ForegroundColor Green
} else {
    Write-Host "  ✗ Client deployment failed" -ForegroundColor Red
    exit 1
}
Write-Host ""

# 3. Restart Windows Server (optional - prompts user)
Write-Host "[3/3] Windows server needs restart to use new binary" -ForegroundColor Yellow
$restart = Read-Host "Restart Windows KVStoreServer now? (y/n)"
if ($restart -eq 'y') {
    Write-Host "Stopping existing server..." -ForegroundColor Yellow
    ssh azureuser@$windowsVM "Stop-Process -Name KVStoreServer -Force -ErrorAction SilentlyContinue"
    Start-Sleep -Seconds 2
    
    Write-Host "Starting new server on port 8085..." -ForegroundColor Yellow
    ssh azureuser@$windowsVM "Start-Process powershell -ArgumentList '-NoExit', '-Command', 'cd ~/kvstore; .\KVStoreServer.exe --port 8085'"
    
    Write-Host "  ✓ Server restarted" -ForegroundColor Green
} else {
    Write-Host "  ⚠ Remember to manually restart server when ready" -ForegroundColor Yellow
}
Write-Host ""

Write-Host "===== Deployment Complete =====" -ForegroundColor Green
Write-Host "Server: azureuser@$windowsVM:~/kvstore/KVStoreServer.exe" -ForegroundColor Gray
Write-Host "Client: azureuser@$linuxVM:~/kvgrpc/KVPlayground" -ForegroundColor Gray
Write-Host ""
Write-Host "To test, run: .\run_azure_linux.ps1" -ForegroundColor Cyan
