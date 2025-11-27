# Run KVPlayground from WSL to Azure Windows Server

$serverIP = "172.179.242.186:8085"

Write-Host "===== Running KVPlayground from WSL to Azure Server =====" -ForegroundColor Cyan
Write-Host "Server: $serverIP" -ForegroundColor Yellow
Write-Host ""

wsl bash run_local_wsl.sh
