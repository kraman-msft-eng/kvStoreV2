# Build KVStoreGrpcService with local Azure SDK - enables Multi-NIC support and custom curl options
# This flavor uses the locally-built Azure SDK with custom features

$ErrorActionPreference = "Stop"

Write-Host "===== Building KVStoreGrpcService with Local Azure SDK (Multi-NIC ENABLED) =====" -ForegroundColor Cyan
Write-Host ""

Write-Host "Configuring CMake with local Azure SDK..." -ForegroundColor Green
$cmakePath = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

& $cmakePath -B build -S . `
    -DUSE_LOCAL_AZURE_SDK=ON `
    -DCMAKE_PREFIX_PATH="C:/Users/kraman/azure-sdk-local-windows-mt" `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE="C:/Users/kraman/source/vcpkg/scripts/buildsystems/vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static

if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configuration failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Building KVStoreServer..." -ForegroundColor Green
& $cmakePath --build build --config Release --target KVStoreServer

if ($LASTEXITCODE -ne 0) {
    Write-Host "Server build failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Building KVStoreClient..." -ForegroundColor Green
& $cmakePath --build build --config Release --target KVStoreClient

if ($LASTEXITCODE -ne 0) {
    Write-Host "Client build failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "===== Build Successful =====" -ForegroundColor Cyan
Write-Host "Features enabled: Multi-NIC, Custom curl options" -ForegroundColor Green
Write-Host "Server: build/Release/KVStoreServer.exe" -ForegroundColor Green
Write-Host "Client: build/Release/KVStoreClient.exe" -ForegroundColor Green
