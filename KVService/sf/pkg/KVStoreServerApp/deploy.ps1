[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$ClusterEndpoint,

    [Parameter(Mandatory=$true)]
    [string]$ServerCertThumbprint,

    [Parameter(Mandatory=$true)]
    [string]$ClientCertThumbprint,

    [Parameter()]
    [string]$ApplicationName = 'fabric:/KVStoreServerApp',

    [Parameter()]
    [string]$ApplicationTypeName = 'KVStoreServerAppType',

    [Parameter()]
    [string]$ApplicationTypeVersion = '1.0.0',

    [Parameter()]
    [hashtable]$AppParameters = @{},

    [Parameter()]
    [string]$AppParametersJson = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Service Fabric PowerShell cmdlets are Windows PowerShell-only and load native DLLs.
# If invoked from PowerShell 7+, re-run this script in Windows PowerShell.
if ($PSVersionTable.PSEdition -eq 'Core') {
    $forwardJson = $AppParametersJson
    if ([string]::IsNullOrWhiteSpace($forwardJson) -and $AppParameters.Count -gt 0) {
        $forwardJson = ($AppParameters | ConvertTo-Json -Compress -Depth 20)
    }

    $argsList = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $PSCommandPath,
        '-ClusterEndpoint', $ClusterEndpoint,
        '-ServerCertThumbprint', $ServerCertThumbprint,
        '-ClientCertThumbprint', $ClientCertThumbprint,
        '-ApplicationName', $ApplicationName,
        '-ApplicationTypeName', $ApplicationTypeName,
        '-ApplicationTypeVersion', $ApplicationTypeVersion
    )
    if (-not [string]::IsNullOrWhiteSpace($forwardJson)) {
        $argsList += @('-AppParametersJson', $forwardJson)
    }

    & powershell.exe @argsList
    exit $LASTEXITCODE
}

# Add SF native DLLs to path
$sfNativeDir = Join-Path $env:ProgramFiles 'Microsoft Service Fabric\bin\Fabric\Fabric.Code'
if (Test-Path $sfNativeDir) {
    $pathParts = $env:Path -split ';'
    if ($pathParts -notcontains $sfNativeDir) {
        $env:Path = "$sfNativeDir;$env:Path"
    }
}

# Import ServiceFabric module
if (-not (Get-Module -ListAvailable -Name ServiceFabric)) {
    $candidateModuleManifests = @(
        (Join-Path $env:ProgramFiles 'Microsoft Service Fabric\bin\ServiceFabric\ServiceFabric.psd1'),
        (Join-Path $env:ProgramFiles 'Microsoft Service Fabric\bin\Fabric\Fabric.Code\ServiceFabric.psd1'),
        (Join-Path $env:ProgramFiles 'Microsoft SDKs\Service Fabric\Tools\PSModule\ServiceFabric\ServiceFabric.psd1')
    )

    $imported = $false
    foreach ($candidate in $candidateModuleManifests) {
        if (Test-Path $candidate) {
            try {
                Import-Module $candidate -Force -ErrorAction Stop
                $imported = $true
                break
            } catch {
                # fall through to try next location
            }
        }
    }

    if (-not $imported -and -not (Get-Module -ListAvailable -Name ServiceFabric)) {
        $searched = ($candidateModuleManifests -join '; ')
        throw "Service Fabric PowerShell module not found via PSModulePath and could not be imported from known locations: $searched. Install the Service Fabric SDK on the machine running this script."
    }
}

if (-not (Get-Module -Name ServiceFabric)) {
    Import-Module ServiceFabric
}

# Parse JSON parameters if provided
if (-not [string]::IsNullOrWhiteSpace($AppParametersJson)) {
    function ConvertTo-Hashtable {
        param([Parameter(Mandatory=$true)]$InputObject)
        if ($null -eq $InputObject) { return $null }

        if ($InputObject -is [System.Collections.IDictionary]) {
            return $InputObject
        }

        if ($InputObject -is [System.Collections.IEnumerable] -and -not ($InputObject -is [string])) {
            $list = @()
            foreach ($item in $InputObject) {
                $list += ConvertTo-Hashtable -InputObject $item
            }
            return $list
        }

        if ($InputObject -is [psobject]) {
            $ht = @{}
            foreach ($p in $InputObject.PSObject.Properties) {
                $ht[$p.Name] = ConvertTo-Hashtable -InputObject $p.Value
            }
            return $ht
        }

        return $InputObject
    }

    try {
        $parsedObj = $AppParametersJson | ConvertFrom-Json
        $parsed = ConvertTo-Hashtable -InputObject $parsedObj
        if ($null -ne $parsed) {
            $AppParameters = $parsed
        }
    } catch {
        throw "Failed to parse -AppParametersJson as JSON: $($_.Exception.Message)"
    }
}

Write-Host "Connecting to Service Fabric cluster: $ClusterEndpoint" -ForegroundColor Cyan
Connect-ServiceFabricCluster -ConnectionEndpoint $ClusterEndpoint -X509Credential -ServerCertThumbprint $ServerCertThumbprint -FindType FindByThumbprint -FindValue $ClientCertThumbprint -StoreLocation CurrentUser -StoreName My

$pkgPath = (Resolve-Path (Join-Path $PSScriptRoot '.')).Path
$imageStorePath = 'KVStoreServerApp'

Write-Host "Copying app package to image store: $pkgPath" -ForegroundColor Cyan
Copy-ServiceFabricApplicationPackage -ApplicationPackagePath $pkgPath -ImageStoreConnectionString 'fabric:ImageStore' -ApplicationPackagePathInImageStore $imageStorePath

# Check if this version is already registered
$existingType = Get-ServiceFabricApplicationType -ApplicationTypeName $ApplicationTypeName | Where-Object { $_.ApplicationTypeVersion -eq $ApplicationTypeVersion }
if ($existingType) {
    Write-Host "Application type version $ApplicationTypeVersion already registered, unregistering first..." -ForegroundColor Yellow
    Unregister-ServiceFabricApplicationType -ApplicationTypeName $ApplicationTypeName -ApplicationTypeVersion $ApplicationTypeVersion -Force
}

Write-Host "Registering application type: $ApplicationTypeName $ApplicationTypeVersion" -ForegroundColor Cyan
Register-ServiceFabricApplicationType -ApplicationPathInImageStore $imageStorePath

$existing = Get-ServiceFabricApplication -ApplicationName $ApplicationName -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "Upgrading existing application: $ApplicationName" -ForegroundColor Cyan
    Start-ServiceFabricApplicationUpgrade -ApplicationName $ApplicationName -ApplicationTypeVersion $ApplicationTypeVersion -ApplicationParameter $AppParameters -Monitored -FailureAction Rollback
} else {
    Write-Host "Creating application: $ApplicationName" -ForegroundColor Cyan
    New-ServiceFabricApplication -ApplicationName $ApplicationName -ApplicationTypeName $ApplicationTypeName -ApplicationTypeVersion $ApplicationTypeVersion -ApplicationParameter $AppParameters
}

Write-Host "Done." -ForegroundColor Green
