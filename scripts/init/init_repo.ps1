# Initialize KVStoreV2 Git Repository (PowerShell version)

Write-Host ""
Write-Host "Initializing KVStoreV2 Git Repository" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

Set-Location $PSScriptRoot

# Initialize git
if (-not (Test-Path ".git")) {
    Write-Host "Initializing git repository..." -ForegroundColor Yellow
    git init
    Write-Host "✓ Git repository initialized" -ForegroundColor Green
} else {
    Write-Host "✓ Git repository already exists" -ForegroundColor Green
}

# Add all files
Write-Host ""
Write-Host "Adding files to git..." -ForegroundColor Yellow
git add .

# Show status
Write-Host ""
Write-Host "Git status:" -ForegroundColor Cyan
git status --short

# Create initial commit
Write-Host ""
$response = Read-Host "Create initial commit? (y/n)"
if ($response -eq 'y' -or $response -eq 'Y') {
    git commit -m "Initial commit: KVStoreV2

Repository structure:
- KVService: Windows gRPC server with Azure Blob Storage backend
- KVClient: Linux gRPC client library with AzureStorageKVStoreLibV2 interface
- KVPlayground: Linux test application using KVClient

Features:
- Multi-NIC support (Windows service)
- Bloom filter caching
- 128-token block storage
- Async gRPC operations
- Cross-platform architecture"
    
    Write-Host ""
    Write-Host "✓ Initial commit created" -ForegroundColor Green
}

Write-Host ""
Write-Host "======================================" -ForegroundColor Cyan
Write-Host "Repository ready!" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Set up remote: git remote add origin <url>"
Write-Host "  2. Push to remote: git push -u origin main"
Write-Host "  3. Test Windows build: cd KVService; .\build_with_local_sdk.ps1"
Write-Host "  4. Test Linux build: ./build_linux.sh (on Linux)"
Write-Host ""
