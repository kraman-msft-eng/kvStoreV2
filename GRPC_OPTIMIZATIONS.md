# gRPC Performance Optimizations

This document describes the gRPC channel arguments and optimizations configured for low-latency communication between the Linux client and Windows server VMs.

## Overview

The KVStore system uses gRPC for RPC communication with large payloads (~1.2MB per cache block). Both client and server are configured with matching optimizations to minimize latency and maximize throughput.

## Configuration Summary

| Setting | Client | Server | Purpose |
|---------|--------|--------|---------|
| TCP_NODELAY | ✓ | ✓ | Disable Nagle's algorithm |
| TCP_USER_TIMEOUT | 20s | 20s | Detect dead connections |
| KEEPALIVE_TIME | 10s | 10s | Keep connections warm |
| MAX_CONCURRENT_STREAMS | 200 | 200 | High concurrency support |
| HTTP2_MAX_FRAME_SIZE | 16MB | 16MB | Reduce frame overhead |
| HTTP2_BDP_PROBE | ✓ | ✓ | Optimize flow control |

---

## TCP Optimizations

### TCP_NODELAY (Nagle's Algorithm)
```cpp
args.SetInt("grpc.tcp_nodelay", 1);
```

**What it does:** Disables Nagle's algorithm, which normally buffers small packets for up to 40ms before sending.

**Why we need it:** Without this, small RPC messages (like Lookup requests ~100 bytes) would be delayed waiting to batch with other data. This is critical for latency-sensitive applications.

**Impact:** Reduces latency by up to 40ms for small messages.

### TCP_USER_TIMEOUT
```cpp
args.SetInt("grpc.tcp_user_timeout_ms", 20000);  // 20 seconds
```

**What it does:** Sets the maximum time TCP will wait for an ACK before considering the connection dead.

**Why we need it:** Helps detect failed connections faster than the default Linux TCP timeout (~15 minutes). Prevents hanging RPCs on network issues.

---

## Keepalive Settings

### GRPC_ARG_KEEPALIVE_TIME_MS
```cpp
args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 10000);  // 10 seconds
```

**What it does:** Sends HTTP/2 PING frames every 10 seconds to keep the connection alive.

**Why we need it:** 
- Prevents idle connections from being closed by intermediate firewalls/NATs
- Keeps TCP connection "warm" (avoids slow-start after idle periods)
- Detects dead peers proactively

### GRPC_ARG_KEEPALIVE_TIMEOUT_MS
```cpp
args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000);  // 5 seconds
```

**What it does:** Time to wait for a PING ACK before considering the connection dead.

### GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS
```cpp
args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
```

**What it does:** Allows keepalive pings even when no RPCs are active.

**Why we need it:** Maintains connection health during idle periods between batches of requests.

### Server-Only: Ping Rate Limiting
```cpp
// Server accepts client pings with minimum 5s interval
builder.AddChannelArgument(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 5000);
builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);  // Unlimited
```

**What it does:** Allows the server to accept frequent PING frames from clients without triggering abuse protection.

---

## HTTP/2 Flow Control

### GRPC_ARG_HTTP2_MAX_FRAME_SIZE
```cpp
args.SetInt(GRPC_ARG_HTTP2_MAX_FRAME_SIZE, 16 * 1024 * 1024);  // 16MB
```

**What it does:** Sets the maximum HTTP/2 DATA frame size (default is 16KB).

**Why we need it:** Our payloads are ~1.2MB. With 16KB frames, that's 75 frames per message. With 16MB frames, it's 1 frame. This reduces:
- Syscall overhead (75 → 1)
- Frame header overhead
- CPU time parsing frames

**Note:** HTTP/2 spec maximum is 16,777,215 bytes (16MB - 1 byte).

### GRPC_ARG_HTTP2_BDP_PROBE
```cpp
args.SetInt(GRPC_ARG_HTTP2_BDP_PROBE, 1);
```

**What it does:** Enables Bandwidth-Delay Product probing. gRPC periodically estimates the network's BDP and adjusts flow control windows accordingly.

**Why we need it:** Automatically optimizes flow control for the actual network conditions between VMs.

### GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES
```cpp
args.SetInt(GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES, 64 * 1024 * 1024);  // 64MB
```

**What it does:** Sets the per-stream flow control window size.

**Why we need it:** Large window allows the sender to transmit multiple 1.2MB messages without waiting for window updates. Prevents flow control stalls.

### GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE
```cpp
args.SetInt(GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE, 8 * 1024 * 1024);  // 8MB
```

**What it does:** Size of the write buffer for outgoing data.

**Why we need it:** Allows buffering multiple large messages for efficient batch transmission.

---

## Concurrency Settings

### GRPC_ARG_MAX_CONCURRENT_STREAMS
```cpp
args.SetInt(GRPC_ARG_MAX_CONCURRENT_STREAMS, 200);
```

**What it does:** Maximum number of concurrent RPC streams per HTTP/2 connection.

**Why we need it:** 
- Default is ~100, which can be limiting under high concurrency
- We run stress tests with 10+ concurrent clients, each making parallel requests
- Higher limit provides headroom for burst traffic

---

## Message Size Limits (Client Only)

```cpp
args.SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, 100 * 1024 * 1024);  // 100MB
args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 100 * 1024 * 1024);  // 100MB
```

**What it does:** Sets maximum allowed message size for send and receive.

**Why we need it:** Our Write operations send ~1.2MB payloads. Default limit (4MB) is sufficient, but we increase to 100MB for future expansion headroom.

---

## Azure Storage Optimizations (Server Side)

### Connection Pool Size
```cpp
curlOptions.MaxConnectionsCache = 100;  // Allow 100 concurrent connections
```

**What it does:** Sets the maximum number of HTTP connections to Azure Storage that libcurl will keep in its connection pool.

**Why we need it:** Streaming read spawns parallel threads that each make Azure Storage requests. With `MaxConnectionsCache = 1` (previous bug), all parallel reads were serialized on one connection. With 100, reads actually run in parallel.

**Impact:** Reduced streaming read latency from ~50ms (serialized) to ~18ms (parallel).

### Other Azure SDK Settings
```cpp
curlOptions.HttpKeepAlive = true;           // Reuse connections
curlOptions.DnsCacheTimeout = 300;          // Cache DNS for 5 minutes
curlOptions.EnableCurlSslCaching = true;    // Cache SSL sessions
curlOptions.NoSignal = true;                // Thread-safe mode
curlOptions.ConnectionTimeout = 3000ms;     // 3s connection timeout
```

---

## Environment Variables

### GRPC_VERBOSITY
```bash
export GRPC_VERBOSITY=error  # Reduce gRPC logging noise
```

Set in `run_azure_linux.ps1` to suppress verbose gRPC internal logs.

---

## Windows TCP Tuning (Server VM)

Run these commands on the Windows Server VM to optimize TCP performance:

```powershell
# Enable TCP auto-tuning (allows dynamic receive window scaling)
netsh int tcp set global autotuninglevel=normal

# Use Compound TCP (CTCP) congestion provider for better throughput
netsh int tcp set supplemental internet congestionprovider=ctcp
```

### TCP Auto-Tuning Level
**What it does:** Controls the TCP receive window auto-tuning behavior.

| Level | Description |
|-------|-------------|
| `disabled` | Fixed receive window (legacy, poor performance) |
| `highlyrestricted` | Very conservative scaling |
| `restricted` | Conservative scaling |
| `normal` | **Recommended** - Dynamic scaling based on BDP |
| `experimental` | Aggressive scaling |

**Why `normal`:** Allows Windows to dynamically adjust the TCP receive window based on network conditions, maximizing throughput for large transfers like our 1.2MB cache blocks.

### Compound TCP (CTCP)
**What it does:** Uses a more aggressive congestion control algorithm that combines delay-based and loss-based signals.

**Why CTCP:** Better performance on high-bandwidth, high-latency networks. Increases throughput by being more aggressive in probing for available bandwidth while still being responsive to congestion.

**Alternative:** `cubic` (Linux default) - also good for high-BDP networks.

### Verify Settings
```powershell
# Check current TCP settings
netsh int tcp show global

# Check congestion provider
netsh int tcp show supplemental
```

---

## Linux TCP Tuning (Client VM)

Run these commands on the Linux Client VM to optimize TCP buffer sizes for high-throughput transfers:

```bash
# Increase maximum receive buffer size (64MB)
sudo sysctl -w net.core.rmem_max=67108864

# Increase maximum send buffer size (64MB)
sudo sysctl -w net.core.wmem_max=67108864

# TCP receive buffer: min=4KB, default=87KB, max=64MB
sudo sysctl -w net.ipv4.tcp_rmem="4096 87380 67108864"

# TCP send buffer: min=4KB, default=64KB, max=64MB
sudo sysctl -w net.ipv4.tcp_wmem="4096 65536 67108864"
```

### Make Persistent (survives reboot)
Add to `/etc/sysctl.conf`:
```bash
net.core.rmem_max=67108864
net.core.wmem_max=67108864
net.ipv4.tcp_rmem=4096 87380 67108864
net.ipv4.tcp_wmem=4096 65536 67108864
```

Then apply: `sudo sysctl -p`

### Buffer Size Explanation

| Setting | Values | Purpose |
|---------|--------|---------|
| `rmem_max` | 64MB | Maximum receive buffer any socket can request |
| `wmem_max` | 64MB | Maximum send buffer any socket can request |
| `tcp_rmem` | min=4KB, default=87KB, max=64MB | Per-socket receive buffer (auto-tuned) |
| `tcp_wmem` | min=4KB, default=64KB, max=64MB | Per-socket send buffer (auto-tuned) |

**Why 64MB?** With high-bandwidth links (~27 Gbps between VMs) and ~1.5ms RTT:
- BDP (Bandwidth-Delay Product) = 27 Gbps × 1.5ms = ~5MB
- We set 64MB to provide headroom for multiple concurrent streams and burst traffic
- Linux auto-tunes within the min/max range based on actual network conditions

### Verify Settings
```bash
# Check current values
sysctl net.core.rmem_max net.core.wmem_max net.ipv4.tcp_rmem net.ipv4.tcp_wmem

# Check TCP memory usage
cat /proc/net/sockstat
```

---

## Performance Results

With all optimizations enabled:

| Operation | p50 Latency | p99 Latency |
|-----------|-------------|-------------|
| Lookup | 4.3ms | 21ms |
| Streaming Read (4 blocks) | 19ms | 50ms |
| Write (1.2MB) | 30ms | 140ms |

**Latency Breakdown (Streaming Read):**
- Azure Storage (max): 13ms
- Transport Delay: 6ms
- **Total Client E2E: 19ms**

---

## Troubleshooting

### High p99 Latency Spikes
- Check `Transport Delay` - if high, network congestion or gRPC queuing
- Check `Max Storage` - if high, Azure Storage throttling

### Connection Failures
- Verify KEEPALIVE settings match on both sides
- Check firewall rules allow the gRPC port (8085)
- Ensure VNet peering is healthy

### Slow First Request
- If first RPC after idle is slow, check KEEPALIVE is working
- Verify `GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS` is set

---

## References

- [gRPC Core Channel Arguments](https://grpc.github.io/grpc/core/group__grpc__arg__keys.html)
- [gRPC Performance Best Practices](https://grpc.io/docs/guides/performance/)
- [HTTP/2 Flow Control](https://httpwg.org/specs/rfc7540.html#FlowControl)
