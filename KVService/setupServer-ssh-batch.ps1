# setupServer-ssh-batch.ps1
# Deploy KVStoreServer using a single SSH session

param(
    [Parameter(Mandatory=$true)]
    [string]$ConfigFile = "azure-deploy-config.json"
)

$ErrorActionPreference = "Stop"

# SSH key path
$sshKeyPath = "$HOME\.ssh\id_rsa"

# Load configuration
$config = Get-Content $ConfigFile -Raw | ConvertFrom-Json
$vmPublicIp = $config.azure.vmPublicIp
$vmAdminUsername = $config.azure.vmAdminUsername
$installPath = $config.server.installPath
$numInstances = $config.server.numberOfInstances
$startPort = $config.server.startingPort
$lbPort = $config.server.loadBalancerPort

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "KVStoreServer Azure Deployment (SSH Batch)" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Target: $vmAdminUsername@$vmPublicIp" -ForegroundColor Green
Write-Host "Install Path: $installPath" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Note: You'll be prompted for password once" -ForegroundColor Yellow
Write-Host ""

# Create deployment script
$deployScript = @"
`$ErrorActionPreference = 'Stop'

# Create install directory
Write-Host 'Creating installation directory...'
if (!(Test-Path '$installPath')) {
    New-Item -ItemType Directory -Path '$installPath' -Force | Out-Null
}
Write-Host 'OK: Directory ready'

# Configure firewall rules
Write-Host 'Configuring firewall...'
for (`$i = 0; `$i -lt $numInstances; `$i++) {
    `$port = $startPort + `$i
    `$ruleName = "KVStoreServer-`$port"
    New-NetFirewallRule -DisplayName `$ruleName -Direction Inbound -LocalPort `$port -Protocol TCP -Action Allow -ErrorAction SilentlyContinue | Out-Null
}
New-NetFirewallRule -DisplayName 'KVStoreLoadBalancer-$lbPort' -Direction Inbound -LocalPort $lbPort -Protocol TCP -Action Allow -ErrorAction SilentlyContinue | Out-Null
Write-Host 'OK: Firewall configured'

# Create start script
Write-Host 'Creating start script...'
`$startScript = @'
Write-Host 'Starting $numInstances KVStore server instances...' -ForegroundColor Cyan
for (`$i = 0; `$i -lt $numInstances; `$i++) {
    `$port = $startPort + `$i
    `$logFile = Join-Path '$installPath' "server_`${port}.log"
    Write-Host "Starting instance `$(`$i+1) on port `$port..."
    Start-Process -FilePath '$installPath\KVStoreServer.exe' -ArgumentList '--port',`$port,'--log-level','info' -WorkingDirectory '$installPath' -RedirectStandardOutput `$logFile -NoNewWindow
    Start-Sleep -Seconds 2
}
Write-Host 'All servers started!'
Get-Process -Name 'KVStoreServer' -ErrorAction SilentlyContinue | Format-Table Id,ProcessName -AutoSize
'@
Set-Content -Path '$installPath\start-servers.ps1' -Value `$startScript
Write-Host 'OK: start-servers.ps1 created'

# Create stop script
Write-Host 'Creating stop script...'
`$stopScript = @'
Write-Host 'Stopping all KVStore servers...' -ForegroundColor Yellow
`$processes = Get-Process -Name 'KVStoreServer' -ErrorAction SilentlyContinue
if (`$processes) {
    `$processes | Stop-Process -Force
    Write-Host "Stopped `$(`$processes.Count) server(s)"
} else {
    Write-Host 'No running servers found'
}
'@
Set-Content -Path '$installPath\stop-servers.ps1' -Value `$stopScript
Write-Host 'OK: stop-servers.ps1 created'

Write-Host 'Deployment complete! Ready to copy executable.'
"@

# Save deploy script to temp file
$tempScript = Join-Path $env:TEMP "kvstore-deploy.ps1"
$deployScript | Out-File -FilePath $tempScript -Encoding UTF8

Write-Host "Running deployment commands on VM..." -ForegroundColor Green
Write-Host ""

# Execute deployment script on remote VM
Get-Content $tempScript | ssh -i $sshKeyPath $vmAdminUsername@$vmPublicIp "powershell -ExecutionPolicy Bypass -Command -"

if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ Deployment script failed" -ForegroundColor Red
    Remove-Item $tempScript
    exit 1
}

Write-Host ""
Write-Host "✓ Deployment commands completed" -ForegroundColor Green

# Copy executable
Write-Host ""
Write-Host "Copying KVStoreServer.exe..." -ForegroundColor Green
$localExe = Join-Path $PSScriptRoot "build\Release\KVStoreServer.exe"
if (-not (Test-Path $localExe)) {
    Write-Host "✗ KVStoreServer.exe not found at $localExe" -ForegroundColor Red
    Remove-Item $tempScript
    exit 1
}

scp -i $sshKeyPath "$localExe" "${vmAdminUsername}@${vmPublicIp}:${installPath}/KVStoreServer.exe"
if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ KVStoreServer.exe copied" -ForegroundColor Green
} else {
    Write-Host "✗ Failed to copy file" -ForegroundColor Red
    Remove-Item $tempScript
    exit 1
}

# Cleanup
Remove-Item $tempScript

# Summary
Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Deployment Complete!" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "To start servers:" -ForegroundColor Yellow
Write-Host "  ssh $vmAdminUsername@$vmPublicIp" -ForegroundColor White
Write-Host "  cd $installPath" -ForegroundColor White
Write-Host "  .\start-servers.ps1" -ForegroundColor White
Write-Host ""
Write-Host "To stop servers:" -ForegroundColor Yellow
Write-Host "  .\stop-servers.ps1" -ForegroundColor White
Write-Host ""
Write-Host "Client connection:" -ForegroundColor Yellow
Write-Host "  export KVSTORE_GRPC_SERVER='${vmPublicIp}:${startPort}'" -ForegroundColor White
Write-Host ""

$response = Read-Host "Start servers now? (y/n)"
if ($response -eq 'y') {
    Write-Host ""
    Write-Host "Starting servers..." -ForegroundColor Green
    ssh -i $sshKeyPath $vmAdminUsername@$vmPublicIp "powershell -Command `"cd '$installPath'; .\start-servers.ps1`""
    Write-Host "✓ Done!" -ForegroundColor Green
}

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
