# Latency Metrics Explained

This document explains how latency metrics are calculated and measured across the KV Store client-server architecture.

## Overview

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              CLIENT E2E (measured by client)                        │
│                                                                                     │
│  ┌──────────┐     ┌─────────────────────────────────────────────────┐    ┌────────┐│
│  │ Serialize│────►│              NETWORK RTT                        │───►│Deserial││
│  │ Request  │     │  (gRPC call - Server Total - Client Deser)      │    │Response││
│  └──────────┘     │                                                 │    └────────┘│
│                   │  ┌───────────────────────────────────────────┐  │              │
│                   │  │         SERVER TOTAL (rpc_start→rpc_end)  │  │              │
│                   │  │  (starts AFTER gRPC request decode)       │  │              │
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
│                   │  │  (token conversion, response building,    │  │              │
│                   │  │   protobuf ENCODING - NOT decoding)       │  │              │
│                   │  │                                           │  │              │
│                   │  └───────────────────────────────────────────┘  │              │
│                   │                                                 │              │
│                   └─────────────────────────────────────────────────┘              │
│                                                                                     │
└─────────────────────────────────────────────────────────────────────────────────────┘

**Note**: Server Total does NOT include gRPC request deserialization. That time is 
captured in the Network RTT measurement (along with actual network transit time).
```

## Client-Side Timing Breakdown

The client measures three distinct phases for each RPC:

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│                           CLIENT E2E BREAKDOWN                                     │
│                                                                                    │
│  ┌─────────────────┐  ┌──────────────────────────────────┐  ┌───────────────────┐ │
│  │   SERIALIZE     │  │         GRPC CALL                │  │   DESERIALIZE     │ │
│  │   (serialize_us)│  │   (includes network + server)    │  │   (deserialize_us)│ │
│  ├─────────────────┤  ├──────────────────────────────────┤  ├───────────────────┤ │
│  │ Build request   │  │ ┌──────────────────────────────┐ │  │ Extract response  │ │
│  │ protobuf:       │  │ │   PURE NETWORK (network_us)  │ │  │ protobuf:         │ │
│  │                 │  │ │ = gRPC call - Server Total   │ │  │                   │ │
│  │ - set fields    │  │ │   - Deserialize              │ │  │ - copy buffer     │ │
│  │ - copy buffer   │  │ │                              │ │  │ - copy tokens     │ │
│  │ - add tokens    │  │ │ Includes:                    │ │  │ - extract fields  │ │
│  │                 │  │ │ - Request network transit    │ │  │                   │ │
│  │ Write: ~50-200μs│  │ │ - Server protobuf decode     │ │  │ Read: ~50-200μs   │ │
│  │ Read: ~5-10μs   │  │ │ - Response network transit   │ │  │ Write: ~0μs       │ │
│  │ Lookup: ~10-20μs│  │ │ - gRPC overhead              │ │  │ Lookup: ~5-10μs   │ │
│  │                 │  │ └──────────────────────────────┘ │  │                   │ │
│  └─────────────────┘  └──────────────────────────────────┘  └───────────────────┘ │
│                                                                                    │
│  Formula: network_us = gRPC_call_time - server_total_us - deserialize_us          │
│                                                                                    │
└────────────────────────────────────────────────────────────────────────────────────┘
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
    │   ──────────────────║   REQUEST     ║─────────────────────►                     │
    │                     ║   TRANSIT     ║                                           │
    │                     ╚═══════════════╝                                           │
    │                                                                                 │
    │                                                              gRPC receives      │
    │                                                              HTTP/2 frames      │
    │                                                                                 │
    │                                                              Protobuf decode    │
    │                                                              (deserialize       │
    │                                                               LookupRequest)    │
    │                                                                                 │
t1  │                                                              rpc_start ────────│
    │                                                              [Start timer]      │
    │                                                              (Reactor created)  │
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
    │                                                              Protobuf encode    │
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
    Server Total    = t4 - t1        (measured by server, AFTER request decode)
    Server Storage  = t3 - t2        (measured by server, sent in response)
    Server Overhead = (t4-t1)-(t3-t2) = Server Total - Server Storage
    Network RTT     = (t6-t0)-(t4-t1) = Client E2E - Server Total
                                       (includes request decode time + actual network)
```

## Example Breakdown: Lookup

```
Lookup (70000 operations):
  Client E2E:      p50=4.544ms  ◄─── Total time from client's perspective
  Server Storage:  p50=3.579ms  ◄─── Azure Blob lookup (Bloom filter + index)
  Server Total:    p50=3.790ms  ◄─── Entire time request spent on server
  Server Overhead: p50=0.211ms  ◄─── gRPC/protobuf/processing overhead
  Serialize:       p50=0.015ms  ◄─── Client request building (small payload)
  Deserialize:     p50=0.008ms  ◄─── Client response extraction (hash + position)
  Pure Network:    p50=0.731ms  ◄─── Actual network transit + gRPC framing

  ┌────────────────────────────────────────────────────────────────┐
  │                     Client E2E: 4.544ms                        │
  ├───────┬──────────┬─────────────────────────────────┬───────┬───┤
  │Ser    │ Net 0.4ms│     Server Total: 3.79ms        │Net    │Des│
  │0.015ms│  (req)   ├─────────────────────┬───────────┤0.33ms │er │
  │       │          │Storage: 3.579ms     │Ovhd 0.21ms│(resp) │8μs│
  └───────┴──────────┴─────────────────────┴───────────┴───────┴───┘
```

## Streaming Read Operation (Bidirectional Streaming RPC)

For streaming reads, the metrics work differently because multiple chunks are streamed:

```
                    CLIENT (Linux)                              SERVER (Windows)
                    ════════════════                            ════════════════

    ┌─────────────────────────────────────────────────────────────────────────────────┐
    │                                                                                 │
t0  │   stream_start ─────────────┐                                                   │
    │   [Start E2E timer]         │                                                   │
    │                             ▼                                                   │
    │   Send all requests         ║                                                   │
    │   (pipelining)              ║                                                   │
    │                     ╔═══════════════╗                                           │
    │                     ║   NETWORK     ║                                           │
    │   ──────────────────║   REQUESTS    ║──────────────────────►                    │
    │                     ╚═══════════════╝                                           │
    │                                                                                 │
    │                                                   ┌─────────────────────────────│
    │                                                   │  FOR EACH CHUNK (parallel): │
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
    │                                                   │                             │
    │   deser_start[i] ───────┐                         │                             │
    │   Extract PromptChunk:  │                         │                             │
    │   - copy buffer         │ deserialize_us[i]       │                             │
    │   - copy tokens         │                         │                             │
    │   - extract fields      │                         │                             │
    │   deser_end[i] ─────────┘                         │                             │
    │                                                   └─────────────────────────────│
    │                                                                                 │
    │   (repeat for all chunks)                                                       │
    │                                                                                 │
t1  │   stream_end (last chunk deserialized)                                          │
    │                                                                                 │
    └─────────────────────────────────────────────────────────────────────────────────┘

    CALCULATED METRICS:
    ═══════════════════

    Client E2E         = t1 - t0                          (entire stream duration)
    Max Storage        = max(storage_latency[i])          (longest individual blob read)
    Total Deserialize  = sum(deserialize_us[i])           (all chunk extraction time)
    Pure Network       = Client E2E - Max Storage - Total Deserialize
    Transport Delay    = Client E2E - Max Storage         (includes deser + network)
```

## Example Breakdown: Read (Streaming)

```
Read (60000 streaming operations):
  Client E2E:      p50=21.618ms  ◄─── Total time to receive all chunks
  Max Storage:     p50=14.334ms  ◄─── Longest single blob download
  Deserialize:     p50= 0.150ms  ◄─── Total PromptChunk extraction (buffer+tokens)
  Pure Network:    p50= 7.134ms  ◄─── Actual network + gRPC streaming overhead
  Transport Delay: p50= 7.284ms  ◄─── Network + deser combined

  ┌────────────────────────────────────────────────────────────────┐
  │                     Client E2E: 21.618ms                       │
  ├────────────────────────────────┬─────────────────────┬─────────┤
  │   Max Storage: 14.334ms        │ Pure Net: 7.134ms   │Deser    │
  │                                │                     │0.15ms   │
  │   (slowest blob download       │  (network + gRPC    │         │
  │    determines minimum time)    │   streaming + HTTP2)│         │
  └────────────────────────────────┴─────────────────────┴─────────┘
  
  Per-chunk deserialization breakdown:
  ┌────────────────────────────────────────────────────────────────┐
  │ For each 4KB PromptChunk (~100 tokens):                        │
  │   - Buffer copy (4KB):     ~20-30μs                            │
  │   - Token copy (100 ints): ~10-15μs                            │
  │   - Field extraction:      ~5-10μs                             │
  │   Total per chunk:         ~35-55μs                            │
  └────────────────────────────────────────────────────────────────┘
```

## Write Operation

```
Write (30000 operations):
  Client E2E:      p50=32.778ms  ◄─── Total time from client's perspective
  Server Storage:  p50=26.666ms  ◄─── Azure Blob write (upload chunk)
  Server Total:    p50=26.834ms  ◄─── Entire time request spent on server
  Server Overhead: p50= 0.168ms  ◄─── Very low (minimal processing)
  Serialize:       p50= 0.120ms  ◄─── Client PromptChunk serialization (buffer+tokens)
  Pure Network:    p50= 5.824ms  ◄─── Higher due to larger request payload

  ┌────────────────────────────────────────────────────────────────┐
  │                     Client E2E: 32.778ms                       │
  ├───────┬──────────┬─────────────────────────────────┬───────────┤
  │Ser    │ Net 3ms  │     Server Total: 26.834ms      │ Net 2.9ms │
  │0.12ms │  (req)   ├─────────────────────┬───────────┤  (resp)   │
  │(large │ (large   │Storage: 26.666ms    │Ovhd 0.17ms│  (small)  │
  │buffer)│ payload) │                     │           │           │
  └───────┴──────────┴─────────────────────┴───────────┴───────────┘
  
  Serialization breakdown for 4KB PromptChunk:
  ┌────────────────────────────────────────────────────────────────┐
  │ Write request serialization:                                   │
  │   - set_buffer(4KB data):    ~50-100μs   ◄─── Main cost        │
  │   - add_tokens(100 ints):    ~20-40μs                          │
  │   - set scalar fields:       ~5-10μs                           │
  │   Total serialize_us:        ~75-150μs                         │
  └────────────────────────────────────────────────────────────────┘
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

// === SERIALIZE TIMING ===
auto serialize_start = std::chrono::high_resolution_clock::now();

// Build request protobuf
WriteRequest request;
request.set_resource_name(resourceName_);
auto* protoChunk = request.mutable_chunk();
protoChunk->set_buffer(chunk.buffer.data(), chunk.buffer.size());  // Main cost
for (auto token : chunk.tokens) {
    protoChunk->add_tokens(static_cast<int64_t>(token));
}

auto serialize_end = std::chrono::high_resolution_clock::now();

// === GRPC CALL ===
auto grpc_start = std::chrono::high_resolution_clock::now();
Status status = stub_->Write(&context, request, &response);
auto grpc_end = std::chrono::high_resolution_clock::now();

// === DESERIALIZE TIMING (for Read) ===
auto deserialize_start = std::chrono::high_resolution_clock::now();

// Extract response protobuf (for Read - PromptChunk)
const auto& protoChunk = response.chunk();
chunk.buffer.assign(bufferData.begin(), bufferData.end());  // Main cost
for (auto token : protoChunk.tokens()) {
    chunk.tokens.push_back(static_cast<Token>(token));
}

auto deserialize_end = std::chrono::high_resolution_clock::now();

// === CALCULATE METRICS ===
auto serialize_us = duration_cast<microseconds>(serialize_end - serialize_start).count();
auto grpc_us = duration_cast<microseconds>(grpc_end - grpc_start).count();
auto deserialize_us = duration_cast<microseconds>(deserialize_end - deserialize_start).count();

metrics.serialize_us = serialize_us;
metrics.deserialize_us = deserialize_us;
metrics.client_e2e_us = grpc_us;

// Pure network = gRPC call - server processing - client deserialize
metrics.network_us = grpc_us - response.server_metrics().total_latency_us() - deserialize_us;
```

### Playground (Linux) - Metric Display

```cpp
// In main.cpp - displayDetailedStats()

// Server Overhead = Server Total - Storage
std::cout << "  Server Overhead: p50=" << ((serverTotalP50 - storageP50) / 1000.0) << "ms";

// Client-side serialization/deserialization breakdown
std::cout << "  Serialize:       p50=" << (serP50 / 1000.0) << "ms";
std::cout << "  Deserialize:     p50=" << (deserP50 / 1000.0) << "ms";
std::cout << "  Pure Network:    p50=" << (netP50 / 1000.0) << "ms";
```

## Metrics Summary Table

| Metric | Description | Calculation |
|--------|-------------|-------------|
| **Client E2E** | Total client-observed latency | `grpc_end - grpc_start` |
| **Serialize** | Time to build request protobuf | `serialize_end - serialize_start` |
| **Deserialize** | Time to extract response protobuf | `deserialize_end - deserialize_start` |
| **Pure Network** | Actual network + gRPC framing | `gRPC_call - Server_Total - Deserialize` |
| **Server Total** | Time in server reactor (after decode) | `rpc_end - rpc_start` |
| **Server Storage** | Azure Blob operation time | `storage_end - storage_start` |
| **Server Overhead** | Server processing overhead | `Server_Total - Server_Storage` |

## Where Time Goes (Summary)

| Component | Lookup | Read (Streaming) | Write | Notes |
|-----------|--------|------------------|-------|-------|
| **Client E2E** | 4.5ms | 21.6ms | 32.8ms | What user experiences |
| **Serialize** | 0.015ms (0.3%) | ~0.005ms (~0%) | 0.12ms (0.4%) | Request building |
| **Deserialize** | 0.008ms (0.2%) | 0.15ms (0.7%) | ~0ms | Response extraction |
| **Pure Network** | 0.73ms (16%) | 7.1ms (33%) | 5.8ms (18%) | Actual transit time |
| **Server Storage** | 3.6ms (79%) | 14.3ms (66%) | 26.7ms (81%) | Azure Blob operations |
| **Server Overhead** | 0.2ms (5%) | - | 0.2ms (1%) | gRPC/protobuf on server |

## Optimization Insights

1. **Storage dominates** - 66-81% of time is Azure Blob Storage
2. **Server overhead is minimal** - gRPC async + protobuf is fast (~0.2ms)
3. **Network varies** - Private VNet gives low RTT; streaming has higher transport delay
4. **Writes are slowest** - Blob uploads take longer than lookups/reads
5. **Serialization is cheap** - Even for 4KB PromptChunk, serialize/deserialize is <0.2ms combined
6. **Buffer copy is the main cost** - In both serialize and deserialize, the buffer copy dominates

## Key Takeaways for PromptChunk Serialization

```
┌────────────────────────────────────────────────────────────────┐
│ PromptChunk (~4KB buffer + ~100 tokens):                       │
│                                                                │
│ WRITE (Serialize):                                             │
│   serialize_us ≈ 75-150μs (0.4% of 32ms E2E)                   │
│   - set_buffer() is the main cost                              │
│   - Protobuf copies the buffer to internal storage             │
│                                                                │
│ READ (Deserialize):                                            │
│   deserialize_us ≈ 35-55μs per chunk (0.7% of 21ms E2E)        │
│   - buffer.assign() copies data from protobuf                  │
│   - Token vector copy is secondary                             │
│                                                                │
│ CONCLUSION: Protobuf ser/deser overhead is negligible (<1%)    │
│             compared to Azure Storage (66-81%) and Network     │
└────────────────────────────────────────────────────────────────┘
```
