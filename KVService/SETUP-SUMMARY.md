# KV Store gRPC Service - Standalone Project Setup

## Summary

The gRPC service layer has been created as a **standalone project** separate from the main kvStore library. This keeps your Linux builds clean and focused.

## Directory Structure

```
C:\Users\kraman\source\
├── kvStore/                      # Your main KV Store library (unchanged)
│   ├── AzureStorageKVStoreLib/  # Core library
│   ├── KVPlayground/            # Test apps
│   ├── tests/                   # Unit tests
│   └── CMakeLists.txt           # Library build (no gRPC deps)
│
└── KVStoreGrpcService/          # NEW: Standalone gRPC service
    ├── protos/                  # gRPC API definitions
    │   └── kvstore.proto
    ├── include/                 # Service headers
    │   └── KVStoreServiceImpl.h
    ├── src/                     # Service implementation
    │   ├── KVStoreServiceImpl.cpp
    │   ├── server.cpp          # Server executable
    │   └── client_test.cpp     # Test client
    ├── CMakeLists.txt          # Standalone build
    ├── vcpkg.json              # Service dependencies (includes gRPC)
    ├── README.md               # Service documentation
    └── QUICKSTART.md           # Getting started guide
```

## Key Benefits

1. **Clean Separation**: kvStore library builds remain unchanged
2. **No gRPC Dependencies**: Your Linux builds don't need gRPC/protobuf
3. **Independent Versioning**: Service can evolve separately
4. **Source Reference**: Service compiles KVStoreLibV2 from source (no binary dependency)

## What Changed in kvStore

✅ Removed: `add_subdirectory("KVStoreService")` from CMakeLists.txt
✅ Removed: gRPC/protobuf from vcpkg.json
✅ Result: Your existing builds work exactly as before

## Building the Service

### From Windows:

```powershell
# Navigate to service directory
cd C:\Users\kraman\source\KVStoreGrpcService

# Configure and build
cmake -B build -S .
cmake --build build --config Release

# Run server
.\build\Release\KVStoreServer.exe --log-level info
```

### From Linux (when ready):

```bash
cd ~/KVStoreGrpcService
cmake -B build -S .
cmake --build build --config Release
./build/KVStoreServer --transport libcurl
```

## How It Works

1. **Standalone Build**: Service has its own CMakeLists.txt with gRPC dependencies
2. **Source Dependency**: Compiles `AzureStorageKVStoreLibV2.cpp` and `BloomFilter.cpp` from `../kvStore`
3. **Header Include**: References headers from `../kvStore/AzureStorageKVStoreLib/include`
4. **No Library Build**: Service doesn't build the library separately, just compiles needed source files

## API Design

The gRPC service exposes three operations:

### 1. Lookup(account_url, container, tokens) → locations
- Finds cached blocks for token sequence
- Account specified per request (multi-tenant)

### 2. Read(account_url, container, location) → chunk
- Retrieves chunk data by location
- Location from Lookup response

### 3. Write(account_url, container, chunk) → success
- Stores new chunk
- Full chunk data in request

## Next Steps

1. **Install gRPC via vcpkg** (if not already installed):
   ```powershell
   # Find or install vcpkg
   git clone https://github.com/Microsoft/vcpkg.git C:\tools\vcpkg
   cd C:\tools\vcpkg
   .\bootstrap-vcpkg.bat
   
   # Install packages (will take 15-30 minutes)
   .\vcpkg install grpc:x64-windows-static protobuf:x64-windows-static
   ```

2. **Update CMakeLists.txt toolchain path** (if needed):
   Edit `KVStoreGrpcService/CMakeLists.txt` line 10 to point to your vcpkg installation

3. **Build the service**:
   ```powershell
   cd C:\Users\kraman\source\KVStoreGrpcService
   cmake -B build -S .
   cmake --build build --config Release
   ```

4. **Test locally** with your Azure Storage account

5. **Package for Service Fabric** when ready for production

## Deploying to Azure

### Option 1: Azure VM
- Run `KVStoreServer` as a service/daemon
- Use systemd (Linux) or Windows Service

### Option 2: Azure Container Instances
- Build Docker image with KVStoreServer
- Deploy to ACI with managed identity

### Option 3: Azure Service Fabric (Recommended)
- Package as stateless service
- Auto-scaling and high availability
- Integration with Azure Monitor

## For More Information

- See `KVStoreGrpcService/README.md` for full documentation
- See `KVStoreGrpcService/QUICKSTART.md` for detailed examples
- Service implementation in `KVStoreGrpcService/src/`
- Proto definitions in `KVStoreGrpcService/protos/`

---

**Your kvStore library is completely unchanged and ready for Linux builds!**
