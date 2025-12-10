# Deploy updated KVStoreServer to Windows Azure VM

$windowsVM = "172.179.242.186"

Write-Host "===== Deploying KVStoreServer to Windows VM =====" -ForegroundColor Cyan
Write-Host "Target: $windowsVM" -ForegroundColor Yellow
Write-Host ""

# Deploy Server to Windows VM
Write-Host "Copying KVStoreServer.exe..." -ForegroundColor Yellow
scp C:\Users\kraman\source\KVStoreV2\KVService\build\Release\KVStoreServer.exe azureuser@${windowsVM}:~/kvstore/
if ($LASTEXITCODE -eq 0) {
    Write-Host "  ✓ Server deployed successfully" -ForegroundColor Green
} else {
    Write-Host "  ✗ Server deployment failed" -ForegroundColor Red
    exit 1
}
Write-Host ""

# Deploy service configuration file
Write-Host "Copying service-config.json..." -ForegroundColor Yellow
scp C:\Users\kraman\source\KVStoreV2\KVService\service-config.json azureuser@${windowsVM}:~/kvstore/
if ($LASTEXITCODE -eq 0) {
    Write-Host "  ✓ Config file deployed successfully" -ForegroundColor Green
} else {
    Write-Host "  ✗ Config file deployment failed" -ForegroundColor Red
    exit 1
}
Write-Host ""

# Restart Server (optional - prompts user)
Write-Host "Server needs restart to use new binary" -ForegroundColor Yellow
$restart = Read-Host "Restart KVStoreServer now? (y/n)"
if ($restart -eq 'y') {
    Write-Host "Stopping existing server..." -ForegroundColor Yellow
    ssh azureuser@$windowsVM "Stop-Process -Name KVStoreServer -Force -ErrorAction SilentlyContinue"
    Start-Sleep -Seconds 2
    
    Write-Host "Starting new server on port 8085..." -ForegroundColor Yellow
    ssh azureuser@$windowsVM "Start-Process powershell -ArgumentList '-NoExit', '-Command', 'cd ~/kvstore; .\KVStoreServer.exe --port 8085 --config service-config.json'"
    
    Write-Host "  ✓ Server restarted" -ForegroundColor Green
} else {
    Write-Host "  ⚠ Remember to manually restart server when ready" -ForegroundColor Yellow
}
Write-Host ""

Write-Host "===== Deployment Complete =====" -ForegroundColor Green
Write-Host "Location: azureuser@${windowsVM}:~/kvstore/KVStoreServer.exe" -ForegroundColor Gray
Write-Host "Config:   azureuser@${windowsVM}:~/kvstore/service-config.json" -ForegroundColor Gray
