<#
.SYNOPSIS
    Deploys the KVStoreServer application to a Service Fabric cluster.

.DESCRIPTION
    Deploys the KVStoreServer application with:
    - Proper environment variables for the region
    - Managed identity configuration
    - ConsoleRedirection for logging

.PARAMETER ClusterName
    Name of the cluster to deploy to

.PARAMETER RegionName
    Azure region name (must have cluster config from AddCluster)

.PARAMETER AppVersion
    Application version to deploy (default: auto-increments)

.PARAMETER ForceUpgrade
    Force upgrade even if version hasn't changed

.EXAMPLE
    .\DeployApp.ps1 -ClusterName "kvstore-c1" -RegionName "westus2"
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ClusterName,

    [Parameter(Mandatory = $true)]
    [string]$RegionName,

    [Parameter(Mandatory = $false)]
    [string]$AppVersion = "",

    [Parameter(Mandatory = $false)]
    [switch]$ForceUpgrade
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# ============================================================================
# Load Configs
# ============================================================================
$configDir = Join-Path $PSScriptRoot "..\..\config\$RegionName"
$regionConfigFile = Join-Path $configDir "region.config.json"
$clusterConfigFile = Join-Path $configDir "$ClusterName.config.json"

if (-not (Test-Path $regionConfigFile)) {
    throw "Region config not found: $regionConfigFile"
}
if (-not (Test-Path $clusterConfigFile)) {
    throw "Cluster config not found: $clusterConfigFile. Run AddCluster.ps1 first."
}

$regionConfig = Get-Content $regionConfigFile -Raw | ConvertFrom-Json
$clusterConfig = Get-Content $clusterConfigFile -Raw | ConvertFrom-Json

# ============================================================================
# Paths
# ============================================================================
$repoRoot = Join-Path $PSScriptRoot "../.."
$pkgDir = Join-Path $repoRoot "KVService/sf/pkg/KVStoreServerApp"
$buildDir = Join-Path $repoRoot "KVService/build/Release"
$deployScript = Join-Path $pkgDir "deploy.ps1"
$appManifest = Join-Path $pkgDir "ApplicationManifest.xml"
# Dual NUMA package structure - Node0 and Node1
$svcManifestNode0 = Join-Path $pkgDir "KVStoreServerNode0Pkg/ServiceManifest.xml"
$svcManifestNode1 = Join-Path $pkgDir "KVStoreServerNode1Pkg/ServiceManifest.xml"

# ============================================================================
# Helper Functions
# ============================================================================
function Write-Step {
    param([string]$Message)
    Write-Host "`n=== $Message ===" -ForegroundColor Cyan
}

function Get-ManifestVersion {
    param([string]$ManifestPath)
    [xml]$xml = Get-Content $ManifestPath
    return $xml.DocumentElement.ApplicationTypeVersion
}

function Set-ManifestVersion {
    param([string]$AppManifestPath, [string[]]$SvcManifestPaths, [string]$Version)
    
    # Update ApplicationManifest
    $content = Get-Content $AppManifestPath -Raw
    $content = $content -replace 'ApplicationTypeVersion="[^"]*"', "ApplicationTypeVersion=`"$Version`""
    $content = $content -replace 'ServiceManifestVersion="[^"]*"', "ServiceManifestVersion=`"$Version`""
    $content | Set-Content $AppManifestPath -Encoding UTF8
    
    # Update each ServiceManifest (Node0 and Node1)
    foreach ($svcManifestPath in $SvcManifestPaths) {
        if (Test-Path $svcManifestPath) {
            $content = Get-Content $svcManifestPath -Raw
            $content = $content -replace '(<ServiceManifest[^>]*Version=")[^"]*"', "`${1}$Version`""
            $content = $content -replace '(<CodePackage[^>]*Version=")[^"]*"', "`${1}$Version`""
            $content | Set-Content $svcManifestPath -Encoding UTF8
            Write-Host "  Updated: $svcManifestPath"
        }
    }
}

function Increment-Version {
    param([string]$Version)
    $parts = $Version -split '\.'
    if ($parts.Count -ge 3) {
        $parts[2] = [int]$parts[2] + 1
        return $parts -join '.'
    }
    return "1.0.1"
}

# ============================================================================
# Main Script
# ============================================================================
Write-Host @"
================================================================================
  KVStore Data Plane - Deploy Application
================================================================================
  Cluster:          $ClusterName
  Region:           $RegionName
  Cluster Endpoint: $($clusterConfig.cluster.endpoint)
================================================================================
"@ -ForegroundColor Green

# Determine version
$currentVersion = Get-ManifestVersion $appManifest
if ([string]::IsNullOrEmpty($AppVersion)) {
    $AppVersion = Increment-Version $currentVersion
    Write-Host "Auto-incrementing version: $currentVersion -> $AppVersion"
}

# Update manifests with new version
Write-Step "Updating Manifest Versions"
Set-ManifestVersion -AppManifestPath $appManifest -SvcManifestPaths @($svcManifestNode0, $svcManifestNode1) -Version $AppVersion
Write-Host "Updated manifests to version: $AppVersion"

# Copy latest binary to both Node0 and Node1 Code folders
Write-Step "Copying Latest Binary"
$exePath = Join-Path $buildDir "KVStoreServer.exe"
$targetPathNode0 = Join-Path $pkgDir "KVStoreServerNode0Pkg/Code/KVStoreServer.exe"
$targetPathNode1 = Join-Path $pkgDir "KVStoreServerNode1Pkg/Code/KVStoreServer.exe"

if (Test-Path $exePath) {
    Copy-Item $exePath $targetPathNode0 -Force
    Copy-Item $exePath $targetPathNode1 -Force
    Write-Host "Copied KVStoreServer.exe to Node0 and Node1 Code folders"
} else {
    Write-Warning "Binary not found at $exePath. Using existing binaries."
}

# Build app parameters
Write-Step "Configuring Application Parameters"

# Support both old single-port and new dual-port config formats
$grpcPortNuma0 = if ($clusterConfig.dataplane.grpcPortNuma0) { 
    $clusterConfig.dataplane.grpcPortNuma0.ToString() 
} else { 
    "8085" 
}
$grpcPortNuma1 = if ($clusterConfig.dataplane.grpcPortNuma1) { 
    $clusterConfig.dataplane.grpcPortNuma1.ToString() 
} else { 
    "8086" 
}

$appParams = @{
    CurrentLocation = $RegionName
    ConfigurationStore = $clusterConfig.dataplane.configStorageAccount
    ConfigurationContainer = $clusterConfig.dataplane.configContainer
    GrpcPortNuma0 = $grpcPortNuma0
    GrpcPortNuma1 = $grpcPortNuma1
    ManagedIdentityClientId = $clusterConfig.identity.uamiClientId
}

Write-Host "Application Parameters:"
$appParams.GetEnumerator() | ForEach-Object { Write-Host "  $($_.Key) = $($_.Value)" }

# Deploy using the deploy script
Write-Step "Deploying Application"
$appParamsJson = $appParams | ConvertTo-Json -Compress

& $deployScript `
    -ClusterEndpoint $clusterConfig.cluster.endpoint `
    -ServerCertThumbprint $clusterConfig.cluster.serverCertThumbprint `
    -ClientCertThumbprint $clusterConfig.cluster.clientCertThumbprint `
    -ApplicationTypeVersion $AppVersion `
    -AppParametersJson $appParamsJson

if ($LASTEXITCODE -ne 0) {
    throw "Deployment failed with exit code: $LASTEXITCODE"
}

# Summary
$grpcEndpointNuma0 = if ($clusterConfig.byolb -and $clusterConfig.byolb.enabled -and $clusterConfig.byolb.privateIp) {
    "$($clusterConfig.byolb.privateIp):$grpcPortNuma0"
} else {
    "$($clusterConfig.nodeType.nodeIps[0]):$grpcPortNuma0"
}
$grpcEndpointNuma1 = if ($clusterConfig.byolb -and $clusterConfig.byolb.enabled -and $clusterConfig.byolb.privateIp) {
    "$($clusterConfig.byolb.privateIp):$grpcPortNuma1"
} else {
    "$($clusterConfig.nodeType.nodeIps[0]):$grpcPortNuma1"
}

$appNodeType = if ($clusterConfig.nodeType.appName) { $clusterConfig.nodeType.appName } else { $clusterConfig.nodeType.primaryName }

Write-Host @"

================================================================================
  Deployment Complete!
================================================================================
  Cluster:          $ClusterName
  App Version:      $AppVersion
  App Node Type:    $appNodeType
  
  Dual-Port NUMA Configuration:
  - Port $grpcPortNuma0 (NUMA Node 0): $grpcEndpointNuma0
  - Port $grpcPortNuma1 (NUMA Node 1): $grpcEndpointNuma1
$(if ($clusterConfig.byolb -and $clusterConfig.byolb.enabled) { @"
  
  BYOLB Configuration:
  - Internal LB:    $($clusterConfig.byolb.internalLoadBalancerName)
  - Private IP:     $($clusterConfig.byolb.privateIp)
"@})
  Node IPs:         $($clusterConfig.nodeType.nodeIps -join ', ')
================================================================================

Test the deployment (dual-port NUMA):
  # Test NUMA Node 0 (port $grpcPortNuma0):
  .\scripts\run\run_azure_linux_cluster.ps1 -GrpcServerPrivate "$grpcEndpointNuma0" -Iterations 10 -Concurrency 1
  
  # Test NUMA Node 1 (port $grpcPortNuma1):
  .\scripts\run\run_azure_linux_cluster.ps1 -GrpcServerPrivate "$grpcEndpointNuma1" -Iterations 10 -Concurrency 1
  
  # Test both NUMA nodes:
  .\scripts\run\run_azure_linux_both.ps1 -GrpcServerPrivate "$($clusterConfig.byolb.privateIp)"

"@ -ForegroundColor Green
