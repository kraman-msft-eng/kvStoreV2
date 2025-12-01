# KVStoreV2 Repository Setup Summary

## Repository Structure Created

```
KVStoreV2/
│
├── README.md                      # Main repository documentation
├── CMakeLists.txt                 # Root CMake for Linux builds
├── build_linux.sh                 # Linux build script
├── .gitignore                     # Git ignore patterns
│
├── KVService/                     # Windows gRPC Server
│   ├── src/
│   │   ├── server.cpp             # gRPC server entry point
│   │   ├── client_test.cpp        # Test client (128 tokens)
│   │   ├── KVStoreServiceImpl.cpp # Service implementation
│   │   ├── AzureStorageKVStoreLibV2.cpp
│   │   └── BloomFilter.cpp
│   ├── include/
│   │   ├── KVStoreServiceImpl.h
│   │   ├── AzureStorageKVStoreLibV2.h
│   │   ├── KVTypes.h
│   │   └── BloomFilter.h
│   ├── protos/
│   │   └── kvstore.proto          # gRPC protocol definition
│   ├── CMakeLists.txt             # Windows CMake configuration
│   ├── build_with_local_sdk.ps1   # Windows build script
│   ├── build.ps1
│   ├── vcpkg.json
│   └── README.md
│
├── KVClient/                      # Linux gRPC Client Library
│   ├── src/
│   │   └── KVStoreGrpcClient.cpp  # gRPC client wrapper
│   ├── include/
│   │   ├── AzureStorageKVStoreLibV2.h  # Same interface as original
│   │   └── KVTypes.h
│   ├── protos/
│   │   └── kvstore.proto          # Same proto as service
│   ├── CMakeLists.txt             # Linux CMake configuration
│   └── README.md
│
└── KVPlayground/                  # Linux Test Application
    ├── src/
    │   └── main.cpp               # Uses KVClient (not direct lib)
    ├── CMakeLists.txt             # Updated to use KVClient
    └── README.md
```

## Key Features

### 1. KVService (Windows)
- **Platform**: Windows x64
- **Technology**: gRPC server, Azure Blob Storage backend
- **Runtime**: Static /MT with local Azure SDK
- **Features**:
  - Multi-NIC support (CurlTransport)
  - Bloom filter caching
  - 128-token block storage
  - Async operations
- **Build**: `.\build_with_local_sdk.ps1`
- **Run**: `.\build\Release\KVStoreServer.exe`

### 2. KVClient (Linux)
- **Platform**: Linux x64
- **Technology**: gRPC client library
- **Interface**: AzureStorageKVStoreLibV2 (drop-in replacement)
- **Features**:
  - Transparent gRPC communication
  - Same API as original library
  - Thread-safe async operations
  - Configurable via environment variable
- **Build**: Part of root CMake on Linux
- **Configuration**: `export KVSTORE_GRPC_SERVER="server:50051"`

### 3. KVPlayground (Linux)
- **Platform**: Linux x64
- **Technology**: Test application
- **Updated**: Now uses KVClient instead of direct library
- **Features**:
  - Multi-threaded testing
  - Performance benchmarking
  - Cache validation
- **Build**: Part of root CMake on Linux

## Protocol

The gRPC protocol (kvstore.proto) defines three operations:

1. **Lookup**: Find cached token blocks by partition/tokens/hashes
2. **Read**: Retrieve chunk data by location
3. **Write**: Store new chunks to cache

## Build Instructions

### Windows (KVService)

```powershell
cd KVService
.\build_with_local_sdk.ps1

# Run server
.\build\Release\KVStoreServer.exe

# Test with client
.\build\Release\KVStoreClient.exe --account "https://aoaikv.blob.core.windows.net" --container "gpt51-promptcache"
```

### Linux (KVClient + KVPlayground)

```bash
# Prerequisites
sudo apt-get install build-essential cmake git pkg-config libssl-dev

# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git ~/vcpkg
cd ~/vcpkg
./bootstrap-vcpkg.sh
./vcpkg install grpc protobuf nlohmann-json

# Build
cd ~/KVStoreV2
export VCPKG_ROOT=~/vcpkg
./build_linux.sh

# Run
export KVSTORE_GRPC_SERVER="windows-server:50051"
./build/KVPlayground/KVPlayground conversation_tokens.json 10 2
```

## Configuration

### Environment Variables

- **KVSTORE_GRPC_SERVER**: gRPC server address for KVClient
  - Default: `localhost:50051`
  - Example: `192.168.1.100:50051` or `myserver.example.com:50051`

- **VCPKG_ROOT**: Path to vcpkg installation (Linux)
  - Default: `~/vcpkg`
  - Used by build scripts

### Server Configuration (KVService)

The server can be configured via command-line arguments (see `server.cpp`):
- Port number (default: 50051)
- Log level (error, info, verbose)
- Azure Storage account URL
- Container name

## Testing

1. **Start Windows Server**:
   ```powershell
   cd KVService\build\Release
   .\KVStoreServer.exe
   ```

2. **Run Linux Client**:
   ```bash
   export KVSTORE_GRPC_SERVER="192.168.1.100:50051"
   ./build/KVPlayground/KVPlayground conversation_tokens.json 5 2
   ```

3. **Verify**:
   - Server logs show Lookup/Read/Write operations
   - Client shows successful cache hits
   - 128-token blocks are written and retrieved correctly

## API Compatibility

KVClient provides **100% API compatibility** with AzureStorageKVStoreLibV2:

```cpp
// Initialization
AzureStorageKVStoreLibV2 kvStore;
kvStore.Initialize(accountUrl, containerName, HttpTransportProtocol::LibCurl);

// Lookup
auto result = kvStore.Lookup(partitionKey, completionId, 
                             tokens.begin(), tokens.end(), hashes);

// Read
auto future = kvStore.ReadAsync(location, completionId);
auto [success, chunk] = future.get();

// Write
auto writeFuture = kvStore.WriteAsync(chunk);
writeFuture.wait();
```

The only difference is that KVClient internally uses gRPC instead of direct Azure Storage access.

## Migration Path

To migrate existing code from direct AzureStorageKVStoreLibV2 to KVClient:

1. **No code changes needed** - API is identical
2. **Set environment variable**: `KVSTORE_GRPC_SERVER`
3. **Recompile with KVClient** instead of linking to AzureStorageKVStoreLibV2
4. **Deploy**: Ensure Windows gRPC service is accessible

## Next Steps

1. **Initialize Git Repository**:
   ```bash
   cd C:\Users\kraman\source\KVStoreV2
   git init
   git add .
   git commit -m "Initial commit: KVStoreV2 with KVService, KVClient, and KVPlayground"
   ```

2. **Test Windows Build**:
   ```powershell
   cd KVService
   .\build_with_local_sdk.ps1
   ```

3. **Test Linux Build** (on Linux machine):
   ```bash
   ./build_linux.sh
   ```

4. **Set up CI/CD**:
   - Windows build pipeline for KVService
   - Linux build pipeline for KVClient + KVPlayground
   - Cross-platform testing

5. **Documentation**:
   - Add API documentation
   - Add deployment guide
   - Add troubleshooting section

## Repository Ready ✅

The repository is now properly structured with:
- ✅ Three distinct projects with clear separation
- ✅ KVService for Windows with local Azure SDK
- ✅ KVClient for Linux with gRPC implementation
- ✅ KVPlayground updated to use KVClient
- ✅ Build scripts for both platforms
- ✅ Comprehensive documentation
- ✅ Protocol definition shared across projects
- ✅ .gitignore configured
- ✅ API compatibility maintained
