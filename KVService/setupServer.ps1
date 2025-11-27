# setupServer.ps1
# Deploy and configure KVStoreServer on Azure Windows VM
# Usage: .\setupServer.ps1 -ConfigFile azure-deploy-config.json

param(
    [Parameter(Mandatory=$true)]
    [string]$ConfigFile = "azure-deploy-config.json"
)

$ErrorActionPreference = "Stop"

# Load configuration
Write-Host "Loading configuration from $ConfigFile..." -ForegroundColor Cyan
if (-not (Test-Path $ConfigFile)) {
    Write-Host "ERROR: Configuration file not found: $ConfigFile" -ForegroundColor Red
    exit 1
}

$config = Get-Content $ConfigFile -Raw | ConvertFrom-Json

# Extract configuration values
$vmName = $config.azure.vmName
$resourceGroup = $config.azure.resourceGroupName
$vmAdminUsername = $config.azure.vmAdminUsername
$vmPublicIp = $config.azure.vmPublicIp
$installPath = $config.server.installPath
$numInstances = $config.server.numberOfInstances
$startPort = $config.server.startingPort
$lbPort = $config.server.loadBalancerPort
$storageUrl = $config.storage.accountUrl
$containerName = $config.storage.containerName
$nginxUrl = $config.loadBalancer.nginxDownloadUrl
$nginxPath = $config.loadBalancer.nginxInstallPath

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "KVStoreServer Azure Deployment" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Target VM: $vmName" -ForegroundColor Green
Write-Host "Resource Group: $resourceGroup" -ForegroundColor Green
Write-Host "Public IP: $vmPublicIp" -ForegroundColor Green
Write-Host "Server Instances: $numInstances" -ForegroundColor Green
Write-Host "Load Balancer Port: $lbPort" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""

# Check and configure WinRM TrustedHosts
Write-Host "Configuring WinRM TrustedHosts..." -ForegroundColor Green
try {
    # Test if WinRM is available
    $winrmTest = Test-WSMan -ComputerName localhost -ErrorAction SilentlyContinue
    if (-not $winrmTest) {
        Write-Host "Initializing WinRM..." -ForegroundColor Yellow
        winrm quickconfig -quiet
    }
    
    $currentTrustedHosts = $null
    try {
        $currentTrustedHosts = (Get-Item WSMan:\localhost\Client\TrustedHosts -ErrorAction Stop).Value
    } catch {
        # TrustedHosts not initialized yet
        $currentTrustedHosts = ""
    }
    
    if ($currentTrustedHosts -notlike "*$vmPublicIp*") {
        Write-Host "Adding $vmPublicIp to TrustedHosts..." -ForegroundColor Yellow
        if ([string]::IsNullOrEmpty($currentTrustedHosts)) {
            Set-Item WSMan:\localhost\Client\TrustedHosts -Value $vmPublicIp -Force
        } else {
            Set-Item WSMan:\localhost\Client\TrustedHosts -Value "$currentTrustedHosts,$vmPublicIp" -Force
        }
        Write-Host "✓ TrustedHosts updated" -ForegroundColor Green
    } else {
        Write-Host "✓ $vmPublicIp already in TrustedHosts" -ForegroundColor Green
    }
} catch {
    Write-Host "✗ Failed to configure WinRM. Please run as Administrator" -ForegroundColor Red
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host ""
    Write-Host "Manually run these commands as Administrator:" -ForegroundColor Yellow
    Write-Host "  winrm quickconfig" -ForegroundColor White
    Write-Host "  Set-Item WSMan:\localhost\Client\TrustedHosts -Value '$vmPublicIp' -Force" -ForegroundColor White
    exit 1
}

# Prompt for credentials
Write-Host ""
Write-Host "Enter VM credentials:" -ForegroundColor Yellow
$credential = Get-Credential -UserName $vmAdminUsername -Message "Enter password for $vmAdminUsername on $vmName"

# Create remote session
Write-Host ""
Write-Host "Connecting to VM $vmName ($vmPublicIp)..." -ForegroundColor Green
try {
    $sessionOptions = New-PSSessionOption -SkipCACheck -SkipCNCheck
    $session = New-PSSession -ComputerName $vmPublicIp -Credential $credential -SessionOption $sessionOptions -ErrorAction Stop
    Write-Host "✓ Connected successfully" -ForegroundColor Green
} catch {
    Write-Host "✗ Failed to connect to VM. Ensure WinRM is enabled on the remote VM" -ForegroundColor Red
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host ""
    Write-Host "To enable WinRM on the remote VM, SSH to it and run:" -ForegroundColor Yellow
    Write-Host "  Enable-PSRemoting -Force" -ForegroundColor White
    Write-Host "  Set-NetFirewallRule -Name 'WINRM-HTTP-In-TCP' -RemoteAddress Any" -ForegroundColor White
    exit 1
}

# Create installation directory on remote VM
Write-Host ""
Write-Host "Creating installation directory: $installPath" -ForegroundColor Green
Invoke-Command -Session $session -ScriptBlock {
    param($path)
    if (-not (Test-Path $path)) {
        New-Item -ItemType Directory -Path $path -Force | Out-Null
        Write-Host "✓ Created $path"
    } else {
        Write-Host "✓ Directory already exists: $path"
    }
} -ArgumentList $installPath

# Copy build output to VM
Write-Host ""
Write-Host "Copying KVStoreServer files to VM..." -ForegroundColor Green
$localBuildPath = Join-Path $PSScriptRoot "build\Release"
$files = @(
    "KVStoreServer.exe"
)

foreach ($file in $files) {
    $localFile = Join-Path $localBuildPath $file
    if (Test-Path $localFile) {
        Write-Host "  Copying $file..."
        Copy-Item -Path $localFile -Destination $installPath -ToSession $session -Force
        Write-Host "  ✓ $file copied"
    } else {
        Write-Host "  ✗ WARNING: $file not found at $localFile" -ForegroundColor Yellow
    }
}

# Create startup script for multiple server instances
Write-Host ""
Write-Host "Creating server startup script..." -ForegroundColor Green
$startupScript = @"
# start-kvstore-servers.ps1
# Start multiple KVStoreServer instances

`$installPath = "$installPath"
`$numInstances = $numInstances
`$startPort = $startPort
`$logLevel = "$($config.server.logLevel)"

Write-Host "Starting `$numInstances KVStore server instances..." -ForegroundColor Cyan

for (`$i = 0; `$i -lt `$numInstances; `$i++) {
    `$port = `$startPort + `$i
    `$logFile = Join-Path `$installPath "server_`$port.log"
    
    Write-Host "Starting instance `$(`$i+1) on port `$port..." -ForegroundColor Green
    
    `$processArgs = @(
        "--port", `$port,
        "--log-level", `$logLevel
    )
    
    Start-Process -FilePath (Join-Path `$installPath "KVStoreServer.exe") ``
        -ArgumentList `$processArgs ``
        -WorkingDirectory `$installPath ``
        -RedirectStandardOutput `$logFile ``
        -NoNewWindow
    
    Start-Sleep -Seconds 2
}

Write-Host ""
Write-Host "All server instances started!" -ForegroundColor Green
Write-Host "Ports: $startPort - $($startPort + $numInstances - 1)" -ForegroundColor Green

# Show running processes
Get-Process -Name "KVStoreServer" -ErrorAction SilentlyContinue | Format-Table Id, ProcessName, @{Name="Port";Expression={`$_.MainWindowTitle}} -AutoSize
"@

$startupScriptPath = Join-Path $installPath "start-kvstore-servers.ps1"
Invoke-Command -Session $session -ScriptBlock {
    param($script, $path)
    Set-Content -Path $path -Value $script -Force
    Write-Host "✓ Startup script created: $path"
} -ArgumentList $startupScript, $startupScriptPath

# Create stop script
Write-Host "Creating server stop script..." -ForegroundColor Green
$stopScript = @"
# stop-kvstore-servers.ps1
# Stop all KVStoreServer instances

Write-Host "Stopping all KVStore server instances..." -ForegroundColor Yellow

`$processes = Get-Process -Name "KVStoreServer" -ErrorAction SilentlyContinue
if (`$processes) {
    `$processes | Stop-Process -Force
    Write-Host "✓ Stopped `$(`$processes.Count) server instance(s)" -ForegroundColor Green
} else {
    Write-Host "No running KVStore server instances found" -ForegroundColor Yellow
}
"@

$stopScriptPath = Join-Path $installPath "stop-kvstore-servers.ps1"
Invoke-Command -Session $session -ScriptBlock {
    param($script, $path)
    Set-Content -Path $path -Value $script -Force
    Write-Host "✓ Stop script created: $path"
} -ArgumentList $stopScript, $stopScriptPath

# Install and configure nginx if load balancer is enabled
if ($numInstances -gt 1) {
    Write-Host ""
    Write-Host "Setting up nginx load balancer..." -ForegroundColor Green
    
    # Create nginx configuration
    $upstreamServers = ""
    for ($i = 0; $i -lt $numInstances; $i++) {
        $port = $startPort + $i
        $upstreamServers += "        server 127.0.0.1:$port;`n"
    }
    
    $nginxConfig = @"
worker_processes auto;

events {
    worker_connections 1024;
}

stream {
    upstream kvstore_backend {
$upstreamServers    }
    
    server {
        listen $lbPort;
        proxy_pass kvstore_backend;
        proxy_connect_timeout 1s;
        proxy_timeout 3s;
    }
}
"@
    
    # Copy nginx config to VM
    Invoke-Command -Session $session -ScriptBlock {
        param($config, $nginxPath, $nginxUrl)
        
        # Create nginx directory
        if (-not (Test-Path $nginxPath)) {
            New-Item -ItemType Directory -Path $nginxPath -Force | Out-Null
        }
        
        # Download nginx if not present
        $nginxExe = Join-Path $nginxPath "nginx.exe"
        if (-not (Test-Path $nginxExe)) {
            Write-Host "  Downloading nginx from $nginxUrl..."
            $zipFile = Join-Path $env:TEMP "nginx.zip"
            Invoke-WebRequest -Uri $nginxUrl -OutFile $zipFile
            
            Write-Host "  Extracting nginx..."
            Expand-Archive -Path $zipFile -DestinationPath $env:TEMP -Force
            
            # Move nginx files
            $extractedFolder = Get-ChildItem -Path $env:TEMP -Filter "nginx-*" -Directory | Select-Object -First 1
            Copy-Item -Path "$($extractedFolder.FullName)\*" -Destination $nginxPath -Recurse -Force
            
            Remove-Item $zipFile -Force
            Write-Host "  ✓ nginx installed to $nginxPath"
        } else {
            Write-Host "  ✓ nginx already installed"
        }
        
        # Write nginx config
        $confPath = Join-Path $nginxPath "conf\nginx.conf"
        Set-Content -Path $confPath -Value $config -Force
        Write-Host "  ✓ nginx configuration updated"
        
    } -ArgumentList $nginxConfig, $nginxPath, $nginxUrl
    
    # Create nginx startup script
    $nginxStartScript = @"
# start-nginx.ps1
# Start nginx load balancer

`$nginxPath = "$nginxPath"
`$nginxExe = Join-Path `$nginxPath "nginx.exe"

if (Test-Path `$nginxExe) {
    Write-Host "Starting nginx load balancer on port $lbPort..." -ForegroundColor Cyan
    Set-Location `$nginxPath
    Start-Process -FilePath `$nginxExe -NoNewWindow
    Start-Sleep -Seconds 1
    
    `$process = Get-Process -Name "nginx" -ErrorAction SilentlyContinue
    if (`$process) {
        Write-Host "✓ nginx started successfully (PID: `$(`$process.Id))" -ForegroundColor Green
    } else {
        Write-Host "✗ Failed to start nginx" -ForegroundColor Red
    }
} else {
    Write-Host "✗ nginx not found at `$nginxExe" -ForegroundColor Red
}
"@
    
    $nginxStartPath = Join-Path $installPath "start-nginx.ps1"
    Invoke-Command -Session $session -ScriptBlock {
        param($script, $path)
        Set-Content -Path $path -Value $script -Force
        Write-Host "✓ nginx startup script created: $path"
    } -ArgumentList $nginxStartScript, $nginxStartPath
}

# Configure Windows Firewall rules
Write-Host ""
Write-Host "Configuring Windows Firewall..." -ForegroundColor Green
Invoke-Command -Session $session -ScriptBlock {
    param($startPort, $numInstances, $lbPort)
    
    # Open ports for all server instances
    for ($i = 0; $i -lt $numInstances; $i++) {
        $port = $startPort + $i
        $ruleName = "KVStoreServer-$port"
        
        $existingRule = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
        if (-not $existingRule) {
            New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -LocalPort $port -Protocol TCP -Action Allow | Out-Null
            Write-Host "  ✓ Firewall rule created for port $port"
        } else {
            Write-Host "  ✓ Firewall rule already exists for port $port"
        }
    }
    
    # Open load balancer port
    $lbRuleName = "KVStoreLoadBalancer-$lbPort"
    $existingLbRule = Get-NetFirewallRule -DisplayName $lbRuleName -ErrorAction SilentlyContinue
    if (-not $existingLbRule) {
        New-NetFirewallRule -DisplayName $lbRuleName -Direction Inbound -LocalPort $lbPort -Protocol TCP -Action Allow | Out-Null
        Write-Host "  ✓ Firewall rule created for load balancer port $lbPort"
    } else {
        Write-Host "  ✓ Firewall rule already exists for load balancer port $lbPort"
    }
    
} -ArgumentList $startPort, $numInstances, $lbPort

# Create deployment summary
Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Deployment Summary" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Installation Path: $installPath" -ForegroundColor Green
Write-Host "Server Instances: $numInstances" -ForegroundColor Green
Write-Host "Server Ports: $startPort - $($startPort + $numInstances - 1)" -ForegroundColor Green
Write-Host "Load Balancer Port: $lbPort" -ForegroundColor Green
Write-Host ""
Write-Host "Scripts Created:" -ForegroundColor Yellow
Write-Host "  - Start Servers: $installPath\start-kvstore-servers.ps1" -ForegroundColor White
Write-Host "  - Stop Servers:  $installPath\stop-kvstore-servers.ps1" -ForegroundColor White
if ($numInstances -gt 1) {
    Write-Host "  - Start nginx:   $installPath\start-nginx.ps1" -ForegroundColor White
}
Write-Host ""
Write-Host "To start the servers, run on the VM:" -ForegroundColor Yellow
Write-Host "  cd $installPath" -ForegroundColor White
Write-Host "  .\start-kvstore-servers.ps1" -ForegroundColor White
if ($numInstances -gt 1) {
    Write-Host "  .\start-nginx.ps1" -ForegroundColor White
}
Write-Host ""
Write-Host "Client Connection:" -ForegroundColor Yellow
Write-Host "  export KVSTORE_GRPC_SERVER='$($vmPublicIp):$lbPort'" -ForegroundColor White
Write-Host ""

# Ask if user wants to start servers now
$response = Read-Host "Do you want to start the servers now? (y/n)"
if ($response -eq 'y' -or $response -eq 'Y') {
    Write-Host ""
    Write-Host "Starting servers on remote VM..." -ForegroundColor Green
    
    # Start servers
    Invoke-Command -Session $session -ScriptBlock {
        param($installPath)
        & "$installPath\start-kvstore-servers.ps1"
    } -ArgumentList $installPath
    
    # Start nginx if multiple instances
    if ($numInstances -gt 1) {
        Start-Sleep -Seconds 3
        Invoke-Command -Session $session -ScriptBlock {
            param($installPath)
            & "$installPath\start-nginx.ps1"
        } -ArgumentList $installPath
    }
    
    Write-Host ""
    Write-Host "✓ Servers started!" -ForegroundColor Green
}

# Clean up session
Remove-PSSession -Session $session

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Deployment Complete!" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
