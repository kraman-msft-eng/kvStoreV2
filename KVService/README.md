# KV Store gRPC Service (Standalone Project)

High-performance gRPC service layer for the Azure KV Store library. This is a **standalone project** that references the KV Store library as a dependency.

## Why Separate Project?

This service is maintained separately from the main kvStore library to:
- Keep the core KV Store library build simple and focused (especially Linux builds)
- Allow independent versioning and deployment
- Add gRPC dependencies without affecting library consumers
- Enable Service Fabric packaging without impacting library usage

## Prerequisites

1. **CMake 3.15+**
2. **Visual Studio 2022** (Windows) or **GCC 7+** (Linux)
3. **vcpkg** - Package manager (will be bootstrapped automatically by CMake)
4. **KV Store Library** - Must be in sibling directory: `../kvStore`

## Quick Start

### 1. Clone or Copy This Project

Ensure the directory structure looks like:
```
source/
├── kvStore/                  # The main KV Store library
└── KVStoreGrpcService/       # This service project (standalone)
```

### 2. Build the Service

#### Windows

```powershell
# Navigate to service directory
cd C:\Users\kraman\source\KVStoreGrpcService

# Configure (vcpkg will auto-install dependencies)
cmake -B build -S .

# Build
cmake --build build --config Release
```

#### Linux

```bash
# Navigate to service directory
cd ~/KVStoreGrpcService

# Configure
cmake -B build -S .

# Build
cmake --build build --config Release
```

### 3. Run the Server

```powershell
# Windows
.\build\Release\KVStoreServer.exe --log-level info

# Linux
./build/KVStoreServer --log-level info --transport libcurl
```

### 4. Test with Client

```powershell
.\build\Release\KVStoreClient.exe `
  --server localhost:50051 `
  --account "https://youraccount.blob.core.windows.net" `
  --container "yourcontainer"
```

## Architecture

- **Standalone Build**: Independent from kvStore library build system
- **Source Dependency**: Compiles KVStoreLibV2 from source (no binary dependency)
- **Multi-tenant**: Each request includes account credentials
- **High Performance**: gRPC with HTTP/2, async I/O, connection pooling

## Project Structure

```
KVStoreGrpcService/
├── protos/
│   └── kvstore.proto          # gRPC service definition
├── include/
│   └── KVStoreServiceImpl.h   # Service interface
├── src/
│   ├── KVStoreServiceImpl.cpp # Service implementation
│   ├── server.cpp             # Server executable
│   └── client_test.cpp        # Test client
├── CMakeLists.txt             # Standalone build configuration
├── vcpkg.json                 # Package dependencies
├── README.md                  # This file
└── QUICKSTART.md              # Detailed guide
```

## Dependencies

Managed by vcpkg.json:
- `grpc` - gRPC framework
- `protobuf` - Protocol Buffers
- `azure-storage-blobs-cpp` - Azure Storage SDK
- `azure-identity-cpp` - Azure authentication
- `azure-core-cpp` - Azure core libraries

## Configuration Options

### CMake Options

- `KVSTORE_LIB_PATH` - Path to kvStore library (default: `../kvStore`)

Example:
```bash
cmake -B build -S . -DKVSTORE_LIB_PATH=/path/to/kvStore
```

### Server Command Line

```bash
KVStoreServer [options]
  --port PORT              Port to listen on (default: 50051)
  --host HOST              Host to bind to (default: 0.0.0.0)
  --log-level LEVEL        Log level: error, info, verbose
  --transport TRANSPORT    HTTP transport: winhttp, libcurl
  --disable-sdk-logging    Disable Azure SDK logging
  --enable-multi-nic       Enable multi-NIC support
```

## API Overview

### Lookup
Finds cached blocks matching a token sequence.

### Read
Retrieves a chunk by location.

### Write
Stores a new chunk.

See `QUICKSTART.md` for detailed API documentation and examples.

## Deployment

### Local Development
Run the server directly for testing.

### Azure Service Fabric
Package as a stateless service for production deployment (coming soon).

### Docker
```dockerfile
FROM mcr.microsoft.com/cbl-mariner/base/core:2.0
COPY build/KVStoreServer /app/
WORKDIR /app
EXPOSE 50051
CMD ["./KVStoreServer"]
```

## Troubleshooting

### Build Fails - "Could not find gRPC"
- Ensure vcpkg is properly configured
- Check CMAKE_TOOLCHAIN_FILE path in CMakeLists.txt
- Try: `vcpkg install grpc protobuf --triplet=x64-windows-static`

### Build Fails - "Cannot find kvStore"
- Verify kvStore library exists at `../kvStore`
- Adjust with: `-DKVSTORE_LIB_PATH=/correct/path`

### Server won't start
- Check port 50051 is not in use
- Verify firewall allows connections
- Try a different port: `--port 50052`

## Updating from kvStore Library

When the kvStore library is updated:

1. No rebuild needed if only implementation changed
2. If headers changed in `AzureStorageKVStoreLib/include`, rebuild this service
3. If `KVTypes.h` or `AzureStorageKVStoreLibV2.h` changed, may need proto updates

## Development

### Adding New RPC Methods

1. Update `protos/kvstore.proto`
2. Implement in `src/KVStoreServiceImpl.cpp`
3. Rebuild to regenerate code

### Changing Dependencies

Edit `vcpkg.json` and reconfigure:
```bash
cmake -B build -S . --fresh
```

## Support

- Main documentation: See README.md and QUICKSTART.md
- KV Store library: See `../kvStore/` for library documentation
- Issues: Report issues specific to gRPC service layer here
- Library issues: Report to kvStore project

## License

Same as parent kvStore project.
