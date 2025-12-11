# Start KV Test Watcher on Linux VM
# This script kills any existing watcher processes and starts a fresh foreground run

param(
    [string]$SshKeyPath = "C:\Users\kraman\Downloads\kvClient.pem",
    [string]$VmIp = "20.115.133.84",
    [string]$VmUser = "azureuser"
)

Write-Host "===== KV Test Watcher Manager =====" -ForegroundColor Cyan
Write-Host ""

# Check if SSH key exists
if (-not (Test-Path $SshKeyPath)) {
    Write-Host "ERROR: SSH key not found at: $SshKeyPath" -ForegroundColor Red
    exit 1
}

# Kill any existing watcher processes
Write-Host "Checking for existing watcher processes..." -ForegroundColor Yellow
$killResult = ssh -i $SshKeyPath "${VmUser}@${VmIp}" "pkill -f kvtest-watcher 2>/dev/null; sleep 1; ps aux | grep kvtest-watcher | grep -v grep || echo 'No watcher running'"

if ($killResult -match "No watcher running") {
    Write-Host "No existing watcher found" -ForegroundColor Green
} else {
    Write-Host "Killed existing watcher processes" -ForegroundColor Green
}

Write-Host ""
Write-Host "Starting watcher in foreground..." -ForegroundColor Yellow
Write-Host "Press Ctrl+C to stop the watcher" -ForegroundColor Gray
Write-Host ""

# Start watcher in foreground
ssh -i $SshKeyPath "${VmUser}@${VmIp}" "export KVPLAYGROUND_PATH=/home/azureuser/kvgrpc/KVPlayground; export CONVERSATION_JSON=/home/azureuser/kvgrpc/conversation_tokens.json; /home/azureuser/kvtest-watcher.sh"
