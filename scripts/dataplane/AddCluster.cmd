@echo off
REM AddCluster.cmd - Wrapper for AddCluster.ps1
REM Usage: AddCluster.cmd <ClusterName> <RegionName> <AdminPassword> [Options]
REM Options: -UseInternalLoadBalancer, -SystemVmSize, -AppVmSize, -SystemVmCount, -AppVmCount

if "%~3"=="" (
    echo Usage: AddCluster.cmd ^<ClusterName^> ^<RegionName^> ^<AdminPassword^> [Options]
    echo.
    echo Standard Mode (single node type, public LB):
    echo   AddCluster.cmd kvstore-c1 westus2 "YourSecureP@ssw0rd!"
    echo.
    echo BYOLB Mode (internal LB for apps, smaller system nodes):
    echo   AddCluster.cmd kvstore-c1 westus2 "YourSecureP@ssw0rd!" -UseInternalLoadBalancer
    echo.
    echo Default VM sizes:
    echo   System Node Type: Standard_D2s_v5 x 3 (smaller, for SF services only)
    echo   App Node Type:    Standard_D2ds_v5 x 3 (with local SSD for apps)
    exit /b 1
)

set CLUSTER=%~1
set REGION=%~2
set PASSWORD=%~3

REM Parse remaining arguments for switches
set ILB_FLAG=
set EXTRA_ARGS=

:parse_args
shift
shift
shift
:loop
if "%~1"=="" goto end_loop
if /i "%~1"=="-UseInternalLoadBalancer" (
    set ILB_FLAG=-UseInternalLoadBalancer
) else (
    set EXTRA_ARGS=%EXTRA_ARGS% %~1 %~2
    shift
)
shift
goto loop
:end_loop

echo.
echo ====================================
echo  Adding Cluster to Region
echo ====================================
echo  Cluster:       %CLUSTER%
echo  Region:        %REGION%
if defined ILB_FLAG echo  Mode:          BYOLB (Internal LB)
if not defined ILB_FLAG echo  Mode:          Standard (Public LB)
echo ====================================
echo.

pwsh -NoProfile -ExecutionPolicy Bypass -Command "& '%~dp0AddCluster.ps1' -ClusterName '%CLUSTER%' -RegionName '%REGION%' -AdminPassword (ConvertTo-SecureString '%PASSWORD%' -AsPlainText -Force) %ILB_FLAG% %EXTRA_ARGS%"
