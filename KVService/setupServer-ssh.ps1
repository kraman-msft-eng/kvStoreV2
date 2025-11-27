# setupServer-ssh.ps1
# Deploy KVStoreServer to Azure Windows VM using SSH
# Simpler alternative to WinRM-based deployment

param(
    [Parameter(Mandatory=$true)]
    [string]$ConfigFile = "azure-deploy-config.json"
)

$ErrorActionPreference = "Stop"

# Load configuration
Write-Host "Loading configuration from $ConfigFile..." -ForegroundColor Cyan
$config = Get-Content $ConfigFile -Raw | ConvertFrom-Json

$vmPublicIp = $config.azure.vmPublicIp
$vmAdminUsername = $config.azure.vmAdminUsername
$installPath = $config.server.installPath
$numInstances = $config.server.numberOfInstances
$startPort = $config.server.startingPort
$lbPort = $config.server.loadBalancerPort

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "KVStoreServer Azure Deployment (SSH)" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Target: $vmAdminUsername@$vmPublicIp" -ForegroundColor Green
Write-Host "Install Path: $installPath" -ForegroundColor Green
Write-Host "Instances: $numInstances (ports $startPort-$($startPort+$numInstances-1))" -ForegroundColor Green
Write-Host "Load Balancer: port $lbPort" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""

# Check for SSH
$sshTest = Get-Command ssh -ErrorAction SilentlyContinue
if (-not $sshTest) {
    Write-Host "✗ SSH not found. Please install OpenSSH client" -ForegroundColor Red
    exit 1
}

# Test SSH connection
Write-Host "Testing SSH connection..." -ForegroundColor Green
$sshTestCmd = "ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no $vmAdminUsername@$vmPublicIp 'echo Connected'"
try {
    $result = Invoke-Expression $sshTestCmd 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ SSH connection successful" -ForegroundColor Green
    } else {
        Write-Host "✗ SSH connection failed" -ForegroundColor Red
        Write-Host "Make sure SSH is enabled on the VM and port 22 is open in NSG" -ForegroundColor Yellow
        exit 1
    }
} catch {
    Write-Host "✗ SSH connection failed: $_" -ForegroundColor Red
    exit 1
}

# Create remote install directory
Write-Host ""
Write-Host "Creating installation directory on remote VM..." -ForegroundColor Green
$escapedPath = $installPath -replace '\\', '\\\\'
ssh $vmAdminUsername@$vmPublicIp "powershell -Command `"if (!(Test-Path '$escapedPath')) { New-Item -ItemType Directory -Path '$escapedPath' -Force | Out-Null }`""
if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ Directory created: $installPath" -ForegroundColor Green
} else {
    Write-Host "✗ Failed to create directory" -ForegroundColor Red
    exit 1
}

# Copy KVStoreServer.exe
Write-Host ""
Write-Host "Copying KVStoreServer.exe to VM..." -ForegroundColor Green
$localExe = Join-Path $PSScriptRoot "build\Release\KVStoreServer.exe"
if (-not (Test-Path $localExe)) {
    Write-Host "✗ KVStoreServer.exe not found at $localExe" -ForegroundColor Red
    Write-Host "Please build the server first: .\build_with_local_sdk.ps1" -ForegroundColor Yellow
    exit 1
}

scp "$localExe" "${vmAdminUsername}@${vmPublicIp}:${installPath}/KVStoreServer.exe"
if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ KVStoreServer.exe copied" -ForegroundColor Green
} else {
    Write-Host "✗ Failed to copy file" -ForegroundColor Red
    exit 1
}

# Create startup scripts on remote VM
Write-Host ""
Write-Host "Creating startup scripts on VM..." -ForegroundColor Green

# Start servers script
$startScript = @"
Write-Host 'Starting $numInstances KVStore server instances...' -ForegroundColor Cyan
for (`$i = 0; `$i -lt $numInstances; `$i++) {
    `$port = $startPort + `$i
    `$logFile = Join-Path '$installPath' "server_`${port}.log"
    Write-Host "Starting instance `$(`$i+1) on port `$port..." -ForegroundColor Green
    Start-Process -FilePath '$installPath\KVStoreServer.exe' -ArgumentList '--port',`$port,'--log-level','info' -WorkingDirectory '$installPath' -RedirectStandardOutput `$logFile -NoNewWindow
    Start-Sleep -Seconds 2
}
Write-Host 'All servers started!' -ForegroundColor Green
Get-Process -Name 'KVStoreServer' -ErrorAction SilentlyContinue | Format-Table Id,ProcessName -AutoSize
"@

# Write start script to temp file then copy
$startScriptPath = "$installPath\start-servers.ps1" -replace '\\', '\\\\'
$startScript | ssh $vmAdminUsername@$vmPublicIp "powershell -Command `"Set-Content -Path '$startScriptPath' -Value `$input`""
Write-Host "✓ start-servers.ps1 created" -ForegroundColor Green

# Stop servers script  
$stopScript = @"
Write-Host 'Stopping all KVStore server instances...' -ForegroundColor Yellow
`$processes = Get-Process -Name 'KVStoreServer' -ErrorAction SilentlyContinue
if (`$processes) {
    `$processes | Stop-Process -Force
    Write-Host "Stopped `$(`$processes.Count) server instance(s)" -ForegroundColor Green
} else {
    Write-Host 'No running servers found' -ForegroundColor Yellow
}
"@

$stopScriptPath = "$installPath\stop-servers.ps1" -replace '\\', '\\\\'
$stopScript | ssh $vmAdminUsername@$vmPublicIp "powershell -Command `"Set-Content -Path '$stopScriptPath' -Value `$input`""
Write-Host "✓ stop-servers.ps1 created" -ForegroundColor Green

# Configure firewall rules
Write-Host ""
Write-Host "Configuring Windows Firewall on VM..." -ForegroundColor Green
for ($i = 0; $i -lt $numInstances; $i++) {
    $port = $startPort + $i
    $ruleName = "KVStoreServer-$port"
    ssh $vmAdminUsername@$vmPublicIp "powershell -Command `"New-NetFirewallRule -DisplayName '$ruleName' -Direction Inbound -LocalPort $port -Protocol TCP -Action Allow -ErrorAction SilentlyContinue | Out-Null`""
    Write-Host "  ✓ Firewall rule for port $port" -ForegroundColor Green
}

$lbRuleName = "KVStoreLoadBalancer-$lbPort"
ssh $vmAdminUsername@$vmPublicIp "powershell -Command `"New-NetFirewallRule -DisplayName '$lbRuleName' -Direction Inbound -LocalPort $lbPort -Protocol TCP -Action Allow -ErrorAction SilentlyContinue | Out-Null`""
Write-Host "  ✓ Firewall rule for port $lbPort" -ForegroundColor Green

# Summary
Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Deployment Complete!" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "To start servers on the VM, SSH and run:" -ForegroundColor Yellow
Write-Host "  ssh $vmAdminUsername@$vmPublicIp" -ForegroundColor White
Write-Host "  cd $installPath" -ForegroundColor White
Write-Host "  .\start-servers.ps1" -ForegroundColor White
Write-Host ""
Write-Host "To stop servers:" -ForegroundColor Yellow
Write-Host "  .\stop-servers.ps1" -ForegroundColor White
Write-Host ""
Write-Host "Client connection string:" -ForegroundColor Yellow
Write-Host "  export KVSTORE_GRPC_SERVER='${vmPublicIp}:${startPort}'" -ForegroundColor White
Write-Host ""

$response = Read-Host "Start servers now? (y/n)"
if ($response -eq 'y') {
    Write-Host ""
    Write-Host "Starting servers..." -ForegroundColor Green
    $escapedPath = $installPath -replace '\\', '\\\\'
    ssh $vmAdminUsername@$vmPublicIp "powershell -Command `"cd '$escapedPath'; .\\start-servers.ps1`""
    Write-Host "✓ Servers started!" -ForegroundColor Green
}

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
