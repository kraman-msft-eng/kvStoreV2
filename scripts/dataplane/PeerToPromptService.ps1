<#
.SYNOPSIS
    Peer your VNet to the Azure Prompt Service for private connectivity.
    Run this script on the customer side to complete bidirectional VNet peering.

.DESCRIPTION
    This script creates a VNet peering from your VNet to the Azure Prompt Service VNet.
    The Azure Prompt Service team must have already created their side of the peering
    using AddClientVNetToRegion.ps1.

    After successful peering, you can access the Prompt Service via the internal 
    load balancer endpoint provided by the service team.

.PARAMETER PromptServiceVNetId
    The full Azure Resource ID of the Prompt Service VNet.
    This will be provided by the Azure Prompt Service team.
    Example: /subscriptions/<sub-id>/resourceGroups/<rg>/providers/Microsoft.Network/virtualNetworks/<vnet-name>

.PARAMETER ClientVNetName
    The name of your VNet that will be peered to the Prompt Service.

.PARAMETER ClientVNetResourceGroup
    The resource group containing your VNet.

.PARAMETER PeeringName
    Optional. Custom name for the peering. Defaults to "peer-to-prompt-service"

.EXAMPLE
    .\PeerToPromptService.ps1 `
        -PromptServiceVNetId "/subscriptions/xxx/resourceGroups/azureprompt_dp/providers/Microsoft.Network/virtualNetworks/kvstore-vnet-westus2" `
        -ClientVNetName "my-app-vnet" `
        -ClientVNetResourceGroup "my-resource-group"

.NOTES
    Prerequisites:
    - Azure CLI installed and logged in
    - Network Contributor permissions on your VNet
    - The Azure Prompt Service team must have created their side of the peering first

    After successful peering:
    - Your applications can reach the Prompt Service at the internal LB endpoint (e.g., 10.0.0.4:8085)
    - Ensure your NSG rules allow outbound traffic on the gRPC port (typically 8085)
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PromptServiceVNetId,

    [Parameter(Mandatory = $true)]
    [string]$ClientVNetName,

    [Parameter(Mandatory = $true)]
    [string]$ClientVNetResourceGroup,

    [Parameter(Mandatory = $false)]
    [string]$PeeringName = "peer-to-prompt-service"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "  Azure Prompt Service - VNet Peering Setup" -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan

# Validate PromptServiceVNetId format
if ($PromptServiceVNetId -notmatch "^/subscriptions/[^/]+/resourceGroups/[^/]+/providers/Microsoft.Network/virtualNetworks/[^/]+$") {
    throw "Invalid PromptServiceVNetId format. Expected: /subscriptions/<sub-id>/resourceGroups/<rg>/providers/Microsoft.Network/virtualNetworks/<vnet-name>"
}

# Extract Prompt Service VNet name from the resource ID
$promptServiceVNetName = ($PromptServiceVNetId -split "/")[-1]
Write-Host "  Prompt Service VNet: $promptServiceVNetName" -ForegroundColor White
Write-Host "  Your VNet: $ClientVNetName (RG: $ClientVNetResourceGroup)" -ForegroundColor White

# Check if customer VNet exists
Write-Host "`nValidating your VNet..." -ForegroundColor Yellow
$clientVNetExists = az network vnet show --name $ClientVNetName --resource-group $ClientVNetResourceGroup --query "id" -o tsv 2>$null
if (-not $clientVNetExists) {
    throw "VNet '$ClientVNetName' not found in resource group '$ClientVNetResourceGroup'. Please verify the VNet name and resource group."
}
Write-Host "  Your VNet validated: $ClientVNetName" -ForegroundColor Green

# Get your VNet address space
$clientAddressSpace = az network vnet show --name $ClientVNetName --resource-group $ClientVNetResourceGroup --query "addressSpace.addressPrefixes" -o json | ConvertFrom-Json
Write-Host "  Your VNet Address Space: $($clientAddressSpace -join ', ')" -ForegroundColor White

# Check if peering already exists
Write-Host "`nChecking for existing peering..." -ForegroundColor Yellow
$existingPeering = az network vnet peering show --name $PeeringName --resource-group $ClientVNetResourceGroup --vnet-name $ClientVNetName --query "peeringState" -o tsv 2>$null

if ($existingPeering) {
    Write-Host "  Peering '$PeeringName' already exists with state: $existingPeering" -ForegroundColor Yellow
    
    if ($existingPeering -eq "Connected") {
        Write-Host "  Peering is already connected. No action needed." -ForegroundColor Green
        Write-Host "`n================================================================================" -ForegroundColor Cyan
        Write-Host "  VNet Peering Active" -ForegroundColor Green
        Write-Host "================================================================================" -ForegroundColor Cyan
        Write-Host "`n  You can now access the Prompt Service via the internal load balancer endpoint" -ForegroundColor White
        Write-Host "  provided by the Azure Prompt Service team." -ForegroundColor White
        exit 0
    } elseif ($existingPeering -eq "Initiated") {
        Write-Host "  Peering is in 'Initiated' state. Waiting for Prompt Service side to complete." -ForegroundColor Yellow
        Write-Host "  Please contact the Azure Prompt Service team to create their side of the peering." -ForegroundColor Yellow
        exit 0
    } elseif ($existingPeering -eq "Disconnected") {
        Write-Host "  Peering is disconnected. Deleting and recreating..." -ForegroundColor Yellow
        az network vnet peering delete --name $PeeringName --resource-group $ClientVNetResourceGroup --vnet-name $ClientVNetName
    }
}

# Check for overlapping address space with existing peerings
Write-Host "`nChecking for address space conflicts with existing peerings..." -ForegroundColor Yellow
$existingPeerings = az network vnet peering list --resource-group $ClientVNetResourceGroup --vnet-name $ClientVNetName --query "[].{Name:name, State:peeringState, RemoteVnet:remoteVirtualNetwork.id}" -o json | ConvertFrom-Json

foreach ($peering in $existingPeerings) {
    if ($peering.State -eq "Disconnected") {
        Write-Host "  Found disconnected peering: $($peering.Name)" -ForegroundColor Yellow
        Write-Host "  Consider deleting stale peerings to avoid address space conflicts:" -ForegroundColor Yellow
        Write-Host "    az network vnet peering delete --name `"$($peering.Name)`" --resource-group `"$ClientVNetResourceGroup`" --vnet-name `"$ClientVNetName`"" -ForegroundColor Gray
    }
}

# Create peering from customer VNet to Prompt Service VNet
Write-Host "`nCreating VNet peering: $ClientVNetName -> $promptServiceVNetName" -ForegroundColor Yellow

try {
    $peeringResult = az network vnet peering create `
        --name $PeeringName `
        --resource-group $ClientVNetResourceGroup `
        --vnet-name $ClientVNetName `
        --remote-vnet $PromptServiceVNetId `
        --allow-vnet-access true `
        --allow-forwarded-traffic true `
        -o json 2>&1

    if ($LASTEXITCODE -ne 0) {
        # Check for common errors
        if ($peeringResult -match "VnetAddressSpaceOverlapsWithAlreadyPeeredVnet") {
            Write-Host "`n  ERROR: Address space conflict detected!" -ForegroundColor Red
            Write-Host "  Your VNet has an existing peering to a VNet with overlapping address space." -ForegroundColor Red
            Write-Host "`n  To resolve this:" -ForegroundColor Yellow
            Write-Host "  1. Delete any disconnected/stale peerings from your VNet" -ForegroundColor White
            Write-Host "  2. Ensure no other peered VNets use the same address space as the Prompt Service VNet" -ForegroundColor White
            throw "Address space conflict - see above for resolution steps"
        }
        throw "Failed to create VNet peering: $peeringResult"
    }

    $peeringObj = $peeringResult | ConvertFrom-Json
    $peeringState = $peeringObj.peeringState
    
} catch {
    throw "Failed to create VNet peering: $_"
}

Write-Host "  Peering created successfully!" -ForegroundColor Green
Write-Host "  Peering State: $peeringState" -ForegroundColor White

# Final status check
Write-Host "`n================================================================================" -ForegroundColor Cyan
if ($peeringState -eq "Connected") {
    Write-Host "  VNet Peering Complete!" -ForegroundColor Green
    Write-Host "================================================================================" -ForegroundColor Cyan
    Write-Host "`n  Your VNet is now peered to the Azure Prompt Service." -ForegroundColor White
    Write-Host "  You can access the service via the internal load balancer endpoint" -ForegroundColor White
    Write-Host "  provided by the Azure Prompt Service team." -ForegroundColor White
    Write-Host "`n  Typical endpoint format: <private-ip>:8085" -ForegroundColor Gray
    Write-Host "  Example: 10.0.0.4:8085" -ForegroundColor Gray
} else {
    Write-Host "  VNet Peering Created - Waiting for Service Side" -ForegroundColor Yellow
    Write-Host "================================================================================" -ForegroundColor Cyan
    Write-Host "`n  Your peering has been created with state: $peeringState" -ForegroundColor White
    Write-Host "  The Azure Prompt Service team needs to complete their side of the peering." -ForegroundColor White
    Write-Host "`n  Please provide your VNet ID to the service team:" -ForegroundColor Yellow
    Write-Host "    $clientVNetExists" -ForegroundColor Gray
}

Write-Host "`n================================================================================" -ForegroundColor Cyan
Write-Host "  NSG Configuration Reminder" -ForegroundColor Yellow
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "`n  Ensure your Network Security Group (NSG) allows:" -ForegroundColor White
Write-Host "  - Outbound TCP traffic to the Prompt Service subnet on port 8085" -ForegroundColor White
Write-Host "  - If using a firewall, allow traffic to the internal LB private IP" -ForegroundColor White

Write-Host "`n================================================================================" -ForegroundColor Cyan
Write-Host "  Done!" -ForegroundColor Green
Write-Host "================================================================================" -ForegroundColor Cyan
