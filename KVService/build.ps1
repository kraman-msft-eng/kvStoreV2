# Build script for KV Store gRPC Service
# This standalone project builds independently from the kvStore library

param(
    [string]$Config = "Release",
    [switch]$Clean,
    [switch]$Run,
    [string]$VcpkgPath = "C:\Users\kraman\source\vcpkg"
)

$ErrorActionPreference = "Stop"

Write-Host "
==================================================
" -ForegroundColor Cyan
Write-Host "KV Store gRPC Service Build" -ForegroundColor Green
Write-Host "
==================================================
" -ForegroundColor Cyan

# Check if local KV Store library files exist
if (-not (Test-Path "src\AzureStorageKVStoreLibV2.cpp")) {
    Write-Host "❌ ERROR: KV Store library files not found in src\" -ForegroundColor Red
    Write-Host "   Expected: src\AzureStorageKVStoreLibV2.cpp, src\BloomFilter.cpp" -ForegroundColor Yellow
    exit 1
}

Write-Host "✓ Found KV Store library files in src\" -ForegroundColor Green

# Clean if requested
if ($Clean -and (Test-Path "build")) {
    Write-Host "
Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Path "build" -Recurse -Force
    Write-Host "✓ Build directory cleaned" -ForegroundColor Green
}

# Configure CMake
Write-Host "
Configuring CMake..." -ForegroundColor Yellow
Write-Host "  Config: $Config" -ForegroundColor Gray
Write-Host "  vcpkg: $VcpkgPath" -ForegroundColor Gray

$cmakePath = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if (Test-Path "$VcpkgPath\scripts\buildsystems\vcpkg.cmake") {
    & $cmakePath -S . -B build `
        -DCMAKE_BUILD_TYPE=$Config `
        -DCMAKE_TOOLCHAIN_FILE="$VcpkgPath\scripts\buildsystems\vcpkg.cmake" `
        -DVCPKG_TARGET_TRIPLET=x64-windows-static `
        -DCMAKE_PREFIX_PATH="C:/Users/kraman/azure-sdk-local-windows-mt" `
        -DUSE_LOCAL_AZURE_SDK=ON
} else {
    Write-Host "⚠ vcpkg not found at $VcpkgPath, using default configuration" -ForegroundColor Yellow
    & $cmakePath -B build -S .
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "
❌ CMake configuration failed" -ForegroundColor Red
    exit 1
}

Write-Host "
✓ CMake configuration successful" -ForegroundColor Green

# Build
Write-Host "
Building project ($Config)..." -ForegroundColor Yellow
& $cmakePath --build build --config $Config

if ($LASTEXITCODE -ne 0) {
    Write-Host "
❌ Build failed" -ForegroundColor Red
    exit 1
}

Write-Host "
✓ Build successful!" -ForegroundColor Green

# Show output location
$serverPath = "build\$Config\KVStoreServer.exe"
$clientPath = "build\$Config\KVStoreClient.exe"

if (Test-Path $serverPath) {
    Write-Host "
Executables created:" -ForegroundColor Cyan
    Write-Host "  Server: $serverPath" -ForegroundColor White
    Write-Host "  Client: $clientPath" -ForegroundColor White
}

# Run if requested
if ($Run) {
    Write-Host "
==================================================
" -ForegroundColor Cyan
    Write-Host "Starting KVStoreServer..." -ForegroundColor Green
    Write-Host "
==================================================
" -ForegroundColor Cyan
    
    & $serverPath --log-level info
}

Write-Host "
To run the server:" -ForegroundColor Yellow
Write-Host "  .\$serverPath --log-level info
" -ForegroundColor White
