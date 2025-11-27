# Azure Monitor Metrics Setup

## Overview
The server now outputs metrics in JSON format to stdout. This guide shows how to push these metrics to Azure Monitor.

## Option 1: Azure Monitor Agent (Recommended for Production)

### Install Azure Monitor Agent on Windows VM
```powershell
# Install the agent
az vm extension set --resource-group kraman_rg --vm-name aoaikvfd5 `
  --name AzureMonitorWindowsAgent `
  --publisher Microsoft.Azure.Monitor `
  --enable-auto-upgrade true
```

### Create Log Analytics Workspace
```powershell
az monitor log-analytics workspace create `
  --resource-group kraman_rg `
  --workspace-name kvstore-logs `
  --location westus2
```

### Configure Data Collection Rule (DCR)
1. In Azure Portal, go to **Monitor** > **Data Collection Rules**
2. Create a new DCR:
   - **Name**: kvstore-metrics-dcr
   - **Platform Type**: Windows
   - **Resources**: Select your Windows VM (aoaikvfd5)

3. Add a custom log data source:
   - **Data source type**: Custom Logs
   - **File pattern**: `C:\KVStoreServer\metrics.log`
   - **Table name**: KVStoreMetrics_CL
   - **Parse as JSON**: Yes

### Redirect Server Output to Log File
Modify your server startup to write to a log file:
```powershell
# On Windows VM
.\KVStoreServer.exe --port 8085 --log-level info --disable-sdk-logging | `
  Tee-Object -FilePath "C:\KVStoreServer\metrics.log" -Append
```

### Query Metrics in Log Analytics
```kusto
KVStoreMetrics_CL
| where type_s == "metric"
| extend 
    Method = method_s,
    RequestId = request_id_s,
    StorageLatencyMs = storage_latency_us_d / 1000.0,
    TotalLatencyMs = total_latency_us_d / 1000.0,
    OverheadMs = overhead_us_d / 1000.0,
    Success = success_b
| project TimeGenerated, Method, RequestId, StorageLatencyMs, TotalLatencyMs, OverheadMs, Success
| order by TimeGenerated desc
```

## Option 2: Application Insights (Best for Detailed Telemetry)

### Install Application Insights SDK

Add to your CMakeLists.txt:
```cmake
# Find or install Application Insights C++ SDK
find_package(microsoft-applicationinsights-cpp CONFIG REQUIRED)
target_link_libraries(KVStoreServer PRIVATE microsoft-applicationinsights-cpp::appinsights)
```

### Modify Server Code

```cpp
#include <appinsights/TelemetryClient.h>

// In server.cpp main()
auto config = std::make_shared<microsoft::applicationinsights::TelemetryConfiguration>();
config->SetInstrumentationKey("<YOUR_INSTRUMENTATION_KEY>");
microsoft::applicationinsights::TelemetryClient client(config);

// In LogMetric function
void LogMetric(const std::string& method, const std::string& request_id,
               int64_t storage_latency_us, int64_t total_latency_us,
               bool success, const std::string& error = "") {
    
    // Existing JSON logging
    std::cout << "{\"type\":\"metric\"..." << std::endl;
    
    // Also send to Application Insights
    microsoft::applicationinsights::EventTelemetry event("RPCCall");
    event.GetProperties()["method"] = method;
    event.GetProperties()["request_id"] = request_id;
    event.GetProperties()["success"] = success ? "true" : "false";
    
    microsoft::applicationinsights::MetricTelemetry metric("storage_latency", storage_latency_us / 1000.0);
    metric.GetProperties()["method"] = method;
    
    client.TrackEvent(event);
    client.TrackMetric(metric);
}
```

Get Instrumentation Key:
```powershell
az monitor app-insights component create `
  --app kvstore-insights `
  --location westus2 `
  --resource-group kraman_rg `
  --application-type web

az monitor app-insights component show `
  --app kvstore-insights `
  --resource-group kraman_rg `
  --query instrumentationKey -o tsv
```

## Option 3: Fluentd/FluentBit (Flexible, Cloud-Agnostic)

### Install Fluent Bit on Windows VM
```powershell
# Download and install Fluent Bit
Invoke-WebRequest -Uri "https://packages.fluentbit.io/windows/fluent-bit-3.0.0-win64.exe" `
  -OutFile "fluent-bit-installer.exe"
.\fluent-bit-installer.exe /S
```

### Configure Fluent Bit (fluent-bit.conf)
```ini
[SERVICE]
    Flush        5
    Daemon       Off
    Log_Level    info

[INPUT]
    Name         tail
    Path         C:/KVStoreServer/metrics.log
    Parser       json
    Tag          kvstore.metrics

[FILTER]
    Name         parser
    Match        kvstore.metrics
    Key_Name     log
    Parser       json

[OUTPUT]
    Name            azure_logs_ingestion
    Match           kvstore.metrics
    DCE_URI         https://<DCE_ENDPOINT>
    DCR_ID          <DCR_IMMUTABLE_ID>
    Table           KVStoreMetrics_CL
    Azure_Tenant_Id <TENANT_ID>
    Azure_Client_Id <CLIENT_ID>
    Azure_Client_Secret <CLIENT_SECRET>
```

## Option 4: Simple PowerShell Script (Quick Testing)

```powershell
# push_metrics.ps1
$logFile = "C:\KVStoreServer\metrics.log"
$workspaceId = "<WORKSPACE_ID>"
$sharedKey = "<SHARED_KEY>"

Get-Content $logFile -Tail 100 -Wait | ForEach-Object {
    if ($_ -match '^\{.*"type":"metric"') {
        $json = $_ | ConvertFrom-Json
        
        # Build Log Analytics JSON body
        $body = @(
            @{
                Method = $json.method
                RequestId = $json.request_id
                StorageLatencyUs = $json.storage_latency_us
                TotalLatencyUs = $json.total_latency_us
                OverheadUs = $json.overhead_us
                Success = $json.success
                Error = $json.error
                Timestamp = $json.timestamp
            }
        ) | ConvertTo-Json -Depth 3
        
        # Send to Log Analytics
        $rfc1123date = [DateTime]::UtcNow.ToString("r")
        $contentLength = $body.Length
        $xHeaders = "x-ms-date:$rfc1123date"
        $stringToHash = "POST`n$contentLength`napplication/json`n$xHeaders`n/api/logs"
        
        $bytesToHash = [Text.Encoding]::UTF8.GetBytes($stringToHash)
        $keyBytes = [Convert]::FromBase64String($sharedKey)
        $sha256 = New-Object System.Security.Cryptography.HMACSHA256
        $sha256.Key = $keyBytes
        $calculatedHash = $sha256.ComputeHash($bytesToHash)
        $encodedHash = [Convert]::ToBase64String($calculatedHash)
        $authorization = "SharedKey ${workspaceId}:${encodedHash}"
        
        $uri = "https://${workspaceId}.ods.opinsights.azure.com/api/logs?api-version=2016-04-01"
        
        Invoke-RestMethod -Uri $uri -Method Post -ContentType "application/json" `
            -Headers @{
                "Authorization" = $authorization
                "Log-Type" = "KVStoreMetrics"
                "x-ms-date" = $rfc1123date
            } -Body $body
    }
}
```

Get Workspace credentials:
```powershell
$workspace = az monitor log-analytics workspace show `
  --resource-group kraman_rg `
  --workspace-name kvstore-logs | ConvertFrom-Json

$workspaceId = $workspace.customerId

$keys = az monitor log-analytics workspace get-shared-keys `
  --resource-group kraman_rg `
  --workspace-name kvstore-logs | ConvertFrom-Json

$sharedKey = $keys.primarySharedKey
```

## Dashboard Queries

### Average Latency by Method
```kusto
KVStoreMetrics_CL
| where TimeGenerated > ago(1h)
| summarize 
    AvgStorageMs = avg(storage_latency_us_d) / 1000,
    AvgTotalMs = avg(total_latency_us_d) / 1000,
    AvgOverheadMs = avg(overhead_us_d) / 1000,
    RequestCount = count()
  by method_s
| render columnchart
```

### P50, P90, P99 Latency
```kusto
KVStoreMetrics_CL
| where TimeGenerated > ago(1h)
| summarize 
    P50 = percentile(total_latency_us_d / 1000, 50),
    P90 = percentile(total_latency_us_d / 1000, 90),
    P99 = percentile(total_latency_us_d / 1000, 99)
  by bin(TimeGenerated, 5m), method_s
| render timechart
```

### Success Rate
```kusto
KVStoreMetrics_CL
| where TimeGenerated > ago(1h)
| summarize 
    Total = count(),
    Successful = countif(success_b == true),
    Failed = countif(success_b == false)
  by bin(TimeGenerated, 5m), method_s
| extend SuccessRate = (Successful * 100.0) / Total
| render timechart
```

## Recommendation

For your scenario, I recommend **Option 1 (Azure Monitor Agent)** because:
- ✅ No code changes required - just redirect stdout to a file
- ✅ Built-in monitoring and alerting
- ✅ Easy to query with KQL
- ✅ Integrates with Azure dashboards
- ✅ Minimal performance overhead

Start the server with:
```powershell
.\KVStoreServer.exe --port 8085 --disable-sdk-logging | Tee-Object -FilePath "C:\KVStoreServer\metrics.log" -Append
```
