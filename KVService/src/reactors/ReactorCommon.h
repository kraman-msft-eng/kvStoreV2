#pragma once

#include <string>
#include <chrono>

namespace kvstore {

// Forward declarations
class KVStoreServiceImpl;

// Global flags to control metrics logging (defined in ReactorCommon.cpp)
extern bool g_enableMetricsLogging;
extern bool g_enableConsoleMetrics;

// Helper function to log metrics - sends to both Azure Monitor and optionally console
void LogMetric(const std::string& method, const std::string& request_id,
               int64_t storage_latency_us, int64_t total_latency_us,
               int64_t e2e_latency_us,
               bool success, const std::string& error = "");

} // namespace kvstore
