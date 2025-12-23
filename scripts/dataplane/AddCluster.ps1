<#
.SYNOPSIS
    Creates a new Service Fabric Managed Cluster in a region.

.DESCRIPTION
    Creates a Service Fabric Managed Cluster with:
    - Public load balancer for system services (SFX, PowerShell management)
    - Optional: Internal load balancer for application node type (BYOVNET + BYOLB pattern)
    - Client certificate authentication
    - User-Assigned Managed Identity assigned to node type
    - Outputs cluster.config.json with cluster details

    BYOVNET + BYOLB (Bring Your Own VNet + Load Balancer) Configuration:
    When -UseInternalLoadBalancer is specified, the script:
    1. Creates an Internal LB in the user's VNet (from region config)
    2. Creates SF managed cluster WITH subnetId (BYOVNET) - deploys into user's VNet
    3. Creates Primary node type: Uses default public LB for SF system services
    4. Creates Secondary node type: Uses internal Azure LB for application traffic (private IP only)
    
    This approach ensures the VMSS and internal LB are in the SAME VNet, which is required by Azure.

.PARAMETER ClusterName
    Name of the cluster to create (e.g., kvstore-c1)

.PARAMETER RegionName
    Azure region name (must have region.config.json from SetupDataPlaneRegion)

.PARAMETER SystemVmSize
    VM size for system/primary node type (default: Standard_D2s_v5 - smaller for system services only)

.PARAMETER AppVmSize
    VM size for application node type (default: Standard_E80ids_v4)
    This isolated VM offers 80 vCPUs, 80 Gbps network, 2 NUMA nodes for dual-process pinning.
    Only used in BYOLB mode.

.PARAMETER SystemVmCount
    Number of VMs in the system node type (default: 5 for Standard SKU minimum)

.PARAMETER AppVmCount
    Number of VMs in the application node type (default: 3)
    Only used in BYOLB mode.

.PARAMETER AdminUsername
    Admin username for VMs (default: sfadmin)

.PARAMETER AdminPassword
    Admin password for VMs (required)

.PARAMETER UseInternalLoadBalancer
    If specified, creates a BYOVNET + BYOLB configuration with:
    - Cluster deployed into user's VNet (BYOVNET)
    - Primary node type for SF system services (public LB)
    - Secondary node type for applications (internal LB with private IP)

.PARAMETER InternalLbSubnetId
    Subnet ID for the internal load balancer frontend IP. Required if UseInternalLoadBalancer is specified.
    If not provided, uses the subnet from region config.

.EXAMPLE
    # Standard cluster (single node type, public LB)
    .\AddCluster.ps1 -ClusterName "kvstore-c1" -RegionName "westus2" -AdminPassword "YourSecureP@ssw0rd!"

.EXAMPLE
    # BYOLB cluster (system services on public, app on internal LB)
    .\AddCluster.ps1 -ClusterName "kvstore-c1" -RegionName "westus2" -AdminPassword "YourSecureP@ssw0rd!" -UseInternalLoadBalancer
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ClusterName,

    [Parameter(Mandatory = $true)]
    [string]$RegionName,

    [Parameter(Mandatory = $true)]
    [SecureString]$AdminPassword,

    [Parameter(Mandatory = $false)]
    [string]$SystemVmSize = "Standard_D2s_v5",

    [Parameter(Mandatory = $false)]
    [string]$AppVmSize = "Standard_E80ids_v4",

    [Parameter(Mandatory = $false)]
    [int]$SystemVmCount = 5,  # Standard SKU requires minimum 5 nodes for primary node type

    [Parameter(Mandatory = $false)]
    [int]$AppVmCount = 3,

    [Parameter(Mandatory = $false)]
    [string]$AdminUsername = "sfadmin",

    [Parameter(Mandatory = $false)]
    [string]$PrimaryNodeTypeName = "System",

    [Parameter(Mandatory = $false)]
    [string]$AppNodeTypeName = "App",

    [Parameter(Mandatory = $false)]
    [switch]$UseInternalLoadBalancer,

    [Parameter(Mandatory = $false)]
    [string]$InternalLbSubnetId = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# ============================================================================
# Load Region Config
# ============================================================================
$configDir = Join-Path $PSScriptRoot "..\..\config\$RegionName"
$regionConfigFile = Join-Path $configDir "region.config.json"
$clusterConfigFile = Join-Path $configDir "$ClusterName.config.json"

if (-not (Test-Path $regionConfigFile)) {
    throw "Region config not found: $regionConfigFile. Run SetupDataPlaneRegion.ps1 first."
}

$regionConfig = Get-Content $regionConfigFile -Raw | ConvertFrom-Json
$timestamp = Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ"

# ============================================================================
# Helper Functions
# ============================================================================
function Write-Step {
    param([string]$Message)
    Write-Host "`n=== $Message ===" -ForegroundColor Cyan
}

# ============================================================================
# Determine Configuration Mode
# ============================================================================
$configMode = if ($UseInternalLoadBalancer) { "BYOLB (Internal LB for Apps)" } else { "Standard (Public LB)" }
$nodeTypeName = if ($UseInternalLoadBalancer) { $PrimaryNodeTypeName } else { "NT1" }

# For standard mode, use AppVmSize/AppVmCount for the single node type
$effectiveVmSize = if ($UseInternalLoadBalancer) { $SystemVmSize } else { $AppVmSize }
$effectiveVmCount = if ($UseInternalLoadBalancer) { $SystemVmCount } else { $AppVmCount }

# ============================================================================
# Main Script
# ============================================================================
Write-Host @"
================================================================================
  KVStore Data Plane - Add Cluster
================================================================================
  Cluster Name:     $ClusterName
  Region:           $RegionName
  Resource Group:   $($regionConfig.azure.resourceGroupName)
  Configuration:    $configMode
$(if ($UseInternalLoadBalancer) {
"  System Node Type: $PrimaryNodeTypeName ($SystemVmSize x $SystemVmCount)
  App Node Type:    $AppNodeTypeName ($AppVmSize x $AppVmCount)"
} else {
"  Node Type:        NT1 ($AppVmSize x $AppVmCount)"
})
================================================================================
"@ -ForegroundColor Green

$subscriptionId = $regionConfig.azure.subscriptionId
$resourceGroupName = $regionConfig.azure.resourceGroupName
$location = $regionConfig.azure.location
$serverCertThumbprint = $regionConfig.keyVault.serverCertThumbprint
$clientCertThumbprint = $regionConfig.keyVault.clientCertThumbprint
$uamiResourceId = $regionConfig.identity.uamiResourceId
$keyVaultName = $regionConfig.keyVault.name

# Set subscription context
Write-Step "Setting Azure subscription context"
az account set --subscription $subscriptionId
if ($LASTEXITCODE -ne 0) { throw "Failed to set subscription" }

# Download certificates from Key Vault to local machine for cluster auth
Write-Step "Downloading certificates from Key Vault"
$certDir = Join-Path $env:TEMP "kvstore-certs"
if (-not (Test-Path $certDir)) {
    New-Item -ItemType Directory -Path $certDir -Force | Out-Null
}

# Download client certificate (we need this for cluster management)
$clientCertPfxPath = Join-Path $certDir "$($regionConfig.keyVault.clientCertName).pfx"
if (-not (Test-Path $clientCertPfxPath)) {
    $clientSecretId = az keyvault certificate show --vault-name $keyVaultName --name $($regionConfig.keyVault.clientCertName) --query "sid" -o tsv
    az keyvault secret download --id $clientSecretId --file $clientCertPfxPath --encoding base64
    Write-Host "Downloaded client certificate to: $clientCertPfxPath"
}

# Import client certificate to CurrentUser\My store
Write-Step "Importing client certificate to certificate store"
$existingCert = Get-ChildItem -Path Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq $clientCertThumbprint }
if (-not $existingCert) {
    # Key Vault certs exported as secrets have no password
    Import-PfxCertificate -FilePath $clientCertPfxPath -CertStoreLocation Cert:\CurrentUser\My | Out-Null
    Write-Host "Imported certificate with thumbprint: $clientCertThumbprint"
} else {
    Write-Host "Certificate already in store: $clientCertThumbprint"
}

# ============================================================================
# BYOVNET + BYOLB: Create Internal Load Balancer FIRST (before cluster)
# ============================================================================
# Initialize BYOLB variables
$internalLbId = $null
$internalLbPrivateIp = $null
$internalLbName = "$ClusterName-ilb"
$subnetIdForCluster = $null

if ($UseInternalLoadBalancer) {
    Write-Step "Creating Internal Azure Load Balancer for BYOLB (Step 1 of BYOVNET+BYOLB)"
    
    # BYOVNET+BYOLB pattern: Create LB in user's VNet, then create cluster IN that VNet
    # This ensures VMSS and LB are in the same VNet as required by Azure
    
    $userVnetSubnetId = $regionConfig.network.subnetId
    if ([string]::IsNullOrEmpty($userVnetSubnetId)) {
        throw "Network subnetId not found in region config. Ensure SetupDataPlaneRegion created VNet."
    }
    
    Write-Host "  User VNet Subnet: $userVnetSubnetId"
    $subnetIdForCluster = $userVnetSubnetId
    
    # Create internal load balancer in the user's resource group (same as VNet)
    $ilbJson = az network lb show --name $internalLbName --resource-group $resourceGroupName 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Creating internal load balancer: $internalLbName in $resourceGroupName"
        
        # Create the load balancer with internal frontend IP in user's VNet
        az network lb create `
            --name $internalLbName `
            --resource-group $resourceGroupName `
            --location $location `
            --sku Standard `
            --frontend-ip-name "internalFrontend" `
            --backend-pool-name "appBackendPool" `
            --subnet $userVnetSubnetId `
            --output none
        
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to create internal load balancer"
        }
        
        Write-Host "Created internal load balancer: $internalLbName"
        $ilbJson = az network lb show --name $internalLbName --resource-group $resourceGroupName
    } else {
        Write-Host "Internal load balancer already exists: $internalLbName"
    }
    
    $ilb = $ilbJson | ConvertFrom-Json
    
    # Ensure health probes exist (idempotent - check before create)
    $existingProbes = @($ilb.probes | ForEach-Object { $_.name })
    
    # Port 8085: NUMA Node 0 process
    if ($existingProbes -notcontains "grpcHealthProbe") {
        Write-Host "Creating health probe: grpcHealthProbe (port 8085)"
        az network lb probe create `
            --name "grpcHealthProbe" `
            --lb-name $internalLbName `
            --resource-group $resourceGroupName `
            --protocol Tcp `
            --port 8085 `
            --interval 5 `
            --threshold 2 `
            --output none
    } else {
        Write-Host "Health probe already exists: grpcHealthProbe"
    }
    
    # Port 8086: NUMA Node 1 process
    if ($existingProbes -notcontains "grpcHealthProbe8086") {
        Write-Host "Creating health probe: grpcHealthProbe8086 (port 8086)"
        az network lb probe create `
            --name "grpcHealthProbe8086" `
            --lb-name $internalLbName `
            --resource-group $resourceGroupName `
            --protocol Tcp `
            --port 8086 `
            --interval 5 `
            --threshold 2 `
            --output none
    } else {
        Write-Host "Health probe already exists: grpcHealthProbe8086"
    }
    
    # Create inbound NAT pool for RDP if not exists (required by SF for BYOLB)
    $existingNatPools = @($ilb.inboundNatPools | ForEach-Object { $_.name })
    if ($existingNatPools -notcontains "rdpNatPool") {
        Write-Host "Creating NAT pool: rdpNatPool"
        az network lb inbound-nat-pool create `
            --name "rdpNatPool" `
            --lb-name $internalLbName `
            --resource-group $resourceGroupName `
            --frontend-ip-name "internalFrontend" `
            --protocol Tcp `
            --frontend-port-range-start 50000 `
            --frontend-port-range-end 50119 `
            --backend-port 3389 `
            --output none
    } else {
        Write-Host "NAT pool already exists: rdpNatPool"
    }
    
    # Ensure LB rules exist (idempotent - check before create)
    $existingRules = @($ilb.loadBalancingRules | ForEach-Object { $_.name })
    
    # Port 8085: NUMA Node 0 process
    if ($existingRules -notcontains "grpcLbRule") {
        Write-Host "Creating LB rule: grpcLbRule (port 8085)"
        az network lb rule create `
            --name "grpcLbRule" `
            --lb-name $internalLbName `
            --resource-group $resourceGroupName `
            --frontend-ip-name "internalFrontend" `
            --backend-pool-name "appBackendPool" `
            --protocol Tcp `
            --frontend-port 8085 `
            --backend-port 8085 `
            --probe-name "grpcHealthProbe" `
            --idle-timeout 30 `
            --enable-tcp-reset true `
            --output none
    } else {
        Write-Host "LB rule already exists: grpcLbRule"
    }
    
    # Port 8086: NUMA Node 1 process
    if ($existingRules -notcontains "grpcLbRule8086") {
        Write-Host "Creating LB rule: grpcLbRule8086 (port 8086)"
        az network lb rule create `
            --name "grpcLbRule8086" `
            --lb-name $internalLbName `
            --resource-group $resourceGroupName `
            --frontend-ip-name "internalFrontend" `
            --backend-pool-name "appBackendPool" `
            --protocol Tcp `
            --frontend-port 8086 `
            --backend-port 8086 `
            --probe-name "grpcHealthProbe8086" `
            --idle-timeout 30 `
            --enable-tcp-reset true `
            --output none
    } else {
        Write-Host "LB rule already exists: grpcLbRule8086"
    }
    
    Write-Host "Internal load balancer configured with dual-port NUMA deployment (8085 for NUMA 0, 8086 for NUMA 1)"
    
    # Refresh LB info after updates
    $ilbJson = az network lb show --name $internalLbName --resource-group $resourceGroupName
    $ilb = $ilbJson | ConvertFrom-Json
    $internalLbId = $ilb.id
    $internalLbPrivateIp = $ilb.frontendIPConfigurations[0].privateIPAddress
    Write-Host "  Internal LB ID: $internalLbId"
    Write-Host "  Private IP: $internalLbPrivateIp"
    
    # ============================================================================
    # Grant SF Resource Provider permissions (required for BYOVNET + BYOLB)
    # Per Azure documentation:
    # - BYOVNET: Network Contributor on the SUBNET
    # - BYOLB: Network Contributor on the LOAD BALANCER
    # Role Definition ID for Network Contributor: 4d97b98b-1d4f-4787-a291-c67834d212e7
    # ============================================================================
    Write-Step "Granting SF Resource Provider permissions (BYOVNET + BYOLB)"
    $sfRpPrincipalId = $regionConfig.identity.sfRpPrincipalId
    if ([string]::IsNullOrEmpty($sfRpPrincipalId)) {
        # Get SF RP principal ID using the well-known SF RP App ID
        # The App ID is constant, but Principal ID varies by tenant
        Write-Host "Looking up SF Resource Provider principal ID..."
        $sfRpPrincipalId = (az ad sp show --id "74cb6831-0dbb-4be1-8206-fd4df301cdc2" --query id -o tsv 2>$null)
        if ([string]::IsNullOrEmpty($sfRpPrincipalId)) {
            throw "Could not find Service Fabric Resource Provider service principal. Ensure it exists in your tenant."
        }
        Write-Host "  SF RP Principal ID: $sfRpPrincipalId"
    }
    
    # Grant Network Contributor on the SUBNET (required for BYOVNET)
    Write-Host "Granting Network Contributor on subnet for BYOVNET..."
    az role assignment create `
        --role "Network Contributor" `
        --assignee-object-id $sfRpPrincipalId `
        --assignee-principal-type ServicePrincipal `
        --scope $userVnetSubnetId `
        --output none 2>$null
    Write-Host "  Granted Network Contributor to SF RP on subnet: $userVnetSubnetId"
    
    # Grant Network Contributor on the LOAD BALANCER (required for BYOLB)
    Write-Host "Granting Network Contributor on load balancer for BYOLB..."
    az role assignment create `
        --role "Network Contributor" `
        --assignee-object-id $sfRpPrincipalId `
        --assignee-principal-type ServicePrincipal `
        --scope $internalLbId `
        --output none 2>$null
    Write-Host "  Granted Network Contributor to SF RP on LB: $internalLbId"
}

# Create the Service Fabric Managed Cluster
# Use Standard SKU for BYOLB (multiple node types), Basic for single node type
$clusterSku = if ($UseInternalLoadBalancer) { "Standard" } else { "Basic" }
Write-Step "Creating Service Fabric Managed Cluster (SKU: $clusterSku)"
$clusterExists = az sf managed-cluster show --cluster-name $ClusterName --resource-group $resourceGroupName 2>$null
if ($LASTEXITCODE -ne 0) {
    $plainPassword = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($AdminPassword))
    
    if ($UseInternalLoadBalancer -and $subnetIdForCluster) {
        # BYOVNET: Use REST API to create cluster with subnetId for deploying into user's VNet
        Write-Host "Creating cluster with BYOVNET (subnetId) using REST API..."
        $clusterResourceId = "/subscriptions/$subscriptionId/resourceGroups/$resourceGroupName/providers/Microsoft.ServiceFabric/managedClusters/$ClusterName"
        
        $clusterBody = @{
            location = $location
            sku = @{
                name = $clusterSku
            }
            properties = @{
                dnsName = $ClusterName.ToLower()
                adminUserName = $AdminUsername
                adminPassword = $plainPassword
                clientConnectionPort = 19000
                httpGatewayConnectionPort = 19080
                subnetId = $subnetIdForCluster
                clients = @(
                    @{
                        isAdmin = $true
                        thumbprint = $clientCertThumbprint
                    }
                )
            }
        } | ConvertTo-Json -Depth 10 -Compress
        
        $tempFile = [System.IO.Path]::GetTempFileName()
        [System.IO.File]::WriteAllText($tempFile, $clusterBody, [System.Text.UTF8Encoding]::new($false))
        
        az rest --method PUT --uri "$clusterResourceId`?api-version=2024-04-01" --body "@$tempFile" --output none
        Remove-Item $tempFile -Force
        
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to create cluster with BYOVNET"
        }
        Write-Host "Created cluster: $ClusterName with BYOVNET (subnetId: $subnetIdForCluster)"
    } else {
        # Standard cluster creation without BYOVNET
        az sf managed-cluster create `
            --cluster-name $ClusterName `
            --resource-group $resourceGroupName `
            --location $location `
            --admin-password $plainPassword `
            --admin-user-name $AdminUsername `
            --client-cert-thumbprint $clientCertThumbprint `
            --sku $clusterSku `
            --output none
        
        Write-Host "Created cluster: $ClusterName (SKU: $clusterSku)"
    }
} else {
    Write-Host "Cluster already exists: $ClusterName"
}

# Wait for cluster to be ready
Write-Host "Waiting for cluster provisioning..."
$maxWait = 30
$waited = 0
do {
    Start-Sleep -Seconds 60
    $waited++
    $state = az sf managed-cluster show --cluster-name $ClusterName --resource-group $resourceGroupName --query "provisioningState" -o tsv
    Write-Host "  Provisioning state: $state ($waited min)"
} while (($state -eq "Updating" -or $state -eq "Creating") -and $waited -lt $maxWait)

if ($state -ne "Succeeded") {
    throw "Cluster provisioning failed or timed out. State: $state"
}

# Get cluster details
$clusterJson = az sf managed-cluster show --cluster-name $ClusterName --resource-group $resourceGroupName
$cluster = $clusterJson | ConvertFrom-Json
$clusterFqdn = $cluster.fqdn
$clusterEndpoint = "$clusterFqdn`:19000"
$sfInfraRg = "SFC_$($cluster.clusterId)"

# ============================================================================
# Create Primary Node Type (System Services)
# ============================================================================
Write-Step "Creating Primary Node Type: $nodeTypeName"
$nodeTypeExists = az sf managed-node-type show --cluster-name $ClusterName --resource-group $resourceGroupName --node-type-name $nodeTypeName 2>$null
if ($LASTEXITCODE -ne 0) {
    az sf managed-node-type create `
        --cluster-name $ClusterName `
        --resource-group $resourceGroupName `
        --node-type-name $nodeTypeName `
        --instance-count $effectiveVmCount `
        --vm-size $effectiveVmSize `
        --primary `
        --output none
    
    Write-Host "Created primary node type: $nodeTypeName ($effectiveVmSize x $effectiveVmCount)"
} else {
    Write-Host "Node type already exists: $nodeTypeName"
}

# Wait for node type provisioning
Write-Host "Waiting for primary node type provisioning..."
$waited = 0
do {
    Start-Sleep -Seconds 60
    $waited++
    $nodeTypeId = "/subscriptions/$subscriptionId/resourceGroups/$resourceGroupName/providers/Microsoft.ServiceFabric/managedClusters/$ClusterName/nodeTypes/$nodeTypeName"
    $state = az rest --method GET --uri "$nodeTypeId`?api-version=2024-04-01" --query "properties.provisioningState" -o tsv 2>$null
    Write-Host "  Node type provisioning state: $state ($waited min)"
} while (($state -eq "Updating" -or $state -eq "Creating") -and $waited -lt $maxWait)

# ============================================================================
# BYOLB: Create Secondary Node Type for Applications  
# (Internal LB was already created before cluster in Step 1)
# ============================================================================
$appNodeIps = @()
if ($UseInternalLoadBalancer) {
    Write-Step "Creating Secondary Node Type for Applications: $AppNodeTypeName"
    
    $appNodeTypeExists = az sf managed-node-type show --cluster-name $ClusterName --resource-group $resourceGroupName --node-type-name $AppNodeTypeName 2>$null
    if ($LASTEXITCODE -ne 0) {
        # Create secondary node type with BYOLB configuration using REST API
        # The az sf managed-node-type create doesn't support all BYOLB options
        $appNodeTypeId = "/subscriptions/$subscriptionId/resourceGroups/$resourceGroupName/providers/Microsoft.ServiceFabric/managedClusters/$ClusterName/nodeTypes/$AppNodeTypeName"
        
        # Get backend pool and NAT pool IDs from the LB we created earlier
        $backendPoolId = "$internalLbId/backendAddressPools/appBackendPool"
        $natPoolId = "$internalLbId/inboundNatPools/rdpNatPool"
        
        $appNodeTypeBody = @{
            location = $location
            properties = @{
                isPrimary = $false
                vmSize = $AppVmSize
                vmInstanceCount = $AppVmCount
                dataDiskSizeGB = 256
                dataDiskType = "StandardSSD_LRS"
                dataDiskLetter = "S"
                # VM Image - Windows Server 2022
                vmImagePublisher = "MicrosoftWindowsServer"
                vmImageOffer = "WindowsServer"
                vmImageSku = "2022-Datacenter"
                vmImageVersion = "latest"
                # Enable accelerated networking for high throughput
                enableAcceleratedNetworking = $true
                # BYOLB configuration - primary NIC joins LB backend pool
                frontendConfigurations = @(
                    @{
                        loadBalancerBackendAddressPoolId = $backendPoolId
                        loadBalancerInboundNatPoolId = $natPoolId
                    }
                )
                # Secondary NIC for NUMA node 1 - also joins the same LB backend pool
                # This enables dual NUMA processes on same port (8085) via different NICs
                additionalNetworkInterfaceConfigurations = @(
                    @{
                        name = "nic-numa1"
                        enableAcceleratedNetworking = $true
                        ipConfigurations = @(
                            @{
                                name = "ipconfig-numa1"
                                loadBalancerBackendAddressPools = @(
                                    @{ id = $backendPoolId }
                                )
                                subnet = @{ id = $userVnetSubnetId }
                                privateIPAddressVersion = "IPv4"
                            }
                        )
                    }
                )
                # Use default public LB for outbound connectivity (required for Azure dependencies)
                useDefaultPublicLoadBalancer = $true
                # Assign managed identity
                vmManagedIdentity = @{
                    userAssignedIdentities = @($uamiResourceId)
                }
            }
        } | ConvertTo-Json -Depth 10 -Compress
        
        $tempFile = [System.IO.Path]::GetTempFileName()
        [System.IO.File]::WriteAllText($tempFile, $appNodeTypeBody, [System.Text.UTF8Encoding]::new($false))
        
        Write-Host "Creating app node type with BYOLB configuration..."
        az rest --method PUT --uri "$appNodeTypeId`?api-version=2024-04-01" --body "@$tempFile" --output none
        Remove-Item $tempFile -Force
        
        Write-Host "Created secondary node type: $AppNodeTypeName with internal LB"
    } else {
        Write-Host "App node type already exists: $AppNodeTypeName"
    }
    
    # Wait for app node type provisioning
    Write-Host "Waiting for app node type provisioning..."
    $waited = 0
    do {
        Start-Sleep -Seconds 60
        $waited++
        $appNodeTypeId = "/subscriptions/$subscriptionId/resourceGroups/$resourceGroupName/providers/Microsoft.ServiceFabric/managedClusters/$ClusterName/nodeTypes/$AppNodeTypeName"
        $state = az rest --method GET --uri "$appNodeTypeId`?api-version=2024-04-01" --query "properties.provisioningState" -o tsv 2>$null
        Write-Host "  App node type provisioning state: $state ($waited min)"
    } while (($state -eq "Updating" -or $state -eq "Creating") -and $waited -lt $maxWait)
    
    # UAMI is already assigned during node type creation for BYOLB
    Write-Host "UAMI assigned during app node type creation"
    
    # The target node type for applications is the secondary node type
    $targetNodeTypeName = $AppNodeTypeName
} else {
    # For non-BYOLB, assign UAMI to the primary node type
    $targetNodeTypeName = $nodeTypeName
    
    # Assign UAMI to node type
    Write-Step "Assigning Managed Identity to Node Type"
    $nodeTypeId = "/subscriptions/$subscriptionId/resourceGroups/$resourceGroupName/providers/Microsoft.ServiceFabric/managedClusters/$ClusterName/nodeTypes/$nodeTypeName"
    $currentConfig = az rest --method GET --uri "$nodeTypeId`?api-version=2024-04-01" | ConvertFrom-Json
    
    if (-not $currentConfig.properties.vmManagedIdentity) {
        $currentConfig.properties | Add-Member -MemberType NoteProperty -Name "vmManagedIdentity" -Value @{userAssignedIdentities=@($uamiResourceId)} -Force
        $updatedJson = $currentConfig | ConvertTo-Json -Depth 10 -Compress
        
        $tempFile = [System.IO.Path]::GetTempFileName()
        [System.IO.File]::WriteAllText($tempFile, $updatedJson, [System.Text.UTF8Encoding]::new($false))
        
        az rest --method PUT --uri "$nodeTypeId`?api-version=2024-04-01" --body "@$tempFile" --output none
        Remove-Item $tempFile -Force
        
        Write-Host "Assigned UAMI to node type"
        
        # Wait for identity assignment
        Write-Host "Waiting for identity assignment..."
        $waited = 0
        do {
            Start-Sleep -Seconds 30
            $waited++
            $state = az rest --method GET --uri "$nodeTypeId`?api-version=2024-04-01" --query "properties.provisioningState" -o tsv
            Write-Host "  State: $state"
        } while ($state -eq "Updating" -and $waited -lt 60)
    } else {
        Write-Host "UAMI already assigned to node type"
    }
}

# Get cluster VMSS infrastructure RG
Write-Host "Service Fabric infrastructure RG: $sfInfraRg"

# Add NSG rules for gRPC ports (only for non-BYOLB mode, BYOLB gets its own NSG)
if (-not $UseInternalLoadBalancer) {
    Write-Step "Configuring NSG for gRPC ports (dual-port NUMA deployment)"
    $nsgList = az network nsg list --resource-group $sfInfraRg --query "[0].name" -o tsv 2>$null
    if ($nsgList) {
        # Port 8085: NUMA Node 0
        az network nsg rule create `
            --nsg-name $nsgList `
            --resource-group $sfInfraRg `
            --name "Allow-gRPC-8085" `
            --priority 1100 `
            --direction Inbound `
            --access Allow `
            --protocol Tcp `
            --destination-port-ranges 8085 `
            --output none 2>$null
        Write-Host "Added NSG rule for port 8085 (NUMA Node 0)"
        
        # Port 8086: NUMA Node 1
        az network nsg rule create `
            --nsg-name $nsgList `
            --resource-group $sfInfraRg `
            --name "Allow-gRPC-8086" `
            --priority 1101 `
            --direction Inbound `
            --access Allow `
            --protocol Tcp `
            --destination-port-ranges 8086 `
            --output none 2>$null
        Write-Host "Added NSG rule for port 8086 (NUMA Node 1)"
    }
}

# Get node IPs for the application node type
Write-Step "Getting Node IPs"
$nodeIps = @()

# Find the VMSS for the target node type
$vmssList = az vmss list --resource-group $sfInfraRg --query "[].{name:name, tags:tags}" -o json | ConvertFrom-Json
foreach ($vmss in $vmssList) {
    # Check if this VMSS matches our target node type
    $vmssName = $vmss.name
    if ($vmssName -like "*$targetNodeTypeName*" -or (-not $UseInternalLoadBalancer -and $vmssList.Count -eq 1)) {
        $instances = az vmss list-instances --resource-group $sfInfraRg --name $vmssName --query "[].instanceId" -o tsv
        foreach ($instanceId in $instances) {
            $nicId = az vmss nic list --resource-group $sfInfraRg --vmss-name $vmssName --instance-id $instanceId --query "[0].id" -o tsv
            if ($nicId) {
                $ip = az network nic show --ids $nicId --query "ipConfigurations[0].privateIPAddress" -o tsv
                $nodeIps += $ip
            }
        }
        break
    }
}

# For BYOLB, also configure NSG on the app node type's NSG
if ($UseInternalLoadBalancer -and $nodeIps.Count -gt 0) {
    Write-Step "Configuring NSG for App Node Type (dual-port NUMA deployment)"
    # BYOLB creates a separate NSG for each node type
    $appNsgList = az network nsg list --resource-group $sfInfraRg --query "[?contains(name, '$AppNodeTypeName') || contains(name, 'App')].name" -o tsv 2>$null
    if ($appNsgList) {
        foreach ($nsgName in $appNsgList) {
            # Port 8085: NUMA Node 0
            az network nsg rule create `
                --nsg-name $nsgName `
                --resource-group $sfInfraRg `
                --name "Allow-gRPC-8085" `
                --priority 1100 `
                --direction Inbound `
                --access Allow `
                --protocol Tcp `
                --destination-port-ranges 8085 `
                --output none 2>$null
            Write-Host "Added NSG rule for port 8085 to NSG: $nsgName (NUMA Node 0)"
            
            # Port 8086: NUMA Node 1
            az network nsg rule create `
                --nsg-name $nsgName `
                --resource-group $sfInfraRg `
                --name "Allow-gRPC-8086" `
                --priority 1101 `
                --direction Inbound `
                --access Allow `
                --protocol Tcp `
                --destination-port-ranges 8086 `
                --output none 2>$null
            Write-Host "Added NSG rule for port 8086 to NSG: $nsgName (NUMA Node 1)"
        }
    }
}

Write-Host "Node IPs: $($nodeIps -join ', ')"

# Generate cluster config file
Write-Step "Generating Cluster Config File"
$clusterConfig = @{
    schemaVersion = 2
    generatedOnUtc = $timestamp
    region = $RegionName
    clusterName = $ClusterName
    configurationMode = $configMode
    azure = @{
        subscriptionId = $subscriptionId
        resourceGroupName = $resourceGroupName
        infraResourceGroupName = $sfInfraRg
        location = $location
    }
    cluster = @{
        fqdn = $clusterFqdn
        endpoint = $clusterEndpoint
        clientCertThumbprint = $clientCertThumbprint
        serverCertThumbprint = $serverCertThumbprint
    }
    nodeType = @{
        primaryName = $nodeTypeName
        primaryVmSize = $effectiveVmSize
        primaryVmCount = $effectiveVmCount
        appName = if ($UseInternalLoadBalancer) { $AppNodeTypeName } else { $nodeTypeName }
        appVmSize = if ($UseInternalLoadBalancer) { $AppVmSize } else { $effectiveVmSize }
        appVmCount = if ($UseInternalLoadBalancer) { $AppVmCount } else { $effectiveVmCount }
        nodeIps = $nodeIps
    }
    identity = @{
        uamiResourceId = $uamiResourceId
        uamiClientId = $regionConfig.identity.uamiClientId
    }
    dataplane = @{
        # Dual-port NUMA deployment: Port 8085 for NUMA 0, Port 8086 for NUMA 1
        grpcPortNuma0 = 8085
        grpcPortNuma1 = 8086
        grpcPorts = @(8085, 8086)
        configStorageAccount = $regionConfig.dataplane.configStorageAccount
        configContainer = $regionConfig.dataplane.configContainer
    }
}

# Add BYOLB-specific config
if ($UseInternalLoadBalancer) {
    $clusterConfig.byolb = @{
        enabled = $true
        internalLoadBalancerId = $internalLbId
        internalLoadBalancerName = $internalLbName
        privateIp = $internalLbPrivateIp
        backendPoolId = "$internalLbId/backendAddressPools/appBackendPool"
        # Dual-port NUMA endpoints
        grpcEndpointNuma0 = "$internalLbPrivateIp`:8085"
        grpcEndpointNuma1 = "$internalLbPrivateIp`:8086"
    }
}

$clusterConfig | ConvertTo-Json -Depth 10 | Out-File -FilePath $clusterConfigFile -Encoding utf8
Write-Host "Saved cluster config to: $clusterConfigFile"

# Update region config with this cluster
$regionConfig.clusters += @{
    name = $ClusterName
    configFile = $clusterConfigFile
}
$regionConfig | ConvertTo-Json -Depth 10 | Out-File -FilePath $regionConfigFile -Encoding utf8

# Summary
$grpcEndpointNuma0 = if ($UseInternalLoadBalancer -and $internalLbPrivateIp) { 
    "$internalLbPrivateIp`:8085" 
} else { 
    "$($nodeIps[0]):8085" 
}
$grpcEndpointNuma1 = if ($UseInternalLoadBalancer -and $internalLbPrivateIp) { 
    "$internalLbPrivateIp`:8086" 
} else { 
    "$($nodeIps[0]):8086" 
}

Write-Host @"

================================================================================
  Cluster Created Successfully!
================================================================================
  Cluster Name:      $ClusterName
  FQDN:              $clusterFqdn
  Endpoint:          $clusterEndpoint
  Configuration:     $configMode
  App Node Type:     $(if ($UseInternalLoadBalancer) { $AppNodeTypeName } else { $nodeTypeName })
  Node IPs:          $($nodeIps -join ', ')
$(if ($UseInternalLoadBalancer) { @"
  
  BYOLB Configuration:
  - Internal LB:     $internalLbName
  - Private IP:      $internalLbPrivateIp
  - gRPC Endpoint (NUMA 0): $grpcEndpointNuma0
  - gRPC Endpoint (NUMA 1): $grpcEndpointNuma1
"@})
  Config File:       $clusterConfigFile
================================================================================

Next Steps:
  1. Run DeployApp.ps1 -ClusterName "$ClusterName" -RegionName "$RegionName" to deploy the app
$(if ($UseInternalLoadBalancer) { @"
  2. Ensure client VM is in the same VNet or has VNet peering to access private IP
  3. Test both NUMA endpoints:
     - NUMA 0: .\run_azure_linux_cluster.ps1 -GrpcServerPrivate "$grpcEndpointNuma0"
     - NUMA 1: .\run_azure_linux_cluster.ps1 -GrpcServerPrivate "$grpcEndpointNuma1"
     - Both:   .\run_azure_linux_both.ps1 -GrpcServerPrivate "$($internalLbPrivateIp)"
"@} else { @"
  2. Test both NUMA endpoints:
     - NUMA 0: .\run_azure_linux_cluster.ps1 -GrpcServerPrivate "$grpcEndpointNuma0"
     - NUMA 1: .\run_azure_linux_cluster.ps1 -GrpcServerPrivate "$grpcEndpointNuma1"
"@})

"@ -ForegroundColor Green
