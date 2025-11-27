# Run KVPlayground from Azure Linux VM to Azure Windows Server

$sshKey = "C:\Users\kraman\Downloads\kvClient.pem"
$linuxVM = "20.115.133.84"
$serverIP = "172.16.0.4:8085"

Write-Host "===== Running KVPlayground from Azure Linux VM to Azure Server =====" -ForegroundColor Cyan
Write-Host "Linux VM: $linuxVM (Private IP: 10.1.1.4)" -ForegroundColor Yellow
Write-Host "Server: $serverIP (Private IP, VNet Peered)" -ForegroundColor Yellow
Write-Host "gRPC Optimizations: TCP_NODELAY enabled" -ForegroundColor Yellow
Write-Host ""

# gRPC environment variables for latency optimization:
# - GRPC_VERBOSITY: Set to 'info' for debugging, 'error' for production
# - TCP_NODELAY is enabled by default in gRPC, but we ensure optimal settings
ssh -i $sshKey azureuser@$linuxVM "cd ~/kvgrpc && export KVSTORE_GRPC_SERVER='$serverIP' && export GRPC_VERBOSITY=error && ./KVPlayground conversation_tokens.json 10000 10 -s https://aoaikv.blob.core.windows.net -c gpt51-promptcache --log-level error"
