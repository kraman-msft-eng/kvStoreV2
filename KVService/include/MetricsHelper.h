#pragma once

#include <string>
#include <atomic>
#include <mutex>

namespace kvstore {

// Simple MetricsHelper - can be extended to use OpenTelemetry or Azure Monitor SDK
// Currently acts as a lightweight counter that can be queried for stats
class MetricsHelper {
public:
    static MetricsHelper& GetInstance();
    
    void Initialize(const std::string& endpoint, const std::string& instrumentationKey);
    
    void RecordStorageLatency(const std::string& method, double latency_ms);
    void RecordTotalLatency(const std::string& method, double latency_ms);
    void RecordOverhead(const std::string& method, double overhead_ms);
    void IncrementRequestCount(const std::string& method, bool success);
    
    bool IsInitialized() const { return initialized_; }
    
    // Get aggregated stats
    uint64_t GetRequestCount() const { return request_count_.load(); }
    uint64_t GetErrorCount() const { return error_count_.load(); }

private:
    MetricsHelper() = default;
    ~MetricsHelper() = default;
    MetricsHelper(const MetricsHelper&) = delete;
    MetricsHelper& operator=(const MetricsHelper&) = delete;
    
    std::mutex mutex_;
    bool initialized_ = false;
    std::string endpoint_;
    std::string instrumentationKey_;
    
    // Simple atomic counters for basic stats
    std::atomic<uint64_t> request_count_{0};
    std::atomic<uint64_t> error_count_{0};
};

} // namespace kvstore
