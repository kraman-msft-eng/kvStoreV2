# Latency Metrics Explained

This document explains how latency metrics are calculated and measured across the KV Store client-server architecture.

## Overview

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              CLIENT E2E (measured by client)                        │
│                                                                                     │
│  ┌──────────┐     ┌─────────────────────────────────────────────────┐    ┌────────┐│
│  │ Request  │────►│              NETWORK RTT                        │───►│Response││
│  │  Build   │     │  (Client E2E - Server Total)                    │    │ Parse  ││
│  └──────────┘     │                                                 │    └────────┘│
│                   │  ┌───────────────────────────────────────────┐  │              │
│                   │  │         SERVER TOTAL (rpc_start→rpc_end)  │  │              │
│                   │  │                                           │  │              │
│                   │  │  ┌─────────────────────────────────────┐  │  │              │
│                   │  │  │     SERVER STORAGE                  │  │  │              │
│                   │  │  │   (storage_start→storage_end)       │  │  │              │
│                   │  │  │                                     │  │  │              │
│                   │  │  │  ┌────────────────────────────────┐ │  │  │              │
│                   │  │  │  │   Azure Blob Storage Call      │ │  │  │              │
│                   │  │  │  │   (Lookup/Read/Write)          │ │  │  │              │
│                   │  │  │  └────────────────────────────────┘ │  │  │              │
│                   │  │  │                                     │  │  │              │
│                   │  │  └─────────────────────────────────────┘  │  │              │
│                   │  │                                           │  │              │
│                   │  │  SERVER OVERHEAD = Server Total - Storage │  │              │
│                   │  │  (gRPC deserialization, token conversion, │  │              │
│                   │  │   response building, protobuf encoding)   │  │              │
│                   │  │                                           │  │              │
│                   │  └───────────────────────────────────────────┘  │              │
│                   │                                                 │              │
│                   └─────────────────────────────────────────────────┘              │
│                                                                                     │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

## Lookup / Write Operations (Unary RPC)

```
                    CLIENT (Linux)                              SERVER (Windows)
                    ════════════════                            ════════════════

    ┌─────────────────────────────────────────────────────────────────────────────────┐
    │                                                                                 │
t0  │   client_start ─────────────┐                                                   │
    │   [Start timer]             │                                                   │
    │                             │                                                   │
    │   Build gRPC request        │                                                   │
    │   (set resource_name,       │                                                   │
    │    container, tokens,       │                                                   │
    │    partition_key)           │                                                   │
    │                             │                                                   │
    │                             ▼                                                   │
    │                     ╔═══════════════╗                                           │
    │                     ║   NETWORK     ║                                           │
t1  │   ──────────────────║   REQUEST     ║─────────────────────►  rpc_start ────────│
    │                     ║   TRANSIT     ║                        [Start timer]      │
    │                     ╚═══════════════╝                                           │
    │                                                                                 │
    │                                                              Validate request   │
    │                                                              Resolve account    │
    │                                                              Convert tokens     │
    │                                                                                 │
t2  │                                                              storage_start ────│
    │                                                              [Start timer]      │
    │                                                                                 │
    │                                                              ┌────────────────┐ │
    │                                                              │ Azure Storage  │ │
    │                                                              │ Blob Lookup/   │ │
    │                                                              │ Read/Write     │ │
    │                                                              └────────────────┘ │
    │                                                                                 │
t3  │                                                              storage_end ──────│
    │                                                              [Stop timer]       │
    │                                                                                 │
    │                                                              Build response     │
    │                                                              Set server_metrics │
    │                                                                                 │
t4  │                                                              rpc_end ──────────│
    │                     ╔═══════════════╗                        [Stop timer]       │
    │                     ║   NETWORK     ║                                           │
t5  │   ◄─────────────────║   RESPONSE    ║◄─────────────────────────────────────────│
    │                     ║   TRANSIT     ║                                           │
    │                     ╚═══════════════╝                                           │
    │                                                                                 │
    │   Parse response                                                                │
    │   Extract server_metrics                                                        │
    │                                                                                 │
t6  │   client_end ───────────────┘                                                   │
    │   [Stop timer]                                                                  │
    │                                                                                 │
    └─────────────────────────────────────────────────────────────────────────────────┘

    ════════════════════════════════════════════════════════════════════════════════════

    CALCULATED METRICS:
    ═══════════════════

    Client E2E      = t6 - t0        (measured by client)
    Server Total    = t4 - t1        (measured by server, sent in response)
    Server Storage  = t3 - t2        (measured by server, sent in response)
    Server Overhead = (t4-t1)-(t3-t2) = Server Total - Server Storage
    Network RTT     = (t6-t0)-(t4-t1) = Client E2E - Server Total
```

## Example Breakdown: Lookup

```
Lookup (70000 operations):
  Client E2E:      p50=4.544ms  ◄─── Total time from client's perspective
  Server Storage:  p50=3.579ms  ◄─── Azure Blob lookup (Bloom filter + index)
  Server Total:    p50=3.790ms  ◄─── Entire time request spent on server
  Server Overhead: p50=0.211ms  ◄─── gRPC/protobuf/processing overhead
  Network RTT:     p50=0.754ms  ◄─── Round-trip network latency

  ┌────────────────────────────────────────────────────────────────┐
  │                     Client E2E: 4.544ms                        │
  ├──────────┬─────────────────────────────────────────┬───────────┤
  │ Net 0.4ms│           Server Total: 3.79ms          │ Net 0.35ms│
  │  (req)   ├────────────────────────────┬────────────┤  (resp)   │
  │          │ Storage: 3.579ms           │Ovhd 0.21ms │           │
  └──────────┴────────────────────────────┴────────────┴───────────┘
              ◄────────────────────────────────────────►
                        Sent in gRPC response
```

## Streaming Read Operation (Server-Side Streaming RPC)

For streaming reads, the metrics work differently because multiple chunks are streamed:

```
                    CLIENT (Linux)                              SERVER (Windows)
                    ════════════════                            ════════════════

    ┌─────────────────────────────────────────────────────────────────────────────────┐
    │                                                                                 │
t0  │   client_start ─────────────┐                                                   │
    │   [Start E2E timer]         │                                                   │
    │                             ▼                                                   │
    │                     ╔═══════════════╗                                           │
    │                     ║   NETWORK     ║                                           │
    │   ──────────────────║   REQUEST     ║──────────────────────►                    │
    │                     ╚═══════════════╝                                           │
    │                                                                                 │
    │                                                   ┌─────────────────────────────│
    │                                                   │  FOR EACH CHUNK:            │
    │                                                   │                             │
    │                                                   │  storage_start[i]           │
    │                                                   │  ┌────────────────┐         │
    │                                                   │  │ Azure Storage  │         │
    │                                                   │  │ Blob Download  │         │
    │                                                   │  └────────────────┘         │
    │                                                   │  storage_end[i]             │
    │                                                   │                             │
    │             ╔═══════════════╗                     │  Stream chunk               │
    │   ◄─────────║   STREAM      ║◄────────────────────┤                             │
    │   chunk[i]  ║   CHUNK[i]    ║  (includes latency) │                             │
    │             ╚═══════════════╝                     │                             │
    │                                                   └─────────────────────────────│
    │                                                                                 │
    │   (repeat for all chunks)                                                       │
    │                                                                                 │
t1  │   client_end (last chunk received)                                              │
    │                                                                                 │
    └─────────────────────────────────────────────────────────────────────────────────┘

    CALCULATED METRICS:
    ═══════════════════

    Client E2E       = t1 - t0                        (entire stream duration)
    Max Storage      = max(storage_latency[i])        (longest individual blob read)
    Transport Delay  = Client E2E - Max Storage       (network + gRPC streaming overhead)
```

## Example Breakdown: Read (Streaming)

```
Read (60000 streaming operations):
  Client E2E:      p50=21.618ms  ◄─── Total time to receive all chunks
  Max Storage:     p50=14.334ms  ◄─── Longest single blob download
  Transport Delay: p50= 7.284ms  ◄─── Network + streaming + serialization

  ┌────────────────────────────────────────────────────────────────┐
  │                     Client E2E: 21.618ms                       │
  ├────────────────────────────────┬───────────────────────────────┤
  │   Max Storage: 14.334ms        │  Transport Delay: 7.284ms     │
  │                                │  (network + gRPC streaming +  │
  │   (slowest blob download       │   protobuf serialization +    │
  │    determines minimum time)    │   HTTP/2 framing)             │
  └────────────────────────────────┴───────────────────────────────┘
```

## Write Operation

```
Write (30000 operations):
  Client E2E:      p50=32.778ms  ◄─── Total time from client's perspective
  Server Storage:  p50=26.666ms  ◄─── Azure Blob write (upload chunk)
  Server Total:    p50=26.834ms  ◄─── Entire time request spent on server
  Server Overhead: p50= 0.168ms  ◄─── Very low (minimal processing)
  Network RTT:     p50= 5.944ms  ◄─── Higher due to larger request payload

  ┌────────────────────────────────────────────────────────────────┐
  │                     Client E2E: 32.778ms                       │
  ├──────────┬─────────────────────────────────────────┬───────────┤
  │ Net 3ms  │           Server Total: 26.834ms        │ Net 2.9ms │
  │  (req)   ├─────────────────────────────┬───────────┤  (resp)   │
  │  (larger │ Storage: 26.666ms           │Ovhd 0.17ms│  (small)  │
  │  payload)│                             │           │           │
  └──────────┴─────────────────────────────┴───────────┴───────────┘
```

## Code References

### Server (Windows) - Metric Calculation

```cpp
// In LookupReactor.cpp / WriteReactor.cpp

rpc_start_ = std::chrono::high_resolution_clock::now();  // When request arrives

// ... later in async thread ...

auto storage_start = std::chrono::high_resolution_clock::now();
auto result = store->Lookup(...);  // Azure Storage call
auto storage_end = std::chrono::high_resolution_clock::now();

// Build response
auto rpc_end = std::chrono::high_resolution_clock::now();

// Calculate metrics
auto storage_us = duration_cast<microseconds>(storage_end - storage_start).count();
auto total_us = duration_cast<microseconds>(rpc_end - rpc_start_).count();

// Populate response
metrics->set_storage_latency_us(storage_us);
metrics->set_total_latency_us(total_us);
metrics->set_overhead_us(total_us - storage_us);
```

### Client (Linux) - Metric Calculation

```cpp
// In KVStoreGrpcClient.cpp

auto client_start = std::chrono::high_resolution_clock::now();
Status status = stub_->Lookup(&context, request, &response);  // gRPC call
auto client_end = std::chrono::high_resolution_clock::now();

// Client E2E
auto e2e_us = duration_cast<microseconds>(client_end - client_start).count();

// Network RTT (derived)
auto network_rtt_us = e2e_us - response.server_metrics().total_latency_us();
```

### Playground (Linux) - Metric Display

```cpp
// In main.cpp

// Server Overhead = Server Total - Storage
std::cout << "  Server Overhead: p50=" << ((serverTotalP50 - storageP50) / 1000.0) << "ms";

// Network RTT = Client E2E - Server Total  
std::cout << "  Network RTT:     p50=" << ((e2eP50 - serverTotalP50) / 1000.0) << "ms";
```

## Where Time Goes (Summary)

| Component | Lookup | Read | Write | Notes |
|-----------|--------|------|-------|-------|
| **Client E2E** | 4.5ms | 21.6ms | 32.8ms | What user experiences |
| **Server Storage** | 3.6ms (79%) | 14.3ms (66%) | 26.7ms (81%) | Azure Blob operations |
| **Server Overhead** | 0.2ms (5%) | - | 0.2ms (1%) | gRPC/protobuf processing |
| **Network RTT** | 0.8ms (16%) | 7.3ms (34%) | 5.9ms (18%) | Round-trip latency |

## Optimization Insights

1. **Storage dominates** - 66-81% of time is Azure Blob Storage
2. **Server overhead is minimal** - gRPC async + protobuf is fast (~0.2ms)
3. **Network varies** - Private VNet gives low RTT; streaming has higher transport delay
4. **Writes are slowest** - Blob uploads take longer than lookups/reads
