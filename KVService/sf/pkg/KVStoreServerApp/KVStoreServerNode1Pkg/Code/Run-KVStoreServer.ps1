# Run-KVStoreServer.ps1
# Launches KVStoreServer.exe pinned to the NUMA node specified by environment variable
# With multi-NIC support: binds to specific NIC IP based on NIC_INDEX

$ErrorActionPreference = 'Continue'

# Get configuration from environment
$port = $env:KVSTORE_PORT
$numaNode = $env:KVSTORE_NUMA_NODE
$nicIndex = $env:KVSTORE_NIC_INDEX
$logLevel = if ($env:KVSTORE_LOG_LEVEL) { $env:KVSTORE_LOG_LEVEL } else { "error" }

if (-not $port) { $port = "8085" }
if (-not $numaNode) { $numaNode = "0" }
if (-not $nicIndex) { $nicIndex = "0" }

$exePath = Join-Path $PSScriptRoot "KVStoreServer.exe"

# Get the IP address for the specified NIC index
# Multi-NIC: Sort by IP address (lower IP = index 0, higher IP = index 1)
function Get-NicIpByIndex {
    param([int]$Index)
    
    # Log all adapters for debugging
    Write-Host "Available network adapters:" -ForegroundColor Cyan
    Get-NetAdapter | ForEach-Object {
        $ip = (Get-NetIPAddress -InterfaceIndex $_.InterfaceIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | 
               Where-Object { $_.PrefixOrigin -ne 'WellKnown' } | Select-Object -First 1).IPAddress
        Write-Host "  $($_.Name) (Index: $($_.InterfaceIndex), Status: $($_.Status)) - IP: $ip"
    }
    
    # Get all private IPs in 10.0.0.x range (our VNet subnet), sorted numerically by last octet
    $privateIPs = @(Get-NetIPAddress -AddressFamily IPv4 | 
        Where-Object { $_.IPAddress -like "10.0.0.*" -and $_.PrefixOrigin -eq "Dhcp" } |
        Sort-Object -Property { [int]($_.IPAddress.Split('.')[-1]) } |
        Select-Object -ExpandProperty IPAddress)
    
    Write-Host "Detected private IPs in subnet (sorted): $($privateIPs -join ', ')" -ForegroundColor Cyan
    
    if ($Index -lt $privateIPs.Count) {
        $selectedIP = $privateIPs[$Index]
        Write-Host "Selected IP $selectedIP for NIC index $Index" -ForegroundColor Green
        return $selectedIP
    }
    
    # Fallback to 0.0.0.0 - bind to all interfaces
    Write-Host "NIC index $Index not found, binding to 0.0.0.0 (all interfaces)" -ForegroundColor Yellow
    return "0.0.0.0"
}

$bindIp = Get-NicIpByIndex -Index ([int]$nicIndex)

Write-Host "=========================================="
Write-Host " KVStore Server - NUMA Node $numaNode"
Write-Host "=========================================="
Write-Host " Port:      $port"
Write-Host " Bind IP:   $bindIp (NIC index $nicIndex)"
Write-Host " NUMA Node: $numaNode"
Write-Host " Log Level: $logLevel"
Write-Host " Exe Path:  $exePath"
Write-Host "=========================================="

# Build command arguments (no --processor-group, we use start /NODE instead)
$exeArgs = "--port $port --host $bindIp --log-level $logLevel --disable-metrics"

Write-Host "Starting with NUMA affinity: start /NODE $numaNode /B /WAIT $exePath $exeArgs"

# Use Windows start command with /NODE for NUMA affinity (no special privileges needed)
# /B = no new window, /WAIT = wait for process to exit
$startArgs = "/NODE $numaNode /B /WAIT `"$exePath`" $exeArgs"
Start-Process -FilePath "cmd.exe" -ArgumentList "/c start $startArgs" -NoNewWindow -Wait

# If we get here, the process exited
Write-Host "KVStoreServer exited at $(Get-Date)"
