# Run KVPlayground on BOTH NUMA nodes in parallel

param(
    [string]$AccountName = "aoaikv",
    [string]$ContainerName = "gpt41-promptcache",
    [int]$Iterations = 1000000,
    [int]$Concurrency = 10
)

Write-Host "===== Starting clients for BOTH NUMA nodes =====" -ForegroundColor Cyan
Write-Host "Account: $AccountName" -ForegroundColor Yellow
Write-Host "Container: $ContainerName" -ForegroundColor Yellow
Write-Host "Iterations: $Iterations, Concurrency: $Concurrency" -ForegroundColor Yellow
Write-Host "Port 8085 -> NUMA Node 0" -ForegroundColor Yellow
Write-Host "Port 8086 -> NUMA Node 1" -ForegroundColor Yellow
Write-Host ""

# Start both in parallel using PowerShell jobs
$job1 = Start-Job -ScriptBlock {
    param($AccountName, $ContainerName, $Iterations, $Concurrency)
    $sshKey = "C:\Users\kraman\Downloads\kvClient.pem"
    $linuxVM = "20.115.133.84"
    $serverIP = "172.16.0.4:8085"
    ssh -i $sshKey azureuser@$linuxVM "cd ~/kvgrpc && export KVSTORE_GRPC_SERVER='$serverIP' && export GRPC_VERBOSITY=error && ./KVPlayground conversation_tokens.json $Iterations $Concurrency -s https://$AccountName.prompts.azure.net -c $ContainerName --log-level error"
} -ArgumentList $AccountName, $ContainerName, $Iterations, $Concurrency

$job2 = Start-Job -ScriptBlock {
    param($AccountName, $ContainerName, $Iterations, $Concurrency)
    $sshKey = "C:\Users\kraman\Downloads\kvClient.pem"
    $linuxVM = "20.115.133.84"
    $serverIP = "172.16.0.4:8086"
    ssh -i $sshKey azureuser@$linuxVM "cd ~/kvgrpc && export KVSTORE_GRPC_SERVER='$serverIP' && export GRPC_VERBOSITY=error && ./KVPlayground conversation_tokens.json $Iterations $Concurrency -s https://$AccountName.prompts.azure.net -c $ContainerName --log-level error"
} -ArgumentList $AccountName, $ContainerName, $Iterations, $Concurrency

Write-Host "Waiting for both clients to complete..." -ForegroundColor Green
Write-Host ""

# Wait and show output
$job1, $job2 | Wait-Job | Receive-Job

# Cleanup
Remove-Job $job1, $job2
