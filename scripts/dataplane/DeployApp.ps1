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
$svcManifest = Join-Path $pkgDir "KVStoreServerServicePkg/ServiceManifest.xml"

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
    param([string]$AppManifestPath, [string]$SvcManifestPath, [string]$Version)
    
    # Update ApplicationManifest
    $content = Get-Content $AppManifestPath -Raw
    $content = $content -replace 'ApplicationTypeVersion="[^"]*"', "ApplicationTypeVersion=`"$Version`""
    $content = $content -replace 'ServiceManifestVersion="[^"]*"', "ServiceManifestVersion=`"$Version`""
    $content | Set-Content $AppManifestPath -Encoding UTF8
    
    # Update ServiceManifest
    $content = Get-Content $SvcManifestPath -Raw
    $content = $content -replace '(<ServiceManifest[^>]*Version=")[^"]*"', "`${1}$Version`""
    $content = $content -replace '(<CodePackage[^>]*Version=")[^"]*"', "`${1}$Version`""
    $content | Set-Content $SvcManifestPath -Encoding UTF8
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
Set-ManifestVersion -AppManifestPath $appManifest -SvcManifestPath $svcManifest -Version $AppVersion
Write-Host "Updated manifests to version: $AppVersion"

# Copy latest binary
Write-Step "Copying Latest Binary"
$exePath = Join-Path $buildDir "KVStoreServer.exe"
$targetPath = Join-Path $pkgDir "KVStoreServerServicePkg/Code/KVStoreServer.exe"

if (Test-Path $exePath) {
    Copy-Item $exePath $targetPath -Force
    Write-Host "Copied KVStoreServer.exe from: $exePath"
} else {
    Write-Warning "Binary not found at $exePath. Using existing binary."
}

# Build app parameters
Write-Step "Configuring Application Parameters"
$appParams = @{
    CurrentLocation = $RegionName
    ConfigurationStore = $clusterConfig.dataplane.configStorageAccount
    ConfigurationContainer = $clusterConfig.dataplane.configContainer
    GrpcPort = $clusterConfig.dataplane.grpcPort.ToString()
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
$grpcEndpoint = if ($clusterConfig.byolb -and $clusterConfig.byolb.enabled -and $clusterConfig.byolb.privateIp) {
    "$($clusterConfig.byolb.privateIp):$($clusterConfig.dataplane.grpcPort)"
} else {
    "$($clusterConfig.nodeType.nodeIps[0]):$($clusterConfig.dataplane.grpcPort)"
}

$appNodeType = if ($clusterConfig.nodeType.appName) { $clusterConfig.nodeType.appName } else { $clusterConfig.nodeType.primaryName }

Write-Host @"

================================================================================
  Deployment Complete!
================================================================================
  Cluster:          $ClusterName
  App Version:      $AppVersion
  App Node Type:    $appNodeType
  gRPC Port:        $($clusterConfig.dataplane.grpcPort)
  gRPC Endpoint:    $grpcEndpoint
$(if ($clusterConfig.byolb -and $clusterConfig.byolb.enabled) { @"
  
  BYOLB Configuration:
  - Internal LB:    $($clusterConfig.byolb.internalLoadBalancerName)
  - Private IP:     $($clusterConfig.byolb.privateIp)
"@})
  Node IPs:         $($clusterConfig.nodeType.nodeIps -join ', ')
================================================================================

Test the deployment:
  .\scripts\run\run_azure_linux_cluster.ps1 -GrpcServerPrivate "$grpcEndpoint" -Iterations 10 -Concurrency 1

"@ -ForegroundColor Green
