@echo off
REM SetupDataPlaneRegion.cmd - Wrapper for SetupDataPlaneRegion.ps1
REM Usage: SetupDataPlaneRegion.cmd <RegionName> <SubscriptionId> <ResourceGroupName> [ConfigStorageAccount]

if "%~3"=="" (
    echo Usage: SetupDataPlaneRegion.cmd ^<RegionName^> ^<SubscriptionId^> ^<ResourceGroupName^> [ConfigStorageAccount]
    echo.
    echo Example: SetupDataPlaneRegion.cmd westus2 6cbd0699-eae8-4633-8054-691a6b726d90 kvstore-dp-westus2 promptdatawestus2
    exit /b 1
)

set REGION=%~1
set SUBSCRIPTION=%~2
set RG=%~3
set CONFIG_STORAGE=%~4

echo.
echo ====================================
echo  Setting up Data Plane Region
echo ====================================
echo  Region:           %REGION%
echo  Subscription:     %SUBSCRIPTION%
echo  Resource Group:   %RG%
echo  Config Storage:   %CONFIG_STORAGE%
echo ====================================
echo.

if "%CONFIG_STORAGE%"=="" (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0SetupDataPlaneRegion.ps1" -RegionName %REGION% -SubscriptionId %SUBSCRIPTION% -ResourceGroupName %RG%
) else (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0SetupDataPlaneRegion.ps1" -RegionName %REGION% -SubscriptionId %SUBSCRIPTION% -ResourceGroupName %RG% -ConfigStorageAccount %CONFIG_STORAGE%
)
