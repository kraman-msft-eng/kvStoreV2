# KVClient - gRPC Client Library for Linux

This library provides a Linux-compatible implementation of the `AzureStorageKVStoreLibV2` interface using gRPC to communicate with the Windows-based KVService.

## Architecture

- **Interface**: Same as `AzureStorageKVStoreLibV2.h` - drop-in replacement
- **Implementation**: gRPC client that connects to KVService
- **Platform**: Linux (x64)
- **Dependencies**: gRPC, Protobuf

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

Set the gRPC server address via environment variable:

```bash
export KVSTORE_GRPC_SERVER="your-windows-server:50051"
```

Then use the library exactly like `AzureStorageKVStoreLibV2`:

```cpp
#include "AzureStorageKVStoreLibV2.h"

AzureStorageKVStoreLibV2 kvStore;
kvStore.Initialize(
    "https://account.blob.core.windows.net",
    "container-name",
    HttpTransportProtocol::LibCurl  // Ignored in gRPC client
);

// Use Lookup, Read, Write as normal
auto result = kvStore.Lookup(partitionKey, completionId, tokens.begin(), tokens.end(), hashes);
```

## Configuration

- **KVSTORE_GRPC_SERVER**: Environment variable for server address (default: `localhost:50051`)

## Features

- ✅ Same API as AzureStorageKVStoreLibV2
- ✅ Transparent gRPC communication
- ✅ Async operations supported
- ✅ Thread-safe
- ✅ Linux-native build
