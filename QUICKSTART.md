# KVStoreV2 Quick Reference

## Directory Structure
```
KVStoreV2/
├── KVService/          # Windows gRPC server
├── KVClient/           # Linux gRPC client library
└── KVPlayground/       # Linux test application
```

## Build Commands

### Windows (KVService)
```powershell
cd KVService
.\build_with_local_sdk.ps1
```

### Linux (KVClient + KVPlayground)
```bash
export VCPKG_ROOT=~/vcpkg
./build_linux.sh
```

## Run Commands

### Start Server (Windows)
```powershell
cd KVService\build\Release
.\KVStoreServer.exe
```

### Run Client Test (Windows)
```powershell
.\KVStoreClient.exe --account "https://aoaikv.blob.core.windows.net" --container "gpt51-promptcache"
```

### Run Playground (Linux)
```bash
export KVSTORE_GRPC_SERVER="windows-server-ip:50051"
./build/KVPlayground/KVPlayground conversation_tokens.json 10 2
```

## Configuration

### KVClient Server Address
```bash
export KVSTORE_GRPC_SERVER="192.168.1.100:50051"
```

### Azure Storage (Server-side)
- Account URL: Passed via Initialize() or command-line
- Container: Passed via Initialize() or command-line

## API Example

```cpp
#include "AzureStorageKVStoreLibV2.h"

// Initialize (works same on both Windows and Linux)
AzureStorageKVStoreLibV2 kvStore;
kvStore.Initialize(
    "https://account.blob.core.windows.net",
    "container-name",
    HttpTransportProtocol::LibCurl
);

// Write 128-token block
PromptChunk chunk;
chunk.hash = 12345;
chunk.parentHash = 0;
chunk.partitionKey = "my-partition";
chunk.completionId = "completion-001";
// ... fill chunk.tokens (128 tokens), chunk.buffer

auto writeFuture = kvStore.WriteAsync(chunk);
writeFuture.wait();

// Lookup
std::vector<hash_t> hashes = {12345};
auto result = kvStore.Lookup(
    "my-partition",
    "completion-001",
    tokens.begin(),
    tokens.end(),
    hashes
);

// Read if found
if (result.cachedBlocks > 0) {
    auto readFuture = kvStore.ReadAsync(result.locations[0].location);
    auto [success, retrievedChunk] = readFuture.get();
}
```

## Ports

- gRPC Server: **50051** (default)
- Can be changed in server configuration

## Dependencies

### Windows (KVService)
- vcpkg: grpc, protobuf, curl, openssl
- Local Azure SDK: C:/Users/kraman/azure-sdk-local-windows-mt

### Linux (KVClient + KVPlayground)
- vcpkg: grpc, protobuf, nlohmann-json
- System: build-essential, cmake, pkg-config, libssl-dev

## Troubleshooting

### Connection Refused
- Check server is running: `Get-Process KVStoreServer` (Windows)
- Verify firewall allows port 50051
- Check KVSTORE_GRPC_SERVER is correct

### Build Errors (Linux)
- Ensure vcpkg packages installed: `~/vcpkg/vcpkg list`
- Check VCPKG_ROOT is set correctly
- Verify CMake finds toolchain file

### gRPC Errors
- Verify both client and server use same proto version
- Check network connectivity between machines
- Ensure 128-token blocks (not 5 tokens)

## Performance Tips

- Use 128-token blocks for optimal performance
- Pass precomputed hashes in Lookup for faster matching
- Use async operations (WriteAsync, ReadAsync) for concurrency
- Enable bloom filter (default in service)

## File Locations

### Built Binaries
- Windows Server: `KVService/build/Release/KVStoreServer.exe`
- Windows Client: `KVService/build/Release/KVStoreClient.exe`
- Linux Client Lib: `build/KVClient/libKVClient.a`
- Linux Playground: `build/KVPlayground/KVPlayground`

### Configuration Files
- Proto: `protos/kvstore.proto` (same in KVService and KVClient)
- vcpkg: `KVService/vcpkg.json`
- CMake: `CMakeLists.txt` (in each project + root)

## Common Tasks

### Add New RPC Method
1. Edit `protos/kvstore.proto`
2. Rebuild KVService (Windows)
3. Copy updated proto to KVClient
4. Rebuild KVClient (Linux)

### Change Server Port
- Edit `KVService/src/server.cpp`
- Update default port or add command-line arg

### Update Client Server Address
```bash
export KVSTORE_GRPC_SERVER="new-server:50051"
```

### Clean Build
```powershell
# Windows
Remove-Item KVService\build -Recurse -Force

# Linux
rm -rf build/
```
