# Run KVPlayground from Azure Linux VM to Azure Windows Server - NUMA Node 1

$sshKey = "C:\Users\kraman\Downloads\kvClient.pem"
$linuxVM = "20.115.133.84"
$serverIP = "172.16.0.4:8086"

Write-Host "===== Running KVPlayground - NUMA Node 1 (Port 8086) =====" -ForegroundColor Cyan
Write-Host "Linux VM: $linuxVM" -ForegroundColor Yellow
Write-Host "Server: $serverIP" -ForegroundColor Yellow
Write-Host ""

ssh -i $sshKey azureuser@$linuxVM "cd ~/kvgrpc && export KVSTORE_GRPC_SERVER='$serverIP' && export GRPC_VERBOSITY=error && ./KVPlayground conversation_tokens.json 10000 10 -s https://aoaikv.blob.core.windows.net -c gpt51-promptcache --log-level error"
