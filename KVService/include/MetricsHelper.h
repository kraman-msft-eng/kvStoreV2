#pragma once

#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader.h>
#include <memory>
#include <string>

namespace kvstore {

class MetricsHelper {
public:
    static MetricsHelper& GetInstance();
    
    void Initialize(const std::string& endpoint, const std::string& instrumentationKey);
    
    void RecordStorageLatency(const std::string& method, double latency_ms);
    void RecordTotalLatency(const std::string& method, double latency_ms);
    void RecordOverhead(const std::string& method, double overhead_ms);
    void IncrementRequestCount(const std::string& method, bool success);
    
    bool IsInitialized() const { return initialized_; }

private:
    MetricsHelper() = default;
    ~MetricsHelper() = default;
    MetricsHelper(const MetricsHelper&) = delete;
    MetricsHelper& operator=(const MetricsHelper&) = delete;
    
    bool initialized_ = false;
    std::shared_ptr<opentelemetry::metrics::MeterProvider> provider_;
    std::shared_ptr<opentelemetry::metrics::Meter> meter_;
    
    // Metric instruments
    std::unique_ptr<opentelemetry::metrics::Histogram<double>> storage_latency_;
    std::unique_ptr<opentelemetry::metrics::Histogram<double>> total_latency_;
    std::unique_ptr<opentelemetry::metrics::Histogram<double>> overhead_;
    std::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> request_count_;
};

} // namespace kvstore
