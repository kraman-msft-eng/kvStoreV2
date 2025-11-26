# KVStoreV2 - High-Performance Key-Value Store with gRPC

A distributed key-value store system for caching GPT prompt tokens, featuring a Windows gRPC service backed by Azure Blob Storage and a Linux client library.

## Architecture

```
┌─────────────────────┐         gRPC          ┌─────────────────────┐
│   Linux Client      │ ◄──────────────────► │  Windows Service    │
│                     │                       │                     │
│  - KVPlayground     │                       │    KVService        │
│  - KVClient lib     │                       │  (gRPC Server)      │
│                     │                       │         │           │
└─────────────────────┘                       │         ▼           │
                                              │  Azure Blob Storage │
                                              └─────────────────────┘
```

## Projects

### 1. **KVService** (Windows)
- **Purpose**: gRPC service that manages Azure Blob Storage operations
- **Platform**: Windows x64
- **Features**:
  - Multi-NIC support with custom Azure SDK
  - CurlTransport for network optimization
  - Bloom filter for fast lookups
  - 128-token block caching
  - Static runtime (/MT)
- **Build**: `cd KVService && .\build_with_local_sdk.ps1`

### 2. **KVClient** (Linux)
- **Purpose**: gRPC client library with `AzureStorageKVStoreLibV2` interface
- **Platform**: Linux x64
- **Features**:
  - Drop-in replacement for `AzureStorageKVStoreLibV2`
  - Transparent gRPC communication
  - Same API as original library
  - Thread-safe async operations
- **Build**: See Linux build instructions below

### 3. **KVPlayground** (Linux)
- **Purpose**: Test application demonstrating KVClient usage
- **Platform**: Linux x64
- **Features**:
  - Multi-threaded testing
  - Precomputed token support
  - Performance benchmarking
  - Cache validation
- **Build**: Built automatically with KVClient

## Quick Start

### Windows (KVService)

```powershell
cd KVService
.\build_with_local_sdk.ps1

# Run server
.\build\Release\KVStoreServer.exe
```

### Linux (KVClient + KVPlayground)

```bash
# Prerequisites
sudo apt-get install build-essential cmake
# Install vcpkg and gRPC (see detailed instructions below)

# Build
mkdir build && cd build
cmake ..
make

# Set server address
export KVSTORE_GRPC_SERVER="your-windows-server:50051"

# Run playground
./KVPlayground/KVPlayground conversation_tokens.json 10 5
```

## Building on Linux

### Prerequisites

1. **Install dependencies**:
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git pkg-config \
    libssl-dev autoconf libtool curl unzip
```

2. **Install vcpkg**:
```bash
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
./vcpkg integrate install
```

3. **Install gRPC and Protobuf**:
```bash
./vcpkg install grpc protobuf nlohmann-json
```

### Build Steps

```bash
cd KVStoreV2
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
make -j$(nproc)
```

### Running

```bash
# Set server address
export KVSTORE_GRPC_SERVER="192.168.1.100:50051"

# Run KVPlayground
cd build/KVPlayground
./KVPlayground ../../conversation_tokens.json 5 2
```

## Configuration

### Environment Variables

- **KVSTORE_GRPC_SERVER**: gRPC server address (default: `localhost:50051`)
  ```bash
  export KVSTORE_GRPC_SERVER="myserver.example.com:50051"
  ```

### KVService Configuration

Edit `KVService/src/server.cpp` or use command-line arguments:
- `--port`: gRPC server port (default: 50051)
- `--log-level`: Logging level (error, info, verbose)

## API Usage

The KVClient provides the exact same API as `AzureStorageKVStoreLibV2`:

```cpp
#include "AzureStorageKVStoreLibV2.h"

// Initialize
AzureStorageKVStoreLibV2 kvStore;
kvStore.Initialize(
    "https://account.blob.core.windows.net",
    "container-name",
    HttpTransportProtocol::LibCurl  // Ignored in gRPC client
);

// Write
PromptChunk chunk;
chunk.hash = 12345;
chunk.partitionKey = "my-partition";
chunk.tokens = {1, 2, 3, ...};  // 128 tokens
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

// Read
if (result.cachedBlocks > 0) {
    auto readFuture = kvStore.ReadAsync(result.locations[0].location);
    auto [success, chunk] = readFuture.get();
}
```

## Protocol

The gRPC protocol is defined in `protos/kvstore.proto`:

- **Lookup**: Find cached token blocks
- **Read**: Retrieve cached chunks by location
- **Write**: Store new chunks

## Performance

- **Block Size**: 128 tokens
- **Caching**: Bloom filter for O(1) lookups
- **Concurrency**: Async operations, thread-safe
- **Network**: Multi-NIC support (Windows service)

## Development

### Project Structure

```
KVStoreV2/
├── CMakeLists.txt              # Root build file (Linux)
├── README.md                   # This file
├── KVService/                  # Windows gRPC service
│   ├── src/
│   ├── include/
│   ├── protos/
│   ├── CMakeLists.txt
│   └── build_with_local_sdk.ps1
├── KVClient/                   # Linux gRPC client library
│   ├── src/
│   │   └── KVStoreGrpcClient.cpp
│   ├── include/
│   │   ├── AzureStorageKVStoreLibV2.h
│   │   └── KVTypes.h
│   ├── protos/
│   │   └── kvstore.proto
│   ├── CMakeLists.txt
│   └── README.md
└── KVPlayground/               # Linux test application
    ├── src/
    │   └── main.cpp
    ├── CMakeLists.txt
    └── README.md
```

### Testing

1. **Start Windows service**:
```powershell
cd KVService\build\Release
.\KVStoreServer.exe
```

2. **Run Linux client**:
```bash
export KVSTORE_GRPC_SERVER="windows-host:50051"
./build/KVPlayground/KVPlayground conversation_tokens.json 10 2
```

## Troubleshooting

### Connection refused
- Ensure KVService is running on Windows
- Check firewall rules allow port 50051
- Verify KVSTORE_GRPC_SERVER is set correctly

### Build errors on Linux
- Ensure all vcpkg packages are installed
- Check CMake finds vcpkg toolchain file
- Verify g++ version >= 7.0

### gRPC errors
- Check network connectivity between Linux and Windows
- Verify protocol buffer versions match
- Ensure both client and server are up-to-date

## License

See LICENSE file for details.

## Contributing

1. Fork the repository
2. Create your feature branch
3. Commit your changes
4. Push to the branch
5. Create a Pull Request
