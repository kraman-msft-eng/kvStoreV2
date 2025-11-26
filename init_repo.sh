#!/bin/bash
# Initialize KVStoreV2 Git repository

echo "Initializing KVStoreV2 Git Repository"
echo "======================================"
echo ""

cd "$(dirname "$0")"

# Initialize git
if [ ! -d ".git" ]; then
    echo "Initializing git repository..."
    git init
    echo "✓ Git repository initialized"
else
    echo "✓ Git repository already exists"
fi

# Add all files
echo ""
echo "Adding files to git..."
git add .

# Show status
echo ""
echo "Git status:"
git status --short

# Create initial commit
echo ""
read -p "Create initial commit? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
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
    
    echo ""
    echo "✓ Initial commit created"
fi

echo ""
echo "======================================"
echo "Repository ready!"
echo ""
echo "Next steps:"
echo "  1. Set up remote: git remote add origin <url>"
echo "  2. Push to remote: git push -u origin main"
echo "  3. Test Windows build: cd KVService && .\build_with_local_sdk.ps1"
echo "  4. Test Linux build: ./build_linux.sh"
echo ""
