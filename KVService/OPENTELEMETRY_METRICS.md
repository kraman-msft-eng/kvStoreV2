# OpenTelemetry Metrics Integration

## Overview

The KVStore gRPC service now includes high-performance metrics collection using **OpenTelemetry** with **Azure Monitor** integration. This provides near-real-time observability with minimal performance overhead (~50-200μs per RPC call).

## Architecture

### Performance Characteristics
- **Overhead**: ~50-200μs per metric recording (vs 5-20ms for file-based approaches)
- **Batching**: Metrics are batched and exported every 5 seconds
- **Protocol**: Binary OTLP over HTTP (efficient serialization)
- **Threading**: Async exports using lock-free ring buffers
- **Connection pooling**: Persistent HTTP connections to Azure Monitor

### Metrics Collected

| Metric Name | Type | Description | Unit | Labels |
|-------------|------|-------------|------|--------|
| `kvstore.storage.latency` | Histogram | Time spent in Azure Storage operations | milliseconds | method |
| `kvstore.rpc.latency` | Histogram | Total RPC handler latency (E2E) | milliseconds | method |
| `kvstore.rpc.overhead` | Histogram | gRPC/serialization overhead | milliseconds | method |
| `kvstore.rpc.requests` | Counter | Total number of RPC requests | requests | method, success |

**Methods tracked**: `Lookup`, `Read`, `Write`

## Setup

### 1. Azure Monitor Configuration

#### Option A: Using Application Insights

1. Create an Application Insights resource in Azure Portal
2. Get the **Instrumentation Key** from the Overview page
3. Get the **Ingestion Endpoint** (defaults to regional endpoint)

```bash
# Example endpoints:
# Global: https://dc.services.visualstudio.com/v2/track
# US: https://eastus-8.in.applicationinsights.azure.com/v2/track
```

#### Option B: Using Azure Monitor OTLP Endpoint

1. Create Application Insights resource
2. Enable OTLP ingestion (preview feature)
3. Get OTLP endpoint: `https://<ingestion-endpoint>/v1/metrics`

### 2. Starting the Server with Metrics

#### Windows (PowerShell)
```powershell
# Set your Application Insights details
$endpoint = "https://eastus-8.in.applicationinsights.azure.com/v1/metrics"
$key = "your-instrumentation-key-here"

# Start server with metrics enabled
.\build\Release\KVStoreServer.exe `
    --port 8085 `
    --metrics-endpoint $endpoint `
    --instrumentation-key $key
```

#### Linux (Bash)
```bash
# Set your Application Insights details
ENDPOINT="https://eastus-8.in.applicationinsights.azure.com/v1/metrics"
KEY="your-instrumentation-key-here"

# Start server with metrics enabled
./build/KVStoreServer \
    --port 8085 \
    --metrics-endpoint "$ENDPOINT" \
    --instrumentation-key "$KEY"
```

### 3. Verifying Metrics Flow

After starting the server with metrics enabled, you should see:
```
Initializing OpenTelemetry metrics...
[MetricsHelper] Initialized with endpoint: https://...
```

Metrics will begin flowing to Azure Monitor within 5-10 seconds.

## Querying Metrics in Azure Monitor

### Using Azure Portal

1. Navigate to your Application Insights resource
2. Go to **Logs** section
3. Use KQL queries to analyze metrics

### Example KQL Queries

#### Average Storage Latency by Method
```kql
customMetrics
| where name == "kvstore.storage.latency"
| extend method = tostring(customDimensions.method)
| summarize avg_latency_ms = avg(value) by method, bin(timestamp, 1m)
| render timechart
```

#### P95/P99 Total Latency
```kql
customMetrics
| where name == "kvstore.rpc.latency"
| extend method = tostring(customDimensions.method)
| summarize 
    p50 = percentile(value, 50),
    p95 = percentile(value, 95),
    p99 = percentile(value, 99)
    by method, bin(timestamp, 5m)
| render timechart
```

#### Request Success Rate
```kql
customMetrics
| where name == "kvstore.rpc.requests"
| extend method = tostring(customDimensions.method)
| extend success = tostring(customDimensions.success)
| summarize total = sum(value) by method, success, bin(timestamp, 1m)
| evaluate pivot(success)
| extend success_rate = todouble(true) / (todouble(true) + todouble(false)) * 100
| render timechart
```

#### Overhead Analysis (gRPC vs Storage)
```kql
customMetrics
| where name in ("kvstore.storage.latency", "kvstore.rpc.overhead")
| extend method = tostring(customDimensions.method)
| summarize avg_value = avg(value) by name, method, bin(timestamp, 1m)
| render timechart
```

#### Request Rate by Method
```kql
customMetrics
| where name == "kvstore.rpc.requests"
| extend method = tostring(customDimensions.method)
| summarize requests = sum(value) by method, bin(timestamp, 1m)
| render timechart
```

## Creating Dashboards

### Azure Portal Dashboard

1. Run queries in Application Insights Logs
2. Click **Pin to dashboard** on each chart
3. Arrange widgets for comprehensive view

### Recommended Dashboard Widgets

1. **Latency Overview**: P50/P95/P99 for all methods
2. **Storage vs Total Latency**: Compare storage and E2E times
3. **Request Volume**: Requests per minute by method
4. **Success Rate**: Percentage of successful requests
5. **Overhead**: gRPC/serialization overhead trends

### Sample Dashboard JSON

```json
{
  "queries": [
    {
      "query": "customMetrics | where name == 'kvstore.rpc.latency' | summarize percentiles(value, 50, 95, 99) by method, bin(timestamp, 5m)",
      "title": "RPC Latency Percentiles"
    },
    {
      "query": "customMetrics | where name == 'kvstore.rpc.requests' | summarize sum(value) by method, bin(timestamp, 1m)",
      "title": "Request Rate"
    }
  ]
}
```

## Alerts

### Create Latency Alert

```kql
customMetrics
| where name == "kvstore.rpc.latency"
| summarize avg(value) by bin(timestamp, 5m)
| where avg_value > 100  // Alert if avg latency > 100ms
```

### Create Error Rate Alert

```kql
customMetrics
| where name == "kvstore.rpc.requests"
| extend success = tostring(customDimensions.success)
| summarize total = sum(value) by success, bin(timestamp, 5m)
| evaluate pivot(success)
| extend error_rate = todouble(false) / (todouble(true) + todouble(false))
| where error_rate > 0.05  // Alert if error rate > 5%
```

## Troubleshooting

### No Metrics in Azure Monitor

1. **Check server logs**: Look for `[MetricsHelper] Initialized` message
2. **Verify endpoint**: Ensure OTLP endpoint URL is correct
3. **Check instrumentation key**: Verify it matches Application Insights resource
4. **Network connectivity**: Ensure server can reach Azure endpoints
5. **Wait 5-10 seconds**: Metrics are batched and exported every 5 seconds

### Metrics Not Updating

1. **Check batch interval**: Metrics export every 5 seconds
2. **Verify requests**: Ensure gRPC requests are being made
3. **Check Application Insights**: Look in Logs > customMetrics table

### High Overhead

OpenTelemetry should add <200μs overhead. If seeing higher:
1. **Check export interval**: Increase from 5s to 10s if needed
2. **Network latency**: Ensure low-latency path to Azure
3. **Disable JSON logging**: Comment out stdout in LogMetric()

### Compilation Issues

```bash
# Ensure OpenTelemetry is installed via vcpkg
cd build
vcpkg install opentelemetry-cpp

# Rebuild
cmake --build . --config Release
```

## Performance Tuning

### Adjust Export Interval

Edit `KVService/src/MetricsHelper.cpp`:
```cpp
reader_opts.export_interval_millis = std::chrono::milliseconds(10000);  // 10 seconds
```

### Disable JSON Stdout (for production)

Comment out stdout in `KVStoreServiceImpl.cpp`:
```cpp
void LogMetric(...) {
    // std::cout << "{...}" << std::endl;  // Disable for production
    
    // Keep OpenTelemetry only
    auto& metrics = MetricsHelper::GetInstance();
    metrics.RecordStorageLatency(method, storage_latency_us / 1000.0);
    // ...
}
```

### Connection Pooling

OpenTelemetry maintains persistent connections. Tune timeouts:
```cpp
exporter_opts.timeout = std::chrono::milliseconds(5000);  // 5s timeout
```

## Best Practices

1. **Always use request IDs**: Include `request-id` in gRPC metadata for E2E tracing
2. **Monitor overhead**: Track `kvstore.rpc.overhead` to detect performance regressions
3. **Set up alerts**: Create alerts for P99 latency and error rates
4. **Regular reviews**: Review dashboards weekly to identify trends
5. **Correlate metrics**: Use request IDs to correlate client and server metrics

## Cost Optimization

- **Sampling**: Not needed - metrics are histograms (already aggregated)
- **Retention**: Configure Application Insights retention (default 90 days)
- **Export interval**: Increase to 10-30s for lower ingestion costs
- **Selective metrics**: Comment out unused metrics if needed

## References

- [OpenTelemetry C++ SDK](https://github.com/open-telemetry/opentelemetry-cpp)
- [Azure Monitor OTLP Ingestion](https://learn.microsoft.com/en-us/azure/azure-monitor/app/opentelemetry-enable)
- [Application Insights KQL](https://learn.microsoft.com/en-us/azure/data-explorer/kusto/query/)
