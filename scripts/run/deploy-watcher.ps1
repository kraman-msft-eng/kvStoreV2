# Deploy KV Test Watcher to Linux VM
# Usage: .\deploy-watcher.ps1 -SshKeyPath "path\to\key.pem" -VmIp "20.115.133.84"

param(
    [Parameter(Mandatory=$true)]
    [string]$SshKeyPath,
    
    [string]$VmIp = "20.115.133.84",
    [string]$VmUser = "azureuser",
    [string]$StorageAccount = "promptdatawestus2"
)

Write-Host "===== Deploying KV Test Watcher =====" -ForegroundColor Cyan
Write-Host "VM: $VmUser@$VmIp" -ForegroundColor Yellow
Write-Host "SSH Key: $SshKeyPath" -ForegroundColor Yellow
Write-Host ""

# Check if SSH key exists
if (-not (Test-Path $SshKeyPath)) {
    Write-Host "ERROR: SSH key not found at: $SshKeyPath" -ForegroundColor Red
    exit 1
}

$scriptPath = Join-Path $PSScriptRoot "kvtest-watcher.sh"
if (-not (Test-Path $scriptPath)) {
    Write-Host "ERROR: Watcher script not found at: $scriptPath" -ForegroundColor Red
    exit 1
}

Write-Host "Step 1: Uploading watcher script to VM..." -ForegroundColor Green
scp -i $SshKeyPath $scriptPath "${VmUser}@${VmIp}:~/kvtest-watcher.sh"
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to upload script" -ForegroundColor Red
    exit 1
}
Write-Host "✓ Script uploaded" -ForegroundColor Green
Write-Host ""

Write-Host "Step 2: Setting up permissions and environment..." -ForegroundColor Green
$setupCommands = @"
chmod +x ~/kvtest-watcher.sh
echo 'export STORAGE_ACCOUNT=$StorageAccount' >> ~/.bashrc
source ~/.bashrc
echo 'Watcher script is ready!'
echo 'Location: ~/kvtest-watcher.sh'
ls -lh ~/kvtest-watcher.sh
"@

ssh -i $SshKeyPath "${VmUser}@${VmIp}" $setupCommands
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to setup script" -ForegroundColor Red
    exit 1
}
Write-Host "✓ Setup complete" -ForegroundColor Green
Write-Host ""

Write-Host "Step 3: Creating containers in blob storage..." -ForegroundColor Green
az storage container create --name kvtest-commands --account-name $StorageAccount --auth-mode login --only-show-errors
az storage container create --name kvtest-results --account-name $StorageAccount --auth-mode login --only-show-errors
Write-Host "✓ Storage containers ready" -ForegroundColor Green
Write-Host ""

Write-Host "===== Deployment Complete =====" -ForegroundColor Cyan
Write-Host ""
Write-Host "To start the watcher on the VM, run:" -ForegroundColor Yellow
Write-Host "  ssh -i $SshKeyPath ${VmUser}@${VmIp}" -ForegroundColor White
Write-Host "  ./kvtest-watcher.sh" -ForegroundColor White
Write-Host ""
Write-Host "Or run in background:" -ForegroundColor Yellow
Write-Host "  ssh -i $SshKeyPath ${VmUser}@${VmIp} 'nohup ./kvtest-watcher.sh > watcher.log 2>&1 &'" -ForegroundColor White
Write-Host ""
Write-Host "To check if it's running:" -ForegroundColor Yellow
Write-Host "  ssh -i $SshKeyPath ${VmUser}@${VmIp} 'ps aux | grep kvtest-watcher'" -ForegroundColor White
Write-Host ""
