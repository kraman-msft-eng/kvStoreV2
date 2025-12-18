@echo off
REM DeployApp.cmd - Wrapper for DeployApp.ps1
REM Usage: DeployApp.cmd <ClusterName> <RegionName> [AppVersion]

if "%~2"=="" (
    echo Usage: DeployApp.cmd ^<ClusterName^> ^<RegionName^> [AppVersion]
    echo.
    echo Example: DeployApp.cmd kvstore-c1 westus2
    echo Example: DeployApp.cmd kvstore-c1 westus2 1.0.5
    exit /b 1
)

set CLUSTER=%~1
set REGION=%~2
set VERSION=%~3

echo.
echo ====================================
echo  Deploying Application
echo ====================================
echo  Cluster:       %CLUSTER%
echo  Region:        %REGION%
echo  Version:       %VERSION%
echo ====================================
echo.

if "%VERSION%"=="" (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0DeployApp.ps1" -ClusterName %CLUSTER% -RegionName %REGION%
) else (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0DeployApp.ps1" -ClusterName %CLUSTER% -RegionName %REGION% -AppVersion %VERSION%
)
