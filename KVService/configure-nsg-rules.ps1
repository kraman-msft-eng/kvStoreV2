# configure-nsg-rules.ps1
# Configure Azure Network Security Group rules for KVStore server

param(
    [Parameter(Mandatory=$true)]
    [string]$ConfigFile = "azure-deploy-config.json"
)

$ErrorActionPreference = "Stop"

# Load configuration
Write-Host "Loading configuration from $ConfigFile..." -ForegroundColor Cyan
if (-not (Test-Path $ConfigFile)) {
    Write-Host "ERROR: Configuration file not found: $ConfigFile" -ForegroundColor Red
    exit 1
}

$config = Get-Content $ConfigFile -Raw | ConvertFrom-Json

$subscriptionId = $config.azure.subscriptionId
$resourceGroup = $config.azure.resourceGroupName
$vmName = $config.azure.vmName
$startPort = $config.server.startingPort
$numInstances = $config.server.numberOfInstances
$lbPort = $config.server.loadBalancerPort

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Azure NSG Configuration for KVStore" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Subscription: $subscriptionId" -ForegroundColor Green
Write-Host "Resource Group: $resourceGroup" -ForegroundColor Green
Write-Host "VM Name: $vmName" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""

# Check if Azure CLI is installed
Write-Host "Checking Azure CLI..." -ForegroundColor Green
$azVersion = az version 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ Azure CLI not found. Please install from: https://aka.ms/installazurecliwindows" -ForegroundColor Red
    exit 1
}
Write-Host "✓ Azure CLI found" -ForegroundColor Green

# Login to Azure
Write-Host ""
Write-Host "Checking Azure login status..." -ForegroundColor Green
$account = az account show 2>$null | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) {
    Write-Host "Not logged in. Logging in to Azure..." -ForegroundColor Yellow
    az login
    if ($LASTEXITCODE -ne 0) {
        Write-Host "✗ Azure login failed" -ForegroundColor Red
        exit 1
    }
}

# Set subscription
Write-Host "Setting subscription to: $subscriptionId" -ForegroundColor Green
az account set --subscription $subscriptionId
if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ Failed to set subscription" -ForegroundColor Red
    exit 1
}
Write-Host "✓ Subscription set" -ForegroundColor Green

# Get NSG associated with the VM
Write-Host ""
Write-Host "Finding Network Security Group for VM $vmName..." -ForegroundColor Green
$nic = az vm nic list --resource-group $resourceGroup --vm-name $vmName | ConvertFrom-Json
if (-not $nic -or $nic.Count -eq 0) {
    Write-Host "✗ No network interface found for VM" -ForegroundColor Red
    exit 1
}

$nicId = $nic[0].id
$nicName = $nicId.Split('/')[-1]
Write-Host "✓ Found NIC: $nicName" -ForegroundColor Green

$nicDetails = az network nic show --ids $nicId | ConvertFrom-Json
$nsgId = $nicDetails.networkSecurityGroup.id

if (-not $nsgId) {
    Write-Host "✗ No NSG attached to the NIC" -ForegroundColor Red
    Write-Host "Please create and attach an NSG to the VM first" -ForegroundColor Yellow
    exit 1
}

$nsgName = $nsgId.Split('/')[-1]
Write-Host "✓ Found NSG: $nsgName" -ForegroundColor Green

# Define rules to create
Write-Host ""
Write-Host "Creating NSG rules..." -ForegroundColor Green

$rules = @(
    @{
        Name = "AllowWinRM"
        Priority = 1000
        Port = 5985
        Description = "Allow WinRM for PowerShell remoting"
    },
    @{
        Name = "AllowKVStoreLoadBalancer"
        Priority = 1010
        Port = $lbPort
        Description = "Allow KVStore load balancer port"
    }
)

# Add rules for each server instance
for ($i = 0; $i -lt $numInstances; $i++) {
    $port = $startPort + $i
    $rules += @{
        Name = "AllowKVStoreServer$($i+1)"
        Priority = 1020 + $i
        Port = $port
        Description = "Allow KVStore server instance $($i+1)"
    }
}

# Create each rule
foreach ($rule in $rules) {
    Write-Host ""
    Write-Host "Creating rule: $($rule.Name) (Port: $($rule.Port), Priority: $($rule.Priority))" -ForegroundColor Yellow
    
    # Check if rule already exists
    $existingRule = az network nsg rule show `
        --resource-group $resourceGroup `
        --nsg-name $nsgName `
        --name $rule.Name 2>$null | ConvertFrom-Json
    
    if ($existingRule) {
        Write-Host "  ✓ Rule already exists" -ForegroundColor Green
    } else {
        az network nsg rule create `
            --resource-group $resourceGroup `
            --nsg-name $nsgName `
            --name $rule.Name `
            --priority $rule.Priority `
            --direction Inbound `
            --access Allow `
            --protocol Tcp `
            --destination-port-ranges $rule.Port `
            --description $rule.Description `
            --output none
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  ✓ Rule created successfully" -ForegroundColor Green
        } else {
            Write-Host "  ✗ Failed to create rule" -ForegroundColor Red
        }
    }
}

# Display all KVStore-related rules
Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "NSG Rules Summary" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""

$allRules = az network nsg rule list `
    --resource-group $resourceGroup `
    --nsg-name $nsgName `
    --query "[?contains(name, 'KVStore') || contains(name, 'WinRM')]" | ConvertFrom-Json

if ($allRules) {
    foreach ($r in $allRules) {
        Write-Host "$($r.name)" -ForegroundColor Green
        Write-Host "  Priority: $($r.priority)" -ForegroundColor White
        Write-Host "  Port: $($r.destinationPortRange)" -ForegroundColor White
        Write-Host "  Direction: $($r.direction)" -ForegroundColor White
        Write-Host "  Access: $($r.access)" -ForegroundColor White
        Write-Host ""
    }
}

Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "NSG Configuration Complete!" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Run: .\setupServer.ps1 -ConfigFile $ConfigFile" -ForegroundColor White
Write-Host ""
