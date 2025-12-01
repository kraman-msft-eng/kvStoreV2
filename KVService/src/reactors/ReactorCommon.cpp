#include "ReactorCommon.h"
#include "MetricsHelper.h"
#include <iostream>
#include <chrono>

namespace kvstore {

// Global flag to control metrics logging (can be disabled for performance)
bool g_enableMetricsLogging = true;
bool g_enableConsoleMetrics = true;  // Separate control for console output

// Helper function to log metrics - sends to both Azure Monitor and optionally console
void LogMetric(const std::string& method, const std::string& request_id,
               int64_t storage_latency_us, int64_t total_latency_us,
               int64_t e2e_latency_us,
               bool success, const std::string& error) {
    // Skip all logging if disabled
    if (!g_enableMetricsLogging) {
        return;
    }
    
    // Convert to milliseconds for metrics
    double storage_ms = storage_latency_us / 1000.0;
    double total_ms = total_latency_us / 1000.0;
    double overhead_ms = (total_latency_us - storage_latency_us) / 1000.0;
    
    // Send to Azure Monitor via OpenTelemetry (if initialized)
    auto& metrics = MetricsHelper::GetInstance();
    if (metrics.IsInitialized()) {
        metrics.RecordStorageLatency(method, storage_ms);
        metrics.RecordTotalLatency(method, total_ms);
        metrics.RecordOverhead(method, overhead_ms);
        metrics.IncrementRequestCount(method, success);
    }
    
    // Optionally log to console in JSON format
    if (g_enableConsoleMetrics) {
        std::cout << "{\"type\":\"metric\""
                  << ",\"method\":\"" << method << "\""
                  << ",\"request_id\":\"" << request_id << "\""
                  << ",\"storage_latency_us\":" << storage_latency_us
                  << ",\"total_latency_us\":" << total_latency_us
                  << ",\"overhead_us\":" << (total_latency_us - storage_latency_us)
                  << ",\"success\":" << (success ? "true" : "false")
                  << ",\"error\":\"" << error << "\""
                  << ",\"timestamp\":" << std::chrono::system_clock::now().time_since_epoch().count()
                  << "}" << std::endl;
    }
}

} // namespace kvstore
