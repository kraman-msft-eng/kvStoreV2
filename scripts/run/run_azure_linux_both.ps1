# Run KVPlayground on BOTH NUMA nodes in parallel

Write-Host "===== Starting clients for BOTH NUMA nodes =====" -ForegroundColor Cyan
Write-Host "Port 8085 -> NUMA Node 0" -ForegroundColor Yellow
Write-Host "Port 8086 -> NUMA Node 1" -ForegroundColor Yellow
Write-Host ""

# Start both in parallel using PowerShell jobs
$job1 = Start-Job -ScriptBlock {
    $sshKey = "C:\Users\kraman\Downloads\kvClient.pem"
    $linuxVM = "20.115.133.84"
    $serverIP = "172.16.0.4:8085"
    ssh -i $sshKey azureuser@$linuxVM "cd ~/kvgrpc && export KVSTORE_GRPC_SERVER='$serverIP' && export GRPC_VERBOSITY=error && ./KVPlayground conversation_tokens.json 10000 10 -s https://aoaikv.blob.core.windows.net -c gpt41-promptcache --log-level error"
}

$job2 = Start-Job -ScriptBlock {
    $sshKey = "C:\Users\kraman\Downloads\kvClient.pem"
    $linuxVM = "20.115.133.84"
    $serverIP = "172.16.0.4:8086"
    ssh -i $sshKey azureuser@$linuxVM "cd ~/kvgrpc && export KVSTORE_GRPC_SERVER='$serverIP' && export GRPC_VERBOSITY=error && ./KVPlayground conversation_tokens.json 10000 10 -s https://aoaikv.blob.core.windows.net -c gpt41-promptcache --log-level error"
}

Write-Host "Waiting for both clients to complete..." -ForegroundColor Green
Write-Host ""

# Wait and show output
$job1, $job2 | Wait-Job | Receive-Job

# Cleanup
Remove-Job $job1, $job2
