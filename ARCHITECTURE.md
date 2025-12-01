# KV Store Service Architecture

## High-Level Overview

The **KV Store Service** is a high-performance gRPC-based caching service designed for **KV (Key-Value) offload** in large language model inference scenarios. It provides a **unified endpoint** for model providers to work with multiple customer storage accounts without requiring individual Private Link (PLINK) connections from each inference node.

---

## System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    CUSTOMER AZURE SUBSCRIPTION                                          │
│  ┌───────────────────────────────────────────────────────────────────────────────────────────────────┐  │
│  │                              KV Store Service (Unified Endpoint)                                  │  │
│  │                                                                                                   │  │
│  │  ┌─────────────────────────────────────────────────────────────────────────────────────────────┐  │  │
│  │  │                         Windows VM (e.g., FX96mds_v2 - 96 cores)                            │  │  │
│  │  │                                                                                             │  │  │
│  │  │   ┌──────────────────────────────────┐   ┌──────────────────────────────────┐              │  │  │
│  │  │   │      KVStoreServer :8085         │   │      KVStoreServer :8086         │              │  │  │
│  │  │   │     (NUMA Node 0 - 48 cores)     │   │     (NUMA Node 1 - 48 cores)     │              │  │  │
│  │  │   │                                  │   │                                  │              │  │  │
│  │  │   │  ┌────────────────────────────┐  │   │  ┌────────────────────────────┐  │              │  │  │
│  │  │   │  │     gRPC Async Server      │  │   │  │     gRPC Async Server      │  │              │  │  │
│  │  │   │  │   (Callback API + HTTP/2)  │  │   │  │   (Callback API + HTTP/2)  │  │              │  │  │
│  │  │   │  └────────────┬───────────────┘  │   │  └────────────┬───────────────┘  │              │  │  │
│  │  │   │               │                  │   │               │                  │              │  │  │
│  │  │   │  ┌────────────▼───────────────┐  │   │  ┌────────────▼───────────────┐  │              │  │  │
│  │  │   │  │   KVStoreServiceImpl       │  │   │  │   KVStoreServiceImpl       │  │              │  │  │
│  │  │   │  │  ┌──────────────────────┐  │  │   │  │  ┌──────────────────────┐  │  │              │  │  │
│  │  │   │  │  │   AccountResolver    │  │  │   │  │  │   AccountResolver    │  │  │              │  │  │
│  │  │   │  │  │  (resource → store)  │  │  │   │  │  │  (resource → store)  │  │  │              │  │  │
│  │  │   │  │  └──────────────────────┘  │  │   │  │  └──────────────────────┘  │  │              │  │  │
│  │  │   │  │  ┌──────────────────────┐  │  │   │  │  ┌──────────────────────┐  │  │              │  │  │
│  │  │   │  │  │    Reactor Pool      │  │  │   │  │  │    Reactor Pool      │  │  │              │  │  │
│  │  │   │  │  │ Lookup|Read|Write    │  │  │   │  │  │ Lookup|Read|Write    │  │  │              │  │  │
│  │  │   │  │  │ StreamingRead        │  │  │   │  │  │ StreamingRead        │  │  │              │  │  │
│  │  │   │  │  └──────────────────────┘  │  │   │  │  └──────────────────────┘  │  │              │  │  │
│  │  │   │  └────────────┬───────────────┘  │   │  └────────────┬───────────────┘  │              │  │  │
│  │  │   │               │                  │   │               │                  │              │  │  │
│  │  │   │  ┌────────────▼───────────────┐  │   │  ┌────────────▼───────────────┐  │              │  │  │
│  │  │   │  │ AzureStorageKVStoreLibV2   │  │   │  │ AzureStorageKVStoreLibV2   │  │              │  │  │
│  │  │   │  │  (Bloom Filter + Cache)    │  │   │  │  (Bloom Filter + Cache)    │  │              │  │  │
│  │  │   │  └────────────────────────────┘  │   │  └────────────────────────────┘  │              │  │  │
│  │  │   └──────────────────────────────────┘   └──────────────────────────────────┘              │  │  │
│  │  │                        │                                   │                               │  │  │
│  │  │                        │  Multi-NIC Round Robin           │                               │  │  │
│  │  │                        │  (libcurl transport)              │                               │  │  │
│  │  │                        ▼                                   ▼                               │  │  │
│  │  │   ┌──────────────────────────────────────────────────────────────────────────────────────┐ │  │  │
│  │  │   │                              Network Interface Cards                                 │ │  │  │
│  │  │   │    ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐     │ │  │  │
│  │  │   │    │  NIC 0  │  │  NIC 1  │  │  NIC 2  │  │  NIC 3  │  │  NIC 4  │  │  NIC 5  │     │ │  │  │
│  │  │   │    └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘     │ │  │  │
│  │  │   └─────────┼────────────┼────────────┼────────────┼────────────┼────────────┼──────────┘ │  │  │
│  │  │             │            │            │            │            │            │            │  │  │
│  │  └─────────────┼────────────┼────────────┼────────────┼────────────┼────────────┼────────────┘  │  │
│  │                │            │            │            │            │            │               │  │
│  └────────────────┼────────────┼────────────┼────────────┼────────────┼────────────┼───────────────┘  │
│                   │            │            │            │            │            │                  │
│                   └────────────┼────────────┼────────────┼────────────┼────────────┘                  │
│                                │            │            │            │                               │
│                                ▼            ▼            ▼            ▼                               │
│  ┌──────────────────────────────────────────────────────────────────────────────────────────────────┐ │
│  │                              Azure Blob Storage Accounts                                          │ │
│  │                                                                                                   │ │
│  │   ┌───────────────────┐ ┌───────────────────┐ ┌───────────────────┐ ┌───────────────────┐        │ │
│  │   │  Storage Acct A   │ │  Storage Acct B   │ │  Storage Acct C   │ │  Storage Acct D   │  ...   │ │
│  │   │  (Customer 1)     │ │  (Customer 2)     │ │  (Customer 3)     │ │  (Customer 4)     │        │ │
│  │   │                   │ │                   │ │                   │ │                   │        │ │
│  │   │ ┌───────────────┐ │ │ ┌───────────────┐ │ │ ┌───────────────┐ │ │ ┌───────────────┐ │        │ │
│  │   │ │ KV Container  │ │ │ │ KV Container  │ │ │ │ KV Container  │ │ │ │ KV Container  │ │        │ │
│  │   │ │ (Token Chunks)│ │ │ │ (Token Chunks)│ │ │ │ (Token Chunks)│ │ │ │ (Token Chunks)│ │        │ │
│  │   │ └───────────────┘ │ │ └───────────────┘ │ │ └───────────────┘ │ │ └───────────────┘ │        │ │
│  │   └───────────────────┘ └───────────────────┘ └───────────────────┘ └───────────────────┘        │ │
│  │                                                                                                   │ │
│  └──────────────────────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                                       │
└───────────────────────────────────────────────────────────────────────────────────────────────────────┘
                                             ▲
                                             │  gRPC (HTTP/2)
                                             │  Single PLINK per provider
                                             │
┌───────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                MODEL PROVIDER INFRASTRUCTURE                                          │
│                                                                                                       │
│  ┌─────────────────────────────────────────────────────────────────────────────────────────────────┐  │
│  │                            Inference Node Cluster (Linux)                                       │  │
│  │                                                                                                 │  │
│  │  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────────┐                     │  │
│  │  │   Inference Node 1  │  │   Inference Node 2  │  │   Inference Node N  │                     │  │
│  │  │   ┌───────────────┐ │  │   ┌───────────────┐ │  │   ┌───────────────┐ │                     │  │
│  │  │   │   LLM Model   │ │  │   │   LLM Model   │ │  │   │   LLM Model   │ │                     │  │
│  │  │   │  (GPT/etc.)   │ │  │   │  (GPT/etc.)   │ │  │   │  (GPT/etc.)   │ │                     │  │
│  │  │   └───────┬───────┘ │  │   └───────┬───────┘ │  │   └───────┬───────┘ │                     │  │
│  │  │           │         │  │           │         │  │           │         │                     │  │
│  │  │   ┌───────▼───────┐ │  │   ┌───────▼───────┐ │  │   ┌───────▼───────┐ │                     │  │
│  │  │   │   KVClient    │ │  │   │   KVClient    │ │  │   │   KVClient    │ │                     │  │
│  │  │   │  (gRPC stub)  │ │  │   │  (gRPC stub)  │ │  │   │  (gRPC stub)  │ │                     │  │
│  │  │   └───────────────┘ │  │   └───────────────┘ │  │   └───────────────┘ │                     │  │
│  │  └─────────────────────┘  └─────────────────────┘  └─────────────────────┘                     │  │
│  │                                                                                                 │  │
│  └─────────────────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                                       │
└───────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Key Value Propositions

### 1. **Unified Endpoint (PLINK Reduction)**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         WITHOUT KV STORE SERVICE                            │
│                                                                             │
│   Inference Node ──PLINK──► Storage Account A (Customer 1)                  │
│   Inference Node ──PLINK──► Storage Account B (Customer 2)                  │
│   Inference Node ──PLINK──► Storage Account C (Customer 3)                  │
│   Inference Node ──PLINK──► Storage Account D (Customer 4)                  │
│                     ...                                                     │
│   Inference Node ──PLINK──► Storage Account N (Customer N)                  │
│                                                                             │
│   ⚠️  N PLINKs required = Complex management, high cost                     │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                          WITH KV STORE SERVICE                              │
│                                                                             │
│   Inference Node ─────┐                                                     │
│   Inference Node ─────┼── 1 PLINK ──► KV Store ──► Storage Account A        │
│   Inference Node ─────┤   Service      Service     Storage Account B        │
│   Inference Node ─────┤                            Storage Account C        │
│   Inference Node ─────┘                            Storage Account D        │
│                                                    Storage Account N        │
│                                                                             │
│   ✅  1 PLINK required = Simple management, low cost                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2. **Account Resolution Flow**

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                              ACCOUNT RESOLUTION FLOW                                   │
│                                                                                        │
│  ┌──────────────┐      resource_name       ┌─────────────────────┐                     │
│  │   KVClient   │ ──────────────────────► │   KVStoreServer     │                     │
│  │              │   "mystorageaccount"     │                     │                     │
│  └──────────────┘                          │  ┌─────────────────────────────────────┐  │
│                                            │  │        IAccountResolver             │  │
│                                            │  │                                     │  │
│                                            │  │  ┌─────────────────────────────┐    │  │
│                                            │  │  │  InMemoryAccountResolver    │    │  │
│                                            │  │  │                             │    │  │
│                                            │  │  │  resource_name              │    │  │
│                                            │  │  │      +                      │    │  │
│                                            │  │  │  ".blob.core.windows.net"   │    │  │
│                                            │  │  │      =                      │    │  │
│                                            │  │  │  Full Account URL           │    │  │
│                                            │  │  └─────────────────────────────┘    │  │
│                                            │  │                                     │  │
│                                            │  │  ┌─────────────────────────────┐    │  │
│                                            │  │  │  DatabaseAccountResolver    │    │  │
│                                            │  │  │  (FUTURE)                   │    │  │
│                                            │  │  │                             │    │  │
│                                            │  │  │  • Customer account lookup  │    │  │
│                                            │  │  │  • Cross-region routing     │    │  │
│                                            │  │  │  • Access control           │    │  │
│                                            │  │  └─────────────────────────────┘    │  │
│                                            │  └─────────────────────────────────────┘  │
│                                            │                                           │
│                                            │  Returns: AzureStorageKVStoreLibV2        │
│                                            │          (connected to resolved account)  │
│                                            └───────────────────────────────────────────┘
│                                                                                        │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

### 3. **Cross-Region Cache Offload (Future)**

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                         CROSS-REGION CACHE OFFLOAD (FUTURE)                             │
│                                                                                         │
│  ┌─────────────────────────────────────┐    ┌─────────────────────────────────────┐     │
│  │           REGION: WEST US           │    │          REGION: EAST US            │     │
│  │                                     │    │                                     │     │
│  │  ┌───────────────────────────────┐  │    │  ┌───────────────────────────────┐  │     │
│  │  │     KV Store Service          │  │    │  │     KV Store Service          │  │     │
│  │  │                               │  │    │  │                               │  │     │
│  │  │  ┌─────────────────────────┐  │◄─┼────┼─►│  ┌─────────────────────────┐  │  │     │
│  │  │  │    Cross-Region Cache   │  │  │    │  │  │    Cross-Region Cache   │  │  │     │
│  │  │  │      Replicator         │  │  │    │  │  │      Replicator         │  │  │     │
│  │  │  └─────────────────────────┘  │  │    │  │  └─────────────────────────┘  │  │     │
│  │  │                               │  │    │  │                               │  │     │
│  │  └───────────────┬───────────────┘  │    │  └───────────────┬───────────────┘  │     │
│  │                  │                  │    │                  │                  │     │
│  │                  ▼                  │    │                  ▼                  │     │
│  │  ┌───────────────────────────────┐  │    │  ┌───────────────────────────────┐  │     │
│  │  │   Local Storage Accounts      │  │    │  │   Local Storage Accounts      │  │     │
│  │  │   (Low latency access)        │  │    │  │   (Low latency access)        │  │     │
│  │  └───────────────────────────────┘  │    │  └───────────────────────────────┘  │     │
│  │                                     │    │                                     │     │
│  │  ┌───────────────────────────────┐  │    │  ┌───────────────────────────────┐  │     │
│  │  │   Inference Nodes             │  │    │  │   Inference Nodes             │  │     │
│  │  └───────────────────────────────┘  │    │  └───────────────────────────────┘  │     │
│  │                                     │    │                                     │     │
│  └─────────────────────────────────────┘    └─────────────────────────────────────┘     │
│                                                                                         │
│  Benefits:                                                                              │
│  • Cache warming across regions                                                         │
│  • Reduced cold start latency for multi-region deployments                              │
│  • Global cache coherence for shared prompt patterns                                    │
│                                                                                         │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Component Architecture

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                              KV STORE SERVER COMPONENTS                                 │
│                                                                                         │
│  ┌───────────────────────────────────────────────────────────────────────────────────┐  │
│  │                                    server.cpp                                     │  │
│  │                                                                                   │  │
│  │   Command Line Options:                                                           │  │
│  │   --port 8085                     Listen port                                     │  │
│  │   --host 0.0.0.0                  Bind address                                    │  │
│  │   --threads N                     Worker threads (auto-detect)                    │  │
│  │   --blob-dns-suffix .blob...      Account URL suffix                              │  │
│  │   --log-level error               Logging verbosity                               │  │
│  │   --transport libcurl             HTTP transport (winhttp|libcurl)                │  │
│  │   --enable-multi-nic              Multi-NIC round-robin                           │  │
│  │                                                                                   │  │
│  └───────────────────────────────────────────────────────────────────────────────────┘  │
│                                           │                                             │
│                                           ▼                                             │
│  ┌───────────────────────────────────────────────────────────────────────────────────┐  │
│  │                            KVStoreServiceImpl                                     │  │
│  │                                                                                   │  │
│  │   ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐                   │  │
│  │   │ IAccountResolver │  │  gRPC Reactors  │  │   Store Cache   │                   │  │
│  │   │                 │  │                 │  │                 │                   │  │
│  │   │ ResolveStore()  │  │ LookupReactor   │  │ resource_name → │                   │  │
│  │   │                 │  │ ReadReactor     │  │ KVStoreLib ptr  │                   │  │
│  │   │                 │  │ WriteReactor    │  │                 │                   │  │
│  │   │                 │  │ StreamingRead   │  │                 │                   │  │
│  │   └─────────────────┘  └─────────────────┘  └─────────────────┘                   │  │
│  │                                                                                   │  │
│  └───────────────────────────────────────────────────────────────────────────────────┘  │
│                                           │                                             │
│                                           ▼                                             │
│  ┌───────────────────────────────────────────────────────────────────────────────────┐  │
│  │                        AzureStorageKVStoreLibV2                                   │  │
│  │                                                                                   │  │
│  │   ┌─────────────────────────────────────────────────────────────────────────────┐ │  │
│  │   │                           Core Operations                                   │ │  │
│  │   │                                                                             │ │  │
│  │   │   Lookup()         Find cached token blocks using Bloom filter              │ │  │
│  │   │   ReadAsync()      Retrieve chunk by blob location                          │ │  │
│  │   │   WriteAsync()     Store new chunk to blob storage                          │ │  │
│  │   │   StreamingRead()  Batch read with bidirectional streaming                  │ │  │
│  │   │                                                                             │ │  │
│  │   └─────────────────────────────────────────────────────────────────────────────┘ │  │
│  │                                                                                   │  │
│  │   ┌─────────────────────────────────────────────────────────────────────────────┐ │  │
│  │   │                         Performance Features                                │ │  │
│  │   │                                                                             │ │  │
│  │   │   • Bloom Filter      O(1) cache miss detection                             │ │  │
│  │   │   • 128-Token Blocks  Optimal chunk size for GPT                            │ │  │
│  │   │   • Connection Pool   100 concurrent HTTP connections                       │ │  │
│  │   │   • DNS Caching       300s TTL reduces DNS lookups                          │ │  │
│  │   │   • SSL Session       Reuse for faster TLS handshakes                       │ │  │
│  │   │                                                                             │ │  │
│  │   └─────────────────────────────────────────────────────────────────────────────┘ │  │
│  │                                                                                   │  │
│  └───────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                         │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## gRPC Protocol

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                  gRPC SERVICE DEFINITION                                │
│                                                                                         │
│   service KVStoreService {                                                              │
│                                                                                         │
│     // Unary RPCs                                                                       │
│     ┌─────────────────────────────────────────────────────────────────────────────────┐ │
│     │  rpc Lookup(LookupRequest) returns (LookupResponse)                             │ │
│     │      • Find cached token blocks matching sequence                               │ │
│     │      • Returns: cached_blocks, locations[], last_hash                           │ │
│     │      • Uses Bloom filter for O(1) cache miss                                    │ │
│     └─────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                         │
│     ┌─────────────────────────────────────────────────────────────────────────────────┐ │
│     │  rpc Read(ReadRequest) returns (ReadResponse)                                   │ │
│     │      • Retrieve chunk by blob location                                          │ │
│     │      • Returns: PromptChunk (tokens, buffer, hash)                              │ │
│     │      • Typical payload: ~1.2MB per chunk                                        │ │
│     └─────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                         │
│     ┌─────────────────────────────────────────────────────────────────────────────────┐ │
│     │  rpc Write(WriteRequest) returns (WriteResponse)                                │ │
│     │      • Store new chunk to blob storage                                          │ │
│     │      • Input: PromptChunk with tokens and KV buffer                             │ │
│     │      • Creates blob with hash-based naming                                      │ │
│     └─────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                         │
│     // Bidirectional Streaming RPC                                                      │
│     ┌─────────────────────────────────────────────────────────────────────────────────┐ │
│     │  rpc StreamingRead(stream ReadRequest) returns (stream ReadResponse)            │ │
│     │      • Pipeline multiple reads with reduced round-trips                         │ │
│     │      • Client sends all requests, then receives all responses                   │ │
│     │      • Parallel storage access on server side                                   │ │
│     │      • Latency = max(individual reads) instead of sum                           │ │
│     └─────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                         │
│   }                                                                                     │
│                                                                                         │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Data Flow

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                              TYPICAL KV OFFLOAD FLOW                                    │
│                                                                                         │
│   ┌───────────────────┐                                                                 │
│   │  1. MODEL START   │  LLM receives prompt tokens for inference                       │
│   │     [tokens...]   │                                                                 │
│   └─────────┬─────────┘                                                                 │
│             │                                                                           │
│             ▼                                                                           │
│   ┌───────────────────┐                                                                 │
│   │  2. LOOKUP        │  KVClient.Lookup(resource_name, partition, tokens)              │
│   │                   │  → gRPC → Server → Bloom Filter → Storage                       │
│   │                   │  ← Returns: cached_blocks=5, locations=[loc1..loc5]             │
│   └─────────┬─────────┘                                                                 │
│             │                                                                           │
│             ▼                                                                           │
│   ┌───────────────────┐                                                                 │
│   │  3. STREAMING     │  KVClient.StreamingRead([loc1, loc2, loc3, loc4, loc5])         │
│   │     READ          │  → gRPC bidi stream → Server → Parallel blob reads              │
│   │                   │  ← Returns: [chunk1, chunk2, chunk3, chunk4, chunk5]            │
│   └─────────┬─────────┘                                                                 │
│             │                                                                           │
│             ▼                                                                           │
│   ┌───────────────────┐                                                                 │
│   │  4. KV RESTORE    │  Model restores KV cache from chunks (skip recomputation)       │
│   │                   │  → 5 blocks × 128 tokens = 640 tokens restored                  │
│   │                   │  → Inference starts from token 641                              │
│   └─────────┬─────────┘                                                                 │
│             │                                                                           │
│             ▼                                                                           │
│   ┌───────────────────┐                                                                 │
│   │  5. INFERENCE     │  Model generates completion tokens                              │
│   │     [new tokens]  │                                                                 │
│   └─────────┬─────────┘                                                                 │
│             │                                                                           │
│             ▼                                                                           │
│   ┌───────────────────┐                                                                 │
│   │  6. WRITE         │  KVClient.Write(new_chunk) for each new 128-token block         │
│   │                   │  → gRPC → Server → Blob upload                                  │
│   │                   │  → Future lookups can reuse this cached data                    │
│   └───────────────────┘                                                                 │
│                                                                                         │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Performance Optimizations

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                             PERFORMANCE ARCHITECTURE                                    │
│                                                                                         │
│  ┌─────────────────────────────────────────────────────────────────────────────────────┐│
│  │                              NUMA-AWARE DEPLOYMENT                                  ││
│  │                                                                                     ││
│  │   Azure FX96mds_v2 (96 cores, 2 NUMA nodes)                                        ││
│  │                                                                                     ││
│  │   ┌─────────────────────────────┐    ┌─────────────────────────────┐               ││
│  │   │       NUMA Node 0          │    │       NUMA Node 1          │               ││
│  │   │         48 cores           │    │         48 cores           │               ││
│  │   │                            │    │                            │               ││
│  │   │   KVStoreServer :8085      │    │   KVStoreServer :8086      │               ││
│  │   │   start /NODE 0            │    │   start /NODE 1            │               ││
│  │   │                            │    │                            │               ││
│  │   │   • Local memory access    │    │   • Local memory access    │               ││
│  │   │   • No cross-NUMA penalty  │    │   • No cross-NUMA penalty  │               ││
│  │   └─────────────────────────────┘    └─────────────────────────────┘               ││
│  │                                                                                     ││
│  │   Result: 2× throughput, consistent low latency                                    ││
│  │                                                                                     ││
│  └─────────────────────────────────────────────────────────────────────────────────────┘│
│                                                                                         │
│  ┌─────────────────────────────────────────────────────────────────────────────────────┐│
│  │                              GRPC OPTIMIZATIONS                                     ││
│  │                                                                                     ││
│  │   TCP_NODELAY=1              Disable Nagle's algorithm (no 40ms batching)          ││
│  │   KEEPALIVE_TIME=10s         Maintain warm connections                              ││
│  │   MAX_CONCURRENT_STREAMS=200 High parallelism per connection                        ││
│  │   MAX_FRAME_SIZE=16MB        Reduce framing overhead for 1.2MB payloads            ││
│  │   STREAM_WINDOW=64MB         Large flow control window                              ││
│  │                                                                                     ││
│  └─────────────────────────────────────────────────────────────────────────────────────┘│
│                                                                                         │
│  ┌─────────────────────────────────────────────────────────────────────────────────────┐│
│  │                            MULTI-NIC ROUND ROBIN                                    ││
│  │                                                                                     ││
│  │   ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐                                  ││
│  │   │NIC0 │ │NIC1 │ │NIC2 │ │NIC3 │ │NIC4 │ │NIC5 │                                  ││
│  │   └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘                                  ││
│  │      │       │       │       │       │       │                                      ││
│  │      └───────┴───────┴───┬───┴───────┴───────┘                                      ││
│  │                          │                                                          ││
│  │                          ▼                                                          ││
│  │                  Round-robin selection                                              ││
│  │                  (Custom Azure SDK fork)                                            ││
│  │                                                                                     ││
│  │   Result: 6× aggregate bandwidth to storage                                         ││
│  │                                                                                     ││
│  └─────────────────────────────────────────────────────────────────────────────────────┘│
│                                                                                         │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Deployment Topology

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                              PRODUCTION DEPLOYMENT                                      │
│                                                                                         │
│  ┌─────────────────────────────────────────────────────────────────────────────────────┐│
│  │                           CUSTOMER VNET                                             ││
│  │                                                                                     ││
│  │   ┌───────────────────────────────────────────────────────────────────────────┐    ││
│  │   │                    KV Store Subnet (10.0.1.0/24)                          │    ││
│  │   │                                                                           │    ││
│  │   │   ┌─────────────────────────────────────────────────────────────────┐     │    ││
│  │   │   │              Windows VM (FX96mds_v2)                            │     │    ││
│  │   │   │              Private IP: 10.0.1.10                              │     │    ││
│  │   │   │                                                                 │     │    ││
│  │   │   │   NSG Rules:                                                    │     │    ││
│  │   │   │   • Inbound TCP 8085: Allow from Inference Subnet               │     │    ││
│  │   │   │   • Inbound TCP 8086: Allow from Inference Subnet               │     │    ││
│  │   │   │   • Outbound HTTPS 443: Allow to Storage Accounts               │     │    ││
│  │   │   │                                                                 │     │    ││
│  │   │   │   Running:                                                      │     │    ││
│  │   │   │   • KVStoreServer.exe :8085 (NUMA 0)                           │     │    ││
│  │   │   │   • KVStoreServer.exe :8086 (NUMA 1)                           │     │    ││
│  │   │   │                                                                 │     │    ││
│  │   │   └─────────────────────────────────────────────────────────────────┘     │    ││
│  │   │                                                                           │    ││
│  │   └───────────────────────────────────────────────────────────────────────────┘    ││
│  │                                         │                                          ││
│  │                                         │ Private Endpoint                         ││
│  │                                         ▼                                          ││
│  │   ┌───────────────────────────────────────────────────────────────────────────┐    ││
│  │   │                    Storage Accounts (Private Endpoints)                   │    ││
│  │   │                                                                           │    ││
│  │   │   storageacct1.privatelink.blob.core.windows.net → 10.0.2.10              │    ││
│  │   │   storageacct2.privatelink.blob.core.windows.net → 10.0.2.11              │    ││
│  │   │   storageacct3.privatelink.blob.core.windows.net → 10.0.2.12              │    ││
│  │   │                                                                           │    ││
│  │   └───────────────────────────────────────────────────────────────────────────┘    ││
│  │                                                                                     ││
│  └─────────────────────────────────────────────────────────────────────────────────────┘│
│                                         ▲                                               │
│                                         │ Private Link                                  │
│                                         │                                               │
│  ┌─────────────────────────────────────────────────────────────────────────────────────┐│
│  │                           MODEL PROVIDER VNET                                       ││
│  │                                                                                     ││
│  │   ┌───────────────────────────────────────────────────────────────────────────┐    ││
│  │   │                    Inference Subnet (10.1.0.0/24)                         │    ││
│  │   │                                                                           │    ││
│  │   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐      │    ││
│  │   │   │  GPU Node 1 │  │  GPU Node 2 │  │  GPU Node 3 │  │  GPU Node N │      │    ││
│  │   │   │             │  │             │  │             │  │             │      │    ││
│  │   │   │ KVSTORE_GRPC│  │ KVSTORE_GRPC│  │ KVSTORE_GRPC│  │ KVSTORE_GRPC│      │    ││
│  │   │   │ _SERVER=    │  │ _SERVER=    │  │ _SERVER=    │  │ _SERVER=    │      │    ││
│  │   │   │ 10.0.1.10:  │  │ 10.0.1.10:  │  │ 10.0.1.10:  │  │ 10.0.1.10:  │      │    ││
│  │   │   │ 8085        │  │ 8086        │  │ 8085        │  │ 8086        │      │    ││
│  │   │   └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘      │    ││
│  │   │                                                                           │    ││
│  │   └───────────────────────────────────────────────────────────────────────────┘    ││
│  │                                                                                     ││
│  └─────────────────────────────────────────────────────────────────────────────────────┘│
│                                                                                         │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Summary

| Feature | Description |
|---------|-------------|
| **Unified Endpoint** | Single gRPC service handles multiple storage accounts - reduces PLINK complexity |
| **Account Resolution** | `IAccountResolver` interface resolves resource names to storage connections |
| **High Performance** | NUMA-aware, multi-NIC, async gRPC with streaming |
| **Scalability** | Multi-process architecture for full CPU utilization |
| **Cross-Region (Future)** | Cache replication across regions for global deployments |
| **Protocol** | gRPC/HTTP2 with optimized settings for large payloads |

---

*Last updated: December 2024*
