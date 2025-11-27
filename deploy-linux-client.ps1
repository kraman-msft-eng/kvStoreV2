# deploy-linux-client.ps1
# Deploy KVPlayground client to Linux VM

param(
    [Parameter(Mandatory=$true)]
    [string]$LinuxVmIp,
    
    [Parameter(Mandatory=$true)]
    [string]$LinuxUsername,
    
    [Parameter(Mandatory=$false)]
    [string]$SshKeyPath = "",
    
    [Parameter(Mandatory=$false)]
    [string]$RemotePath = "/home/$LinuxUsername/kvstore",
    
    [Parameter(Mandatory=$false)]
    [string]$ServerAddress = "172.179.242.186:50051",
    
    [Parameter(Mandatory=$false)]
    [string]$StorageUrl = "https://aoaikv.blob.core.windows.net",
    
    [Parameter(Mandatory=$false)]
    [string]$Container = "gpt51-promptcache"
)

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "KVPlayground Linux Client Deployment" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Target: $LinuxUsername@$LinuxVmIp" -ForegroundColor Green
Write-Host "Remote Path: $RemotePath" -ForegroundColor Green
Write-Host "Server: $ServerAddress" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""

# Build SSH/SCP command prefix
$sshPrefix = if ($SshKeyPath) { "ssh -i `"$SshKeyPath`"" } else { "ssh" }
$scpPrefix = if ($SshKeyPath) { "scp -i `"$SshKeyPath`"" } else { "scp" }

# Local build path
$buildPath = Join-Path $PSScriptRoot "build\KVPlayground"

# Check if build exists
if (-not (Test-Path "$buildPath\KVPlayground")) {
    Write-Host "✗ KVPlayground executable not found at $buildPath" -ForegroundColor Red
    Write-Host "Please build first: ./build_linux.sh" -ForegroundColor Yellow
    exit 1
}

# Create remote directory
Write-Host "Creating remote directory..." -ForegroundColor Green
$createDirCmd = "$sshPrefix $LinuxUsername@$LinuxVmIp `"mkdir -p $RemotePath`""
Invoke-Expression $createDirCmd
if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ Remote directory created" -ForegroundColor Green
} else {
    Write-Host "✗ Failed to create remote directory" -ForegroundColor Red
    exit 1
}

# Copy files
Write-Host ""
Write-Host "Copying files to Linux VM..." -ForegroundColor Green

$filesToCopy = @(
    @{ Local = "$buildPath\KVPlayground"; Remote = "$RemotePath/" }
    @{ Local = "$buildPath\chunk.bin"; Remote = "$RemotePath/" }
    @{ Local = "$buildPath\conversation_tokens.json"; Remote = "$RemotePath/" }
)

foreach ($file in $filesToCopy) {
    $fileName = Split-Path $file.Local -Leaf
    Write-Host "  Copying $fileName..." -ForegroundColor Yellow
    
    if (Test-Path $file.Local) {
        $copyCmd = "$scpPrefix `"$($file.Local)`" $LinuxUsername@${LinuxVmIp}:$($file.Remote)"
        Invoke-Expression $copyCmd
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  ✓ $fileName copied" -ForegroundColor Green
        } else {
            Write-Host "  ✗ Failed to copy $fileName" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "  ⚠ $fileName not found, skipping" -ForegroundColor Yellow
    }
}

# Make executable
Write-Host ""
Write-Host "Setting execute permissions..." -ForegroundColor Green
$chmodCmd = "$sshPrefix $LinuxUsername@$LinuxVmIp `"chmod +x $RemotePath/KVPlayground`""
Invoke-Expression $chmodCmd
if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ Execute permissions set" -ForegroundColor Green
} else {
    Write-Host "✗ Failed to set permissions" -ForegroundColor Red
}

# Create run script
Write-Host ""
Write-Host "Creating run script..." -ForegroundColor Green

$runScript = @"
#!/bin/bash
# run-kvplayground.sh
# Run KVPlayground client

export KVSTORE_GRPC_SERVER='$ServerAddress'

echo "=== KVPlayground Test Runner ==="
echo "Server: \$KVSTORE_GRPC_SERVER"
echo "Storage: $StorageUrl"
echo "Container: $Container"
echo ""

cd $RemotePath
./KVPlayground conversation_tokens.json 10 2 -s $StorageUrl -c $Container
"@

# Write run script to temp file
$tempScript = [System.IO.Path]::GetTempFileName()
Set-Content -Path $tempScript -Value $runScript -Encoding UTF8

# Copy run script
$copyScriptCmd = "$scpPrefix `"$tempScript`" $LinuxUsername@${LinuxVmIp}:$RemotePath/run-kvplayground.sh"
Invoke-Expression $copyScriptCmd

# Make run script executable
$chmodScriptCmd = "$sshPrefix $LinuxUsername@$LinuxVmIp `"chmod +x $RemotePath/run-kvplayground.sh`""
Invoke-Expression $chmodScriptCmd

# Cleanup temp file
Remove-Item $tempScript

Write-Host "✓ Run script created" -ForegroundColor Green

# Summary
Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Deployment Complete!" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "To run the client on the Linux VM:" -ForegroundColor Yellow
Write-Host "  ssh $LinuxUsername@$LinuxVmIp" -ForegroundColor White
Write-Host "  cd $RemotePath" -ForegroundColor White
Write-Host "  ./run-kvplayground.sh" -ForegroundColor White
Write-Host ""
Write-Host "Or run directly:" -ForegroundColor Yellow
Write-Host "  ssh $LinuxUsername@$LinuxVmIp `"$RemotePath/run-kvplayground.sh`"" -ForegroundColor White
Write-Host ""

# Ask to run now
$response = Read-Host "Run client on Linux VM now? (y/n)"
if ($response -eq 'y') {
    Write-Host ""
    Write-Host "Running KVPlayground on Linux VM..." -ForegroundColor Green
    Write-Host ""
    $runCmd = "$sshPrefix $LinuxUsername@$LinuxVmIp `"$RemotePath/run-kvplayground.sh`""
    Invoke-Expression $runCmd
}

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
