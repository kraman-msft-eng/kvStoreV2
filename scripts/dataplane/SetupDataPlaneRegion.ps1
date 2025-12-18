<#
.SYNOPSIS
    Sets up all region-wide assets for the KVStore data plane.

.DESCRIPTION
    Creates:
    - Resource Group (if not exists)
    - User-Assigned Managed Identity (UAMI) for the data plane services
    - Key Vault for storing certificates and secrets
    - Server and Client certificates (self-signed for dev/test)
    - VNet and Subnet for the data plane
    - Assigns UAMI roles for storage access
    - Outputs region.config.json with all resource references

.PARAMETER RegionName
    Azure region name (e.g., westus2, eastus)

.PARAMETER SubscriptionId
    Azure subscription ID

.PARAMETER ResourceGroupName
    Name of the resource group to create/use

.PARAMETER ConfigStorageAccount
    Name of the storage account that holds dataplane configuration metadata

.PARAMETER ConfigContainer
    Name of the container in the config storage account (default: dataplane-metadata)

.EXAMPLE
    .\SetupDataPlaneRegion.ps1 -RegionName westus2 -SubscriptionId "6cbd0699-..." -ResourceGroupName "kvstore-dp-westus2"
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RegionName,

    [Parameter(Mandatory = $true)]
    [string]$SubscriptionId,

    [Parameter(Mandatory = $true)]
    [string]$ResourceGroupName,

    [Parameter(Mandatory = $false)]
    [string]$ConfigStorageAccount = "",

    [Parameter(Mandatory = $false)]
    [string]$ConfigContainer = "dataplane-metadata",

    [Parameter(Mandatory = $false)]
    [string]$VNetAddressPrefix = "10.0.0.0/16",

    [Parameter(Mandatory = $false)]
    [string]$SubnetAddressPrefix = "10.0.0.0/24",

    [Parameter(Mandatory = $false)]
    [string]$UserContentResourceGroup = "",

    [Parameter(Mandatory = $false)]
    [switch]$SkipCertificates
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# ============================================================================
# Configuration
# ============================================================================
$timestamp = Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ"
$configOutputDir = Join-Path $PSScriptRoot "..\..\config\$RegionName"
$regionConfigFile = Join-Path $configOutputDir "region.config.json"

# Naming conventions
$uamiName = "kvstore-svc-$RegionName"
$keyVaultName = "kvstore-kv-$RegionName"
$vnetName = "kvstore-vnet-$RegionName"
$subnetName = "kvstore-subnet"
$nsgName = "kvstore-nsg-$RegionName"
$serverCertName = "kvstore-server-cert"
$clientCertName = "kvstore-client-cert"

# ============================================================================
# Helper Functions
# ============================================================================
function Write-Step {
    param([string]$Message)
    Write-Host "`n=== $Message ===" -ForegroundColor Cyan
}

function Invoke-AzCli {
    param([string]$Command)
    $result = Invoke-Expression "az $Command 2>&1"
    if ($LASTEXITCODE -ne 0) {
        throw "Azure CLI command failed: az $Command`n$result"
    }
    return $result
}

# ============================================================================
# Main Script
# ============================================================================
Write-Host @"
================================================================================
  KVStore Data Plane Region Setup
================================================================================
  Region:           $RegionName
  Subscription:     $SubscriptionId
  Resource Group:   $ResourceGroupName
  Output Config:    $regionConfigFile
================================================================================
"@ -ForegroundColor Green

# Set subscription context
Write-Step "Setting Azure subscription context"
az account set --subscription $SubscriptionId
if ($LASTEXITCODE -ne 0) { throw "Failed to set subscription" }
Write-Host "Using subscription: $SubscriptionId"

# Create resource group
Write-Step "Creating Resource Group"
$rgExists = az group exists --name $ResourceGroupName | ConvertFrom-Json
if (-not $rgExists) {
    az group create --name $ResourceGroupName --location $RegionName --output none
    Write-Host "Created resource group: $ResourceGroupName"
} else {
    Write-Host "Resource group already exists: $ResourceGroupName"
}

# Create User-Assigned Managed Identity
Write-Step "Creating User-Assigned Managed Identity"
$uamiJson = az identity show --name $uamiName --resource-group $ResourceGroupName 2>$null
if ($LASTEXITCODE -ne 0) {
    $uamiJson = az identity create --name $uamiName --resource-group $ResourceGroupName --location $RegionName
    Write-Host "Created UAMI: $uamiName"
} else {
    Write-Host "UAMI already exists: $uamiName"
}
$uami = $uamiJson | ConvertFrom-Json
$uamiId = $uami.id
$uamiClientId = $uami.clientId
$uamiPrincipalId = $uami.principalId
Write-Host "  Client ID: $uamiClientId"
Write-Host "  Principal ID: $uamiPrincipalId"

# Create Key Vault
Write-Step "Creating Key Vault"
$kvJson = az keyvault show --name $keyVaultName --resource-group $ResourceGroupName 2>$null
if ($LASTEXITCODE -ne 0) {
    $kvJson = az keyvault create `
        --name $keyVaultName `
        --resource-group $ResourceGroupName `
        --location $RegionName `
        --enable-rbac-authorization true `
        --sku standard
    Write-Host "Created Key Vault: $keyVaultName"
} else {
    Write-Host "Key Vault already exists: $keyVaultName"
}
$kv = $kvJson | ConvertFrom-Json
$keyVaultUri = $kv.properties.vaultUri

# Grant current user Key Vault Administrator role for certificate management
Write-Step "Granting Key Vault access"
$currentUser = az ad signed-in-user show --query id -o tsv
az role assignment create `
    --role "Key Vault Administrator" `
    --assignee $currentUser `
    --scope $kv.id `
    --output none 2>$null
Write-Host "Granted Key Vault Administrator to current user"

# Grant UAMI Key Vault Secrets User role
az role assignment create `
    --role "Key Vault Secrets User" `
    --assignee-object-id $uamiPrincipalId `
    --assignee-principal-type ServicePrincipal `
    --scope $kv.id `
    --output none 2>$null
Write-Host "Granted Key Vault Secrets User to UAMI"

# Create certificates (unless skipped)
$serverCertThumbprint = ""
$clientCertThumbprint = ""

if (-not $SkipCertificates) {
    Write-Step "Creating Certificates"
    
    # Create server certificate
    $serverCertExists = az keyvault certificate show --vault-name $keyVaultName --name $serverCertName 2>$null
    if ($LASTEXITCODE -ne 0) {
        $certPolicy = @{
            issuerParameters = @{ name = "Self" }
            keyProperties = @{
                exportable = $true
                keySize = 2048
                keyType = "RSA"
                reuseKey = $true
            }
            secretProperties = @{ contentType = "application/x-pkcs12" }
            x509CertificateProperties = @{
                keyUsage = @("digitalSignature", "keyEncipherment")
                subject = "CN=kvstore-server-$RegionName"
                validityInMonths = 12
            }
        } | ConvertTo-Json -Depth 10 -Compress
        
        $policyFile = [System.IO.Path]::GetTempFileName()
        $certPolicy | Out-File -FilePath $policyFile -Encoding utf8
        
        az keyvault certificate create `
            --vault-name $keyVaultName `
            --name $serverCertName `
            --policy "@$policyFile" `
            --output none
        
        Remove-Item $policyFile -Force
        Write-Host "Created server certificate: $serverCertName"
    } else {
        Write-Host "Server certificate already exists: $serverCertName"
    }
    
    $serverCertJson = az keyvault certificate show --vault-name $keyVaultName --name $serverCertName
    $serverCert = $serverCertJson | ConvertFrom-Json
    $serverCertThumbprint = $serverCert.x509ThumbprintHex
    Write-Host "  Server Cert Thumbprint: $serverCertThumbprint"
    
    # Create client certificate
    $clientCertExists = az keyvault certificate show --vault-name $keyVaultName --name $clientCertName 2>$null
    if ($LASTEXITCODE -ne 0) {
        $certPolicy = @{
            issuerParameters = @{ name = "Self" }
            keyProperties = @{
                exportable = $true
                keySize = 2048
                keyType = "RSA"
                reuseKey = $true
            }
            secretProperties = @{ contentType = "application/x-pkcs12" }
            x509CertificateProperties = @{
                keyUsage = @("digitalSignature", "keyEncipherment")
                subject = "CN=kvstore-client-$RegionName"
                validityInMonths = 12
            }
        } | ConvertTo-Json -Depth 10 -Compress
        
        $policyFile = [System.IO.Path]::GetTempFileName()
        $certPolicy | Out-File -FilePath $policyFile -Encoding utf8
        
        az keyvault certificate create `
            --vault-name $keyVaultName `
            --name $clientCertName `
            --policy "@$policyFile" `
            --output none
        
        Remove-Item $policyFile -Force
        Write-Host "Created client certificate: $clientCertName"
    } else {
        Write-Host "Client certificate already exists: $clientCertName"
    }
    
    $clientCertJson = az keyvault certificate show --vault-name $keyVaultName --name $clientCertName
    $clientCert = $clientCertJson | ConvertFrom-Json
    $clientCertThumbprint = $clientCert.x509ThumbprintHex
    Write-Host "  Client Cert Thumbprint: $clientCertThumbprint"
}

# Create VNet and Subnet
Write-Step "Creating Virtual Network"
$vnetJson = az network vnet show --name $vnetName --resource-group $ResourceGroupName 2>$null
if ($LASTEXITCODE -ne 0) {
    az network vnet create `
        --name $vnetName `
        --resource-group $ResourceGroupName `
        --location $RegionName `
        --address-prefix $VNetAddressPrefix `
        --subnet-name $subnetName `
        --subnet-prefix $SubnetAddressPrefix `
        --output none
    Write-Host "Created VNet: $vnetName with subnet: $subnetName"
} else {
    Write-Host "VNet already exists: $vnetName"
}

$vnetJson = az network vnet show --name $vnetName --resource-group $ResourceGroupName
$vnet = $vnetJson | ConvertFrom-Json
$subnetId = ($vnet.subnets | Where-Object { $_.name -eq $subnetName }).id

# Create NSG
Write-Step "Creating Network Security Group"
$nsgJson = az network nsg show --name $nsgName --resource-group $ResourceGroupName 2>$null
if ($LASTEXITCODE -ne 0) {
    az network nsg create `
        --name $nsgName `
        --resource-group $ResourceGroupName `
        --location $RegionName `
        --output none
    Write-Host "Created NSG: $nsgName"
    
    # Add gRPC port rule
    az network nsg rule create `
        --nsg-name $nsgName `
        --resource-group $ResourceGroupName `
        --name "Allow-gRPC-8085" `
        --priority 100 `
        --direction Inbound `
        --access Allow `
        --protocol Tcp `
        --destination-port-ranges 8085 `
        --output none
    Write-Host "Added NSG rule for port 8085"
} else {
    Write-Host "NSG already exists: $nsgName"
}

# Grant UAMI "Managed Identity Operator" to SF Resource Provider
Write-Step "Granting SF Resource Provider access to UAMI"
$sfRpPrincipalId = az ad sp list --display-name "Azure Service Fabric Resource Provider" --query "[0].id" -o tsv 2>$null
if (-not $sfRpPrincipalId) {
    # Fallback to the well-known SF RP application ID
    $sfRpPrincipalId = az ad sp show --id "74cb6831-0dbb-4be1-8206-fd4df301cdc2" --query id -o tsv 2>$null
}
if ($sfRpPrincipalId) {
    az role assignment create `
        --assignee-object-id $sfRpPrincipalId `
        --assignee-principal-type ServicePrincipal `
        --role "Managed Identity Operator" `
        --scope $uamiId `
        --output none 2>$null
    Write-Host "Granted Managed Identity Operator to SF Resource Provider (Principal: $sfRpPrincipalId)"
} else {
    Write-Warning "Could not find SF Resource Provider principal ID"
}

# Grant UAMI storage access if ConfigStorageAccount is specified
if ($ConfigStorageAccount) {
    Write-Step "Granting UAMI access to config storage account"
    $storageId = az storage account show --name $ConfigStorageAccount --query id -o tsv 2>$null
    if ($storageId) {
        az role assignment create `
            --role "Storage Blob Data Reader" `
            --assignee-object-id $uamiPrincipalId `
            --assignee-principal-type ServicePrincipal `
            --scope $storageId `
            --output none 2>$null
        Write-Host "Granted Storage Blob Data Reader on $ConfigStorageAccount"
    } else {
        Write-Warning "Config storage account not found: $ConfigStorageAccount"
    }
}

# Grant UAMI Storage Blob Data Contributor on user content resource group
if ($UserContentResourceGroup) {
    Write-Step "Granting UAMI access to user content resource group"
    $rgId = az group show --name $UserContentResourceGroup --query id -o tsv 2>$null
    if ($rgId) {
        az role assignment create `
            --role "Storage Blob Data Contributor" `
            --assignee-object-id $uamiPrincipalId `
            --assignee-principal-type ServicePrincipal `
            --scope $rgId `
            --output none 2>$null
        Write-Host "Granted Storage Blob Data Contributor on RG: $UserContentResourceGroup"
    } else {
        Write-Warning "User content resource group not found: $UserContentResourceGroup"
    }
}

# Create output config directory
if (-not (Test-Path $configOutputDir)) {
    New-Item -ItemType Directory -Path $configOutputDir -Force | Out-Null
}

# Generate region config file
Write-Step "Generating Region Config File"
$regionConfig = @{
    schemaVersion = 2
    generatedOnUtc = $timestamp
    region = $RegionName
    azure = @{
        subscriptionId = $SubscriptionId
        resourceGroupName = $ResourceGroupName
        location = $RegionName
    }
    identity = @{
        uamiName = $uamiName
        uamiResourceId = $uamiId
        uamiClientId = $uamiClientId
        uamiPrincipalId = $uamiPrincipalId
        sfRpPrincipalId = $sfRpPrincipalId
    }
    keyVault = @{
        name = $keyVaultName
        uri = $keyVaultUri
        serverCertName = $serverCertName
        serverCertThumbprint = $serverCertThumbprint
        clientCertName = $clientCertName
        clientCertThumbprint = $clientCertThumbprint
    }
    network = @{
        vnetName = $vnetName
        vnetAddressPrefix = $VNetAddressPrefix
        subnetName = $subnetName
        subnetAddressPrefix = $SubnetAddressPrefix
        subnetId = $subnetId
        nsgName = $nsgName
    }
    dataplane = @{
        configStorageAccount = $ConfigStorageAccount
        configContainer = $ConfigContainer
        userContentResourceGroup = $UserContentResourceGroup
        grpcPort = 8085
    }
    clusters = @()
}

$regionConfig | ConvertTo-Json -Depth 10 | Out-File -FilePath $regionConfigFile -Encoding utf8
Write-Host "Saved region config to: $regionConfigFile"

# Summary
Write-Host @"

================================================================================
  Region Setup Complete!
================================================================================
  Region:                 $RegionName
  Resource Group:         $ResourceGroupName
  UAMI:                   $uamiName ($uamiClientId)
  Key Vault:              $keyVaultName
  VNet:                   $vnetName / $subnetName
  Server Cert Thumbprint: $serverCertThumbprint
  Client Cert Thumbprint: $clientCertThumbprint
  Config File:            $regionConfigFile
================================================================================

Next Steps:
  1. Run AddCluster.ps1 to create a Service Fabric cluster in this region
  2. Run DeployApp.ps1 to deploy the KVStore server to the cluster

"@ -ForegroundColor Green
