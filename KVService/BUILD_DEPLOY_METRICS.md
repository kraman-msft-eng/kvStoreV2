# Building and Deploying with OpenTelemetry Metrics

## Prerequisites

- vcpkg installed at `C:\Users\kraman\source\vcpkg`
- Visual Studio 2022 or CMake + MSVC build tools
- Azure Application Insights resource created

## Build Instructions

### Step 1: Clean Previous Build (Optional)

```powershell
cd C:\Users\kraman\source\KVStoreV2\KVService\build
Remove-Item * -Recurse -Force
```

### Step 2: Install OpenTelemetry Dependencies

```powershell
cd C:\Users\kraman\source\KVStoreV2\KVService\build

# Install OpenTelemetry and its dependencies
vcpkg install opentelemetry-cpp:x64-windows-static

# Verify installation
vcpkg list | Select-String "opentelemetry"
```

Expected output:
```
opentelemetry-cpp:x64-windows-static
```

### Step 3: Configure CMake

```powershell
cd C:\Users\kraman\source\KVStoreV2\KVService

cmake -B build -S . `
    -DCMAKE_TOOLCHAIN_FILE=C:/Users/kraman/source/vcpkg/scripts/buildsystems/vcpkg.cmake `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static `
    -DCMAKE_BUILD_TYPE=Release
```

### Step 4: Build

```powershell
cmake --build build --config Release -j 8
```

Expected output:
```
[100%] Linking CXX executable KVStoreServer.exe
[100%] Built target KVStoreServer
```

### Step 5: Verify Binary

```powershell
Test-Path .\build\Release\KVStoreServer.exe
Get-Item .\build\Release\KVStoreServer.exe | Select-Object Name, Length, LastWriteTime
```

## Getting Azure Application Insights Credentials

### Option 1: Azure Portal UI

1. Navigate to [Azure Portal](https://portal.azure.com)
2. Go to **Application Insights** resource
3. From **Overview** page:
   - Copy **Instrumentation Key**
   - Note **Connection String** (contains endpoint)

### Option 2: Azure CLI

```powershell
# List Application Insights resources
az monitor app-insights component list --resource-group kraman_rg

# Get instrumentation key
$appInsightsName = "your-app-insights-name"
$resourceGroup = "kraman_rg"

$key = az monitor app-insights component show `
    --app $appInsightsName `
    --resource-group $resourceGroup `
    --query instrumentationKey `
    --output tsv

Write-Host "Instrumentation Key: $key"

# Get connection string (contains endpoint)
$connString = az monitor app-insights component show `
    --app $appInsightsName `
    --resource-group $resourceGroup `
    --query connectionString `
    --output tsv

Write-Host "Connection String: $connString"
```

### Extracting Endpoint from Connection String

Connection string format: `InstrumentationKey=xxx;IngestionEndpoint=https://...;LiveEndpoint=https://...`

```powershell
# Parse endpoint from connection string
$connString = "InstrumentationKey=abc;IngestionEndpoint=https://eastus-8.in.applicationinsights.azure.com/;..."
$endpoint = ($connString -split ';' | Where-Object { $_ -like 'IngestionEndpoint=*' }) -replace 'IngestionEndpoint=',''
$endpoint = $endpoint.TrimEnd('/') + '/v1/metrics'

Write-Host "OTLP Metrics Endpoint: $endpoint"
```

## Starting the Server

### Development (with all logging)

```powershell
cd C:\Users\kraman\source\KVStoreV2\KVService

# Set your credentials
$endpoint = "https://eastus-8.in.applicationinsights.azure.com/v1/metrics"
$key = "your-instrumentation-key-here"

.\build\Release\KVStoreServer.exe `
    --port 8085 `
    --log-level verbose `
    --metrics-endpoint $endpoint `
    --instrumentation-key $key
```

### Production (minimal logging)

```powershell
.\build\Release\KVStoreServer.exe `
    --port 8085 `
    --log-level error `
    --disable-sdk-logging `
    --metrics-endpoint $endpoint `
    --instrumentation-key $key
```

### Without Metrics (backward compatible)

```powershell
# If you don't provide metrics args, JSON stdout still works
.\build\Release\KVStoreServer.exe --port 8085
```

## Deploying to Azure VM

### Copy Binary to Windows Server

```powershell
# From dev machine
$vmIP = "172.179.242.186"
$username = "azureuser"

# Copy executable
scp .\build\Release\KVStoreServer.exe ${username}@${vmIP}:~/kvstore/

# Copy any updated config files if needed
```

### Start Server on Azure VM

```powershell
# SSH to VM
ssh ${username}@${vmIP}

# Navigate to directory
cd ~/kvstore

# Set credentials from environment or config
export METRICS_ENDPOINT="https://eastus-8.in.applicationinsights.azure.com/v1/metrics"
export INSTRUMENTATION_KEY="your-key-here"

# Start server
./KVStoreServer \
    --port 8085 \
    --metrics-endpoint "$METRICS_ENDPOINT" \
    --instrumentation-key "$INSTRUMENTATION_KEY" \
    > server.log 2>&1 &

# Check it's running
ps aux | grep KVStoreServer
```

### Using Windows Service (Production)

See existing service setup or create a wrapper script:

```powershell
# Create run_with_metrics.ps1 on VM
$endpoint = "https://eastus-8.in.applicationinsights.azure.com/v1/metrics"
$key = Get-Content "C:\kvstore\instrumentation_key.txt"

& "C:\kvstore\KVStoreServer.exe" `
    --port 8085 `
    --log-level error `
    --disable-sdk-logging `
    --metrics-endpoint $endpoint `
    --instrumentation-key $key
```

## Verifying Metrics Flow

### 1. Check Server Logs

Look for initialization message:
```
Initializing OpenTelemetry metrics...
[MetricsHelper] Initialized with endpoint: https://eastus-8.in.applicationinsights.azure.com/v1/metrics
Server listening on: 0.0.0.0:8085
```

### 2. Make Test Requests

```powershell
# From client machine - trigger some traffic
.\runLocal.ps1
```

### 3. Query Azure Monitor (after 10-15 seconds)

```kql
customMetrics
| where timestamp > ago(5m)
| where name startswith "kvstore."
| summarize count() by name
```

Expected output:
```
kvstore.storage.latency    42
kvstore.rpc.latency        42
kvstore.rpc.overhead       42
kvstore.rpc.requests       42
```

### 4. Check Specific Metrics

```kql
customMetrics
| where name == "kvstore.rpc.latency"
| where timestamp > ago(5m)
| extend method = tostring(customDimensions.method)
| summarize avg(value), min(value), max(value) by method
```

## Troubleshooting Build Issues

### OpenTelemetry Not Found

```powershell
# Ensure vcpkg is up to date
cd C:\Users\kraman\source\vcpkg
git pull
.\bootstrap-vcpkg.bat

# Reinstall OpenTelemetry
vcpkg remove opentelemetry-cpp:x64-windows-static
vcpkg install opentelemetry-cpp:x64-windows-static
```

### Linking Errors

```powershell
# Clean and rebuild
cd C:\Users\kraman\source\KVStoreV2\KVService\build
Remove-Item * -Recurse -Force

# Reconfigure
cmake -B . -S .. `
    -DCMAKE_TOOLCHAIN_FILE=C:/Users/kraman/source/vcpkg/scripts/buildsystems/vcpkg.cmake `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static `
    -DCMAKE_BUILD_TYPE=Release

# Rebuild
cmake --build . --config Release -j 8
```

### Runtime Errors

```powershell
# Check DLL dependencies (should be none for static build)
dumpbin /dependents .\build\Release\KVStoreServer.exe

# Test without metrics first
.\build\Release\KVStoreServer.exe --port 8085

# Then add metrics
.\build\Release\KVStoreServer.exe --port 8085 --metrics-endpoint "..." --instrumentation-key "..."
```

## Performance Validation

### Check Overhead

Run load test and compare metrics:

```kql
customMetrics
| where name == "kvstore.rpc.overhead"
| where timestamp > ago(5m)
| extend method = tostring(customDimensions.method)
| summarize 
    avg_overhead = avg(value),
    p95_overhead = percentile(value, 95),
    p99_overhead = percentile(value, 99)
    by method
```

Expected overhead: **< 1ms** (most of it is gRPC serialization, not metrics)

### Compare Storage vs Total Latency

```kql
let storage = customMetrics
    | where name == "kvstore.storage.latency"
    | extend method = tostring(customDimensions.method)
    | summarize avg_storage = avg(value) by method;
let total = customMetrics
    | where name == "kvstore.rpc.latency"
    | extend method = tostring(customDimensions.method)
    | summarize avg_total = avg(value) by method;
storage
| join kind=inner total on method
| extend overhead_pct = (avg_total - avg_storage) / avg_total * 100
| project method, avg_storage, avg_total, overhead_pct
```

## Rollback Plan

If metrics cause issues, simply restart without the metrics arguments:

```powershell
# Stop current server
Get-Process KVStoreServer | Stop-Process -Force

# Start without metrics
.\build\Release\KVStoreServer.exe --port 8085
```

JSON stdout metrics will still work for backward compatibility.

## Next Steps

1. ✅ Build server with OpenTelemetry
2. ✅ Get Application Insights credentials
3. ✅ Deploy to Azure VM
4. ✅ Verify metrics flow
5. 📊 Create Azure Monitor dashboards
6. 🔔 Set up alerts for latency and errors
7. 📈 Monitor production performance

For dashboard and alert setup, see **OPENTELEMETRY_METRICS.md**.
