#include "MetricsHelper.h"
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <iostream>

namespace kvstore {

namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace otlp = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;

MetricsHelper& MetricsHelper::GetInstance() {
    static MetricsHelper instance;
    return instance;
}

void MetricsHelper::Initialize(const std::string& endpoint, const std::string& instrumentationKey) {
    if (initialized_) {
        return;
    }
    
    try {
        // Configure OTLP HTTP exporter for Azure Monitor
        otlp::OtlpHttpMetricExporterOptions exporter_opts;
        exporter_opts.url = endpoint;
        
        // Add Azure Monitor headers
        exporter_opts.http_headers = {
            {"x-ms-instrumentation-key", instrumentationKey}
        };
        
        auto exporter = std::make_unique<otlp::OtlpHttpMetricExporter>(exporter_opts);
        
        // Configure periodic metric reader (batching)
        metrics_sdk::PeriodicExportingMetricReaderOptions reader_opts;
        reader_opts.export_interval_millis = std::chrono::milliseconds(5000);  // Export every 5 seconds
        reader_opts.export_timeout_millis = std::chrono::milliseconds(3000);
        
        auto reader = std::make_shared<metrics_sdk::PeriodicExportingMetricReader>(
            std::move(exporter), reader_opts);
        
        // Create meter provider with resource attributes
        auto resource_attrs = resource::ResourceAttributes{
            {"service.name", "kvstore-grpc-service"},
            {"service.version", "1.0.0"}
        };
        auto resource_ptr = resource::Resource::Create(resource_attrs);
        
        provider_ = std::make_shared<metrics_sdk::MeterProvider>(
            std::unique_ptr<metrics_sdk::ViewRegistry>(),
            resource_ptr
        );
        
        auto provider = std::static_pointer_cast<metrics_sdk::MeterProvider>(provider_);
        provider->AddMetricReader(std::move(reader));
        
        // Get meter
        meter_ = provider_->GetMeter("kvstore", "1.0.0");
        
        // Create metric instruments
        storage_latency_ = meter_->CreateDoubleHistogram(
            "kvstore.storage.latency",
            "Storage operation latency in milliseconds",
            "ms"
        );
        
        total_latency_ = meter_->CreateDoubleHistogram(
            "kvstore.rpc.latency",
            "Total RPC handler latency in milliseconds",
            "ms"
        );
        
        overhead_ = meter_->CreateDoubleHistogram(
            "kvstore.rpc.overhead",
            "RPC overhead (serialization, gRPC) in milliseconds",
            "ms"
        );
        
        request_count_ = meter_->CreateUInt64Counter(
            "kvstore.rpc.requests",
            "Total number of RPC requests",
            "requests"
        );
        
        initialized_ = true;
        std::cout << "[MetricsHelper] Initialized with endpoint: " << endpoint << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[MetricsHelper] Failed to initialize: " << e.what() << std::endl;
        initialized_ = false;
    }
}

void MetricsHelper::RecordStorageLatency(const std::string& method, double latency_ms) {
    if (!initialized_ || !storage_latency_) return;
    
    try {
        storage_latency_->Record(latency_ms, {{"method", method}}, opentelemetry::context::Context{});
    } catch (...) {
        // Silently ignore errors to not impact RPC performance
    }
}

void MetricsHelper::RecordTotalLatency(const std::string& method, double latency_ms) {
    if (!initialized_ || !total_latency_) return;
    
    try {
        total_latency_->Record(latency_ms, {{"method", method}}, opentelemetry::context::Context{});
    } catch (...) {
        // Silently ignore errors
    }
}

void MetricsHelper::RecordOverhead(const std::string& method, double overhead_ms) {
    if (!initialized_ || !overhead_) return;
    
    try {
        overhead_->Record(overhead_ms, {{"method", method}}, opentelemetry::context::Context{});
    } catch (...) {
        // Silently ignore errors
    }
}

void MetricsHelper::IncrementRequestCount(const std::string& method, bool success) {
    if (!initialized_ || !request_count_) return;
    
    try {
        request_count_->Add(1, {
            {"method", method},
            {"success", success ? "true" : "false"}
        }, opentelemetry::context::Context{});
    } catch (...) {
        // Silently ignore errors
    }
}

} // namespace kvstore
