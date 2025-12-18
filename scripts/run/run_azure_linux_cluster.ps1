# Run KVPlayground from Azure Linux VM to the Service Fabric managed cluster endpoint

[CmdletBinding()]
param(
    # SSH settings
    [string]$SshKey = "C:\Users\kraman\Downloads\kvClient.pem",
    [string]$LinuxVM = "20.115.133.84",

    # Service Fabric managed cluster gRPC endpoint
    # Prefer private endpoint (VNet) by supplying -GrpcServerPrivate "<private-ip-or-dns>:<port>".
    # Use -UsePublicEndpoint to target the public endpoint instead.
    [string]$GrpcServerPrivate = "",
    [string]$GrpcServerPublic = "azpswestus20.westus2.cloudapp.azure.com:50051",
    [switch]$UsePublicEndpoint,

    # KVPlayground parameters
    [string]$AccountName = "aoaikv",
    [string]$ContainerName = "gpt41-promptcache",
    [int]$Iterations = 10000,
    [int]$Concurrency = 10,
    [string]$ConversationFile = "conversation_tokens.json",
    [string]$LogLevel = "error",
    [string]$GrpcVerbosity = "error"
)

$serviceBase = "https://$AccountName.prompts.azure.net"

$targetGrpcServer = if ($UsePublicEndpoint) {
    $GrpcServerPublic
} else {
    $GrpcServerPrivate
}

if ([string]::IsNullOrWhiteSpace($targetGrpcServer)) {
    Write-Error "No gRPC target specified. Provide -GrpcServerPrivate '<private-ip-or-dns>:<port>' (recommended) or pass -UsePublicEndpoint to use '$GrpcServerPublic'."
    exit 2
}

Write-Host "===== Running KVPlayground from Azure Linux VM to SF Managed Cluster =====" -ForegroundColor Cyan
Write-Host "Linux VM: $LinuxVM" -ForegroundColor Yellow
Write-Host "gRPC Server: $targetGrpcServer" -ForegroundColor Yellow
Write-Host "Service Base: $serviceBase" -ForegroundColor Yellow
Write-Host "Container: $ContainerName" -ForegroundColor Yellow
Write-Host "Iterations: $Iterations, Concurrency: $Concurrency" -ForegroundColor Yellow
Write-Host "" 

# KVClient/KVPlayground reads target gRPC server from KVSTORE_GRPC_SERVER
ssh -i $SshKey azureuser@$LinuxVM "cd ~/kvgrpc && export KVSTORE_GRPC_SERVER='$targetGrpcServer' && export GRPC_VERBOSITY=$GrpcVerbosity && ./KVPlayground $ConversationFile $Iterations $Concurrency -s $serviceBase -c $ContainerName --log-level $LogLevel";
