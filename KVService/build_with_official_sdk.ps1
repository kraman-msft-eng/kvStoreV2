# Build KVStoreGrpcService with official Azure SDK from vcpkg - standard production build
# This flavor uses the official Azure SDK, multi-NIC and custom features disabled

$ErrorActionPreference = "Stop"

Write-Host "===== Building KVStoreGrpcService with Official Azure SDK (Multi-NIC DISABLED) =====" -ForegroundColor Cyan
Write-Host ""

Write-Host "Configuring CMake with official Azure SDK from vcpkg..." -ForegroundColor Green
cmake -B build -S . `
    -DUSE_LOCAL_AZURE_SDK=OFF `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE="C:/Users/kraman/source/vcpkg/scripts/buildsystems/vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static

if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configuration failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Building KVStoreServer..." -ForegroundColor Green
cmake --build build --config Release --target KVStoreServer

if ($LASTEXITCODE -ne 0) {
    Write-Host "Server build failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "===== Build Successful =====" -ForegroundColor Cyan
Write-Host "Standard production build using vcpkg Azure SDK" -ForegroundColor Green
Write-Host "Server: build/Release/KVStoreServer.exe" -ForegroundColor Green
