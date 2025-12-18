# Get private IPs for Service Fabric Managed Cluster node type VMSS

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ClusterName,

    [Parameter()]
    [string]$NodeTypeName = 'NT1',

    [Parameter()]
    [string]$SubscriptionId = '6cbd0699-eae8-4633-8054-691a6b726d90'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$AzCli = 'C:\Program Files\Microsoft SDKs\Azure\CLI2\wbin\az.cmd'
if (-not (Test-Path $AzCli)) {
    throw "Azure CLI not found at expected path: $AzCli"
}

function Invoke-AzCli {
    param([Parameter(Mandatory = $true)][string[]]$Args)
    $out = & $AzCli @Args
    if ($LASTEXITCODE -ne 0) {
        throw "az failed (exit $LASTEXITCODE)."
    }
    return $out
}

Invoke-AzCli -Args @('account', 'set', '--subscription', $SubscriptionId) | Out-Null

# Heuristic: node type VMSS is in the auto-created SFC_* infra RG and VMSS name usually contains cluster name.
$vmssJson = Invoke-AzCli -Args @('vmss', 'list', '--query', "[?contains(resourceGroup,'SFC_') && contains(name,'$ClusterName')].{name:name,rg:resourceGroup}", '-o', 'json')
$vmss = $vmssJson | ConvertFrom-Json

if (-not $vmss -or $vmss.Count -eq 0) {
    throw "No VMSS found in SFC_* resource groups for cluster name '$ClusterName'. If the infra RG naming differs, run: $AzCli vmss list -o table"
}

$target = $vmss | Where-Object { $_.name -match $NodeTypeName } | Select-Object -First 1
if (-not $target) {
    $target = $vmss | Select-Object -First 1
}

Write-Host "Using VMSS: $($target.rg)/$($target.name)" -ForegroundColor Cyan

$ips = Invoke-AzCli -Args @(
    'vmss', 'nic', 'list',
    '-g', $target.rg,
    '--vmss-name', $target.name,
    '--query', "[].ipConfigurations[0].privateIPAddress",
    '-o', 'tsv'
)

$ips
