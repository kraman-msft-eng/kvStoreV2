# KVStore Data Plane Deployment Scripts

This directory contains scripts for setting up and managing the KVStore data plane infrastructure.

## Overview

The deployment follows a 3-step process:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  1. SetupDataPlaneRegion   →   2. AddCluster   →   3. DeployApp            │
│                                                                              │
│  Creates region-wide           Creates SF           Deploys KVStoreServer   │
│  resources (UAMI, KV,          managed cluster      application to cluster  │
│  VNet, certs)                  in the region                                │
│                                                                              │
│  Output: region.config.json    Output: cluster.config.json                  │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Prerequisites

- Azure CLI installed and logged in (`az login`)
- PowerShell 7+ (pwsh) or Windows PowerShell 5.1
- Service Fabric SDK installed (for deployment)
- Access to the Azure subscription

## Quick Start

### 1. Setup a New Region

```cmd
SetupDataPlaneRegion.cmd westus2 <subscription-id> kvstore-dp-westus2 promptdatawestus2
```

Or with PowerShell:

```powershell
.\SetupDataPlaneRegion.ps1 `
    -RegionName westus2 `
    -SubscriptionId "6cbd0699-eae8-4633-8054-691a6b726d90" `
    -ResourceGroupName "kvstore-dp-westus2" `
    -ConfigStorageAccount "promptdatawestus2"
```

This creates:
- Resource Group
- User-Assigned Managed Identity (UAMI)
- Key Vault with server/client certificates
- Virtual Network and Subnet
- Network Security Group
- Role assignments for SF Resource Provider

Output: `config/<region>/region.config.json`

### 2. Add a Cluster

**Standard Mode (Public Load Balancer):**

```cmd
AddCluster.cmd kvstore-c1 westus2 "YourSecureP@ssw0rd!"
```

Or with PowerShell:

```powershell
.\AddCluster.ps1 `
    -ClusterName "kvstore-c1" `
    -RegionName "westus2" `
    -AdminPassword (ConvertTo-SecureString "YourSecureP@ssw0rd!" -AsPlainText -Force)
```

**BYOLB Mode (Internal Load Balancer for Private Access):**

For production deployments where services should only be accessible via private IP:

```cmd
AddCluster.cmd kvstore-c1 westus2 "YourSecureP@ssw0rd!" -UseInternalLoadBalancer
```

Or with PowerShell (with custom VM sizes):

```powershell
.\AddCluster.ps1 `
    -ClusterName "kvstore-c1" `
    -RegionName "westus2" `
    -AdminPassword (ConvertTo-SecureString "YourSecureP@ssw0rd!" -AsPlainText -Force) `
    -UseInternalLoadBalancer `
    -SystemVmSize "Standard_D2s_v5" `  # Smaller for system services
    -SystemVmCount 3 `
    -AppVmSize "Standard_D4ds_v5" `    # Larger with local SSD for app workload
    -AppVmCount 5
```

**Default VM Sizes:**
| Node Type | Default SKU | Notes |
|-----------|-------------|-------|
| System (Primary) | Standard_D2s_v5 | 2 vCPU, 8GB RAM - sufficient for SF system services |
| App (Secondary) | Standard_D2ds_v5 | 2 vCPU, 8GB RAM + local SSD - for application workload |

This creates:
- Service Fabric Managed Cluster
- Primary node type with specified VMs
- UAMI assignment to node type
- NSG rule for gRPC port (8085)
- Downloads and imports certificates
- **(BYOLB)** Internal Azure Load Balancer with private IP
- **(BYOLB)** Secondary node type for applications

Output: `config/<region>/<cluster>.config.json`

## Network Configurations

### Standard Configuration

```
┌──────────────────────────────────────────────────────────────────┐
│                    Service Fabric Managed Cluster                │
│                                                                  │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │  Primary Node Type (NT1)                                │   │
│   │  - SF System Services (SFX, Explorer, PowerShell)       │   │
│   │  - KVStoreServer Application                            │   │
│   │  - Public Load Balancer                                 │   │
│   └─────────────────────────────────────────────────────────┘   │
│                              │                                   │
│                    Public IP:19000 (SF Mgmt)                     │
│                    Private IP:8085 (gRPC - via VNet Peering)     │
└──────────────────────────────────────────────────────────────────┘
```

### BYOLB Configuration (Recommended for Production)

```
┌──────────────────────────────────────────────────────────────────┐
│                    Service Fabric Managed Cluster                │
│                                                                  │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │  Primary Node Type (System)                              │   │
│   │  - SF System Services only                               │   │
│   │  - Default Public Load Balancer                          │   │
│   └─────────────────────────────────────────────────────────┘   │
│                              │ Public IP:19000 (SF Mgmt)         │
│                                                                  │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │  Secondary Node Type (App)                               │   │
│   │  - KVStoreServer Application                             │   │
│   │  - Internal Load Balancer (BYOLB)                        │   │
│   │  - Uses Default Public LB for outbound only              │   │
│   └─────────────────────────────────────────────────────────┘   │
│                              │ Private IP:8085 (gRPC)            │
│                              │ (No public inbound access)        │
└──────────────────────────────────────────────────────────────────┘
```

**BYOLB Benefits:**
- Application services are only accessible via private IP
- System services (SFX) remain accessible via public IP for management
- Compliant with private networking requirements (no NRMS issues)
- Client VMs must be in same VNet or have VNet peering

### 3. Deploy the Application

```cmd
DeployApp.cmd kvstore-c1 westus2
```

Or with PowerShell:

```powershell
.\DeployApp.ps1 `
    -ClusterName "kvstore-c1" `
    -RegionName "westus2"
```

This:
- Auto-increments the app version
- Copies the latest binary
- Deploys with region-specific configuration
- Configures managed identity for storage access

### 4. Test the Deployment

```powershell
# From the scripts/run directory
.\run_azure_linux_cluster.ps1 `
    -GrpcServerPrivate "10.0.0.4:8085" `
    -Iterations 100 `
    -Concurrency 5
```

## Configuration Files

### Region Config (`region.config.json`)

Contains region-wide settings:
- Subscription and resource group info
- UAMI details (name, client ID, principal ID)
- Key Vault and certificate references
- VNet and subnet configuration
- Data plane settings

### Cluster Config (`<cluster>.config.json`)

Contains cluster-specific settings:
- Cluster FQDN and endpoint
- Node type configuration
- Node private IPs
- gRPC port configuration

## Directory Structure

```
config/
├── westus2/
│   ├── region.config.json          # Region-wide config
│   ├── kvstore-c1.config.json      # Cluster 1 config
│   └── kvstore-c2.config.json      # Cluster 2 config
├── eastus/
│   └── ...
```

## Environment Variables

The KVStoreServer uses these environment variables (set automatically by the manifests):

| Variable | Description |
|----------|-------------|
| `KV_CURRENT_LOCATION` | Azure region name |
| `KV_CONFIGURATION_STORE` | Storage account for config metadata |
| `KV_CONFIGURATION_CONTAINER` | Container name for config blobs |
| `AZURE_CLIENT_ID` | UAMI client ID for authentication |
| `KV_PORT` | gRPC port (default: 8085) |

## Troubleshooting

### Certificate Issues

If you get certificate errors when deploying:

```powershell
# List certificates in your store
Get-ChildItem Cert:\CurrentUser\My | Select-Object Thumbprint, Subject

# Re-download from Key Vault
az keyvault secret download --vault-name <kv-name> --name kvstore-client-cert --file client.pfx --encoding base64
Import-PfxCertificate -FilePath client.pfx -CertStoreLocation Cert:\CurrentUser\My
```

### Managed Identity Issues

If you get "Failed to get token from DefaultAzureCredential":

1. Verify UAMI is assigned to node type:
```powershell
az rest --method GET --uri "/subscriptions/<sub>/resourceGroups/<rg>/providers/Microsoft.ServiceFabric/managedClusters/<cluster>/nodeTypes/NT1?api-version=2024-04-01" --query "properties.vmManagedIdentity"
```

2. Verify AZURE_CLIENT_ID is set in the application parameters

3. Check UAMI has Storage Blob Data Reader on the config storage account

### Viewing Logs

Console logs are available on each SF node at:
```
S:\SvcFab\_App\KVStoreServerAppType_App*\log\Code_KVStoreServerServicePkg_M_*.out
S:\SvcFab\_App\KVStoreServerAppType_App*\log\Code_KVStoreServerServicePkg_M_*.err
```

Access via:
```powershell
$rg = "SFC_<cluster-id>"
az vmss run-command invoke -g $rg -n NT1 --instance-id 0 --command-id RunPowerShellScript --scripts "Get-Content 'S:\SvcFab\_App\KVStoreServerAppType_App0\log\Code_KVStoreServerServicePkg_M_0.out' -Tail 50"
```

## Adding Storage Access for Data Plane

The UAMI needs access to the regional storage accounts where KV data is stored. This is typically done via a separate config script that:

1. Creates/identifies the regional storage account
2. Grants UAMI "Storage Blob Data Contributor" role
3. Generates the `aoaikv.json` config file in the config container

Example:
```powershell
# Grant UAMI access to data storage account
az role assignment create `
    --role "Storage Blob Data Contributor" `
    --assignee-object-id <uami-principal-id> `
    --assignee-principal-type ServicePrincipal `
    --scope "/subscriptions/<sub>/resourceGroups/<rg>/providers/Microsoft.Storage/storageAccounts/<storage-account>"
```
