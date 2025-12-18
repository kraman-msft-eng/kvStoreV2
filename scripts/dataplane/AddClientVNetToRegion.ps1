<#
.SYNOPSIS
    Adds a client VNet peering to the KVStore data plane region.
    Run this script on the KVStore service side to allow a customer VNet to connect.

.DESCRIPTION
    This script creates a VNet peering from the KVStore cluster VNet to a customer's VNet.
    The customer must also run PeerToPromptService.ps1 on their side to complete the bidirectional peering.

.PARAMETER ClientVNetId
    The full Azure Resource ID of the customer's VNet.
    Example: /subscriptions/<sub-id>/resourceGroups/<rg>/providers/Microsoft.Network/virtualNetworks/<vnet-name>

.PARAMETER RegionName
    The region name (e.g., westus2, eastus)

.PARAMETER PeeringName
    Optional. Custom name for the peering. Defaults to "peer-to-<customer-vnet-name>"

.EXAMPLE
    .\AddClientVNetToRegion.ps1 -ClientVNetId "/subscriptions/xxx/resourceGroups/customer-rg/providers/Microsoft.Network/virtualNetworks/customer-vnet" -RegionName "westus2"

.NOTES
    Prerequisites:
    - Azure CLI installed and logged in
    - Network Contributor permissions on the KVStore VNet
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ClientVNetId,

    [Parameter(Mandatory = $true)]
    [string]$RegionName,

    [Parameter(Mandatory = $false)]
    [string]$PeeringName
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# Ensure Azure CLI is in PATH
$env:PATH = "$env:PATH;C:\Program Files\Microsoft SDKs\Azure\CLI2\wbin"

Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "  KVStore Data Plane - Add Client VNet Peering" -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan

# Validate ClientVNetId format
if ($ClientVNetId -notmatch "^/subscriptions/[^/]+/resourceGroups/[^/]+/providers/Microsoft.Network/virtualNetworks/[^/]+$") {
    throw "Invalid ClientVNetId format. Expected: /subscriptions/<sub-id>/resourceGroups/<rg>/providers/Microsoft.Network/virtualNetworks/<vnet-name>"
}

# Extract customer VNet name from the resource ID
$clientVNetName = ($ClientVNetId -split "/")[-1]
Write-Host "  Client VNet: $clientVNetName" -ForegroundColor White

# Load region configuration
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$configDir = Join-Path (Split-Path -Parent (Split-Path -Parent $scriptDir)) "config" $RegionName
$regionConfigPath = Join-Path $configDir "region.config.json"

if (-not (Test-Path $regionConfigPath)) {
    throw "Region config not found: $regionConfigPath"
}

$regionConfig = Get-Content $regionConfigPath | ConvertFrom-Json
Write-Host "  Region: $RegionName" -ForegroundColor White

# Get KVStore VNet information
$kvstoreVNetName = $regionConfig.network.vnetName
$kvstoreVNetRg = $regionConfig.azure.resourceGroupName
$grpcPort = $regionConfig.dataplane.grpcPort

if (-not $kvstoreVNetName -or -not $kvstoreVNetRg) {
    throw "VNet configuration not found in region config. Ensure 'network.vnetName' and 'azure.resourceGroupName' are set."
}

Write-Host "  KVStore VNet: $kvstoreVNetName (RG: $kvstoreVNetRg)" -ForegroundColor White

# Check if KVStore VNet exists
$vnetExists = az network vnet show --name $kvstoreVNetName --resource-group $kvstoreVNetRg --query "id" -o tsv 2>$null
if (-not $vnetExists) {
    throw "KVStore VNet '$kvstoreVNetName' not found in resource group '$kvstoreVNetRg'"
}

# Set peering name
if (-not $PeeringName) {
    $PeeringName = "peer-to-$clientVNetName"
}

# Check if customer VNet exists (we need cross-subscription read access)
Write-Host "`nValidating customer VNet..." -ForegroundColor Yellow
$clientVNetExists = az network vnet show --ids $ClientVNetId --query "name" -o tsv 2>$null
if (-not $clientVNetExists) {
    Write-Host "  Warning: Cannot validate customer VNet. This may be due to cross-subscription permissions." -ForegroundColor Yellow
    Write-Host "  Proceeding with peering creation..." -ForegroundColor Yellow
} else {
    Write-Host "  Customer VNet validated: $clientVNetExists" -ForegroundColor Green
    
    # Get customer VNet address space to check for overlap
    $clientAddressSpace = az network vnet show --ids $ClientVNetId --query "addressSpace.addressPrefixes" -o json | ConvertFrom-Json
    $kvstoreAddressSpace = az network vnet show --name $kvstoreVNetName --resource-group $kvstoreVNetRg --query "addressSpace.addressPrefixes" -o json | ConvertFrom-Json
    
    Write-Host "  Customer VNet Address Space: $($clientAddressSpace -join ', ')" -ForegroundColor White
    Write-Host "  KVStore VNet Address Space: $($kvstoreAddressSpace -join ', ')" -ForegroundColor White
}

# Check if peering already exists
Write-Host "`nChecking for existing peering..." -ForegroundColor Yellow
$existingPeering = az network vnet peering show --name $PeeringName --resource-group $kvstoreVNetRg --vnet-name $kvstoreVNetName --query "peeringState" -o tsv 2>$null

if ($existingPeering) {
    Write-Host "  Peering '$PeeringName' already exists with state: $existingPeering" -ForegroundColor Yellow
    
    if ($existingPeering -eq "Connected") {
        Write-Host "  Peering is already connected. No action needed." -ForegroundColor Green
        exit 0
    } elseif ($existingPeering -eq "Initiated") {
        Write-Host "  Peering is in 'Initiated' state. Waiting for customer to complete their side." -ForegroundColor Yellow
        Write-Host "`n  Customer needs to run:" -ForegroundColor Cyan
        Write-Host "    .\PeerToPromptService.ps1 -RegionName `"$RegionName`" -ClientVNetName `"$clientVNetName`" -ClientVNetResourceGroup `"<their-rg>`"" -ForegroundColor White
        exit 0
    } else {
        Write-Host "  Deleting stale peering and recreating..." -ForegroundColor Yellow
        az network vnet peering delete --name $PeeringName --resource-group $kvstoreVNetRg --vnet-name $kvstoreVNetName
    }
}

# Create peering from KVStore VNet to customer VNet
Write-Host "`nCreating VNet peering: $kvstoreVNetName -> $clientVNetName" -ForegroundColor Yellow
$peeringResult = az network vnet peering create `
    --name $PeeringName `
    --resource-group $kvstoreVNetRg `
    --vnet-name $kvstoreVNetName `
    --remote-vnet $ClientVNetId `
    --allow-vnet-access true `
    --allow-forwarded-traffic true `
    -o json | ConvertFrom-Json

if ($LASTEXITCODE -ne 0) {
    throw "Failed to create VNet peering"
}

$peeringState = $peeringResult.peeringState
Write-Host "  Peering created successfully!" -ForegroundColor Green
Write-Host "  Peering State: $peeringState" -ForegroundColor White

# Display information for customer
Write-Host "`n================================================================================" -ForegroundColor Cyan
Write-Host "  Peering Created - Customer Action Required" -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan

# Get KVStore VNet ID for customer script
$kvstoreVNetId = az network vnet show --name $kvstoreVNetName --resource-group $kvstoreVNetRg --query "id" -o tsv

Write-Host "`nProvide the following information to the customer:" -ForegroundColor Yellow
Write-Host ""
Write-Host "  KVStore VNet ID (for peering):" -ForegroundColor White
Write-Host "    $kvstoreVNetId" -ForegroundColor Gray
Write-Host ""
Write-Host "  Internal Load Balancer Endpoint:" -ForegroundColor White

# Get ILB info - check if byolb config exists
if ($regionConfig.network.PSObject.Properties['internalLbIp']) {
    $ilbIp = $regionConfig.network.internalLbIp
} else {
    # Try to get from cluster config
    $clusterConfigs = Get-ChildItem -Path $configDir -Filter "*.config.json" | Where-Object { $_.Name -ne "region.config.json" }
    foreach ($clusterConfigFile in $clusterConfigs) {
        $clusterConfig = Get-Content $clusterConfigFile.FullName | ConvertFrom-Json
        if ($clusterConfig.byolb -and $clusterConfig.byolb.privateIp) {
            $ilbIp = $clusterConfig.byolb.privateIp
            break
        }
    }
}

if ($ilbIp) {
    Write-Host "    $($ilbIp):$grpcPort" -ForegroundColor Gray
} else {
    Write-Host "    <internal-lb-ip>:$grpcPort" -ForegroundColor Gray
    Write-Host "    (Check cluster config for actual IP)" -ForegroundColor Yellow
}
Write-Host ""
Write-Host "  Customer should run on their side:" -ForegroundColor Yellow
Write-Host "    .\PeerToPromptService.ps1 ``" -ForegroundColor White
Write-Host "      -PromptServiceVNetId `"$kvstoreVNetId`" ``" -ForegroundColor White
Write-Host "      -ClientVNetName `"$clientVNetName`" ``" -ForegroundColor White
Write-Host "      -ClientVNetResourceGroup `"<customer-resource-group>`"" -ForegroundColor White

Write-Host "`n================================================================================" -ForegroundColor Cyan
Write-Host "  Done!" -ForegroundColor Green
Write-Host "================================================================================" -ForegroundColor Cyan
