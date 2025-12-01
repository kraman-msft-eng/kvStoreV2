#include "MetricsHelper.h"
#include <iostream>

namespace kvstore {

MetricsHelper& MetricsHelper::GetInstance() {
    static MetricsHelper instance;
    return instance;
}

void MetricsHelper::Initialize(const std::string& endpoint, const std::string& instrumentationKey) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return;
    }
    
    endpoint_ = endpoint;
    instrumentationKey_ = instrumentationKey;
    initialized_ = true;
    
    std::cout << "[MetricsHelper] Azure Monitor integration initialized" << std::endl;
    std::cout << "  Endpoint: " << endpoint << std::endl;
    
    // TODO: Add full Azure Monitor / OpenTelemetry integration
    // For now, metrics are counted in-memory and can be queried via GetRequestCount()
    // Full telemetry export requires linking opentelemetry-cpp properly
}

void MetricsHelper::RecordStorageLatency(const std::string& method, double latency_ms) {
    // Placeholder - in full implementation, send to Azure Monitor
    // Currently just counting for basic stats
}

void MetricsHelper::RecordTotalLatency(const std::string& method, double latency_ms) {
    // Placeholder
}

void MetricsHelper::RecordOverhead(const std::string& method, double overhead_ms) {
    // Placeholder
}

void MetricsHelper::IncrementRequestCount(const std::string& method, bool success) {
    request_count_++;
    if (!success) {
        error_count_++;
    }
}

} // namespace kvstore
