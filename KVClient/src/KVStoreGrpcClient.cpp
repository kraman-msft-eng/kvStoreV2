// KVStoreGrpcClient.cpp - gRPC client implementation of AzureStorageKVStoreLibV2 interface
// This allows Linux applications to use the same API while communicating with Windows gRPC service

#include "AzureStorageKVStoreLibV2.h"
#include "kvstore.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <iostream>
#include <limits>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using kvstore::KVStoreService;
using kvstore::LookupRequest;
using kvstore::LookupResponse;
using kvstore::ReadRequest;
using kvstore::ReadResponse;
using kvstore::WriteRequest;
using kvstore::WriteResponse;

class AzureStorageKVStoreLibV2::Impl {
public:
    Impl() : initialized_(false) {}
    
    // Extract resource name from account URL
    // e.g., "https://mystorageaccount.blob.core.windows.net" -> "mystorageaccount"
    static std::string ExtractResourceName(const std::string& accountUrl) {
        // Find the start of the hostname (after "https://")
        size_t start = accountUrl.find("://");
        if (start == std::string::npos) {
            // No scheme, assume it's already just the resource name
            return accountUrl;
        }
        start += 3; // Skip "://"
        
        // Find the first dot (end of resource name)
        size_t end = accountUrl.find('.', start);
        if (end == std::string::npos) {
            // No dot found, return everything after scheme
            return accountUrl.substr(start);
        }
        
        return accountUrl.substr(start, end - start);
    }
    
    bool Initialize(const std::string& accountUrl, 
                   const std::string& containerName,
                   const std::string& grpcServerAddress) {
        // Extract just the resource name from the full URL
        resourceName_ = ExtractResourceName(accountUrl);
        containerName_ = containerName;
        
        // Configure channel arguments for performance
        grpc::ChannelArguments args;
        
        // TCP_NODELAY: Disable Nagle's algorithm for low-latency
        // Critical for small messages - prevents 40ms delays waiting to batch data
        args.SetInt("grpc.tcp_user_timeout_ms", 20000);  // 20 second TCP timeout
        args.SetInt("grpc.tcp_nodelay", 1);  // Disable Nagle's algorithm
        
        // Keepalive: ping every 10s to keep connection alive during idle periods
        // This prevents slow initial RPCs after idle periods
        args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 10000);  // 10 seconds
        args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000);  // 5 second timeout
        args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);  // Allow keepalive without active calls
        
        // Increase concurrent stream limit (default ~100, we set to 200 for headroom)
        args.SetInt(GRPC_ARG_MAX_CONCURRENT_STREAMS, 200);
        
        // HTTP/2 flow control: maximize for large messages (1.2MB payloads)
        // Max frame size: 16MB (max allowed by HTTP/2 spec is 16777215)
        // This reduces 1.2MB from 75 frames to 1 frame, cutting syscall overhead
        args.SetInt(GRPC_ARG_HTTP2_MAX_FRAME_SIZE, 16 * 1024 * 1024);  // 16MB max frame
        args.SetInt(GRPC_ARG_HTTP2_BDP_PROBE, 1);  // Enable bandwidth-delay product probing
        
        // Increase flow control window sizes for large payloads
        args.SetInt(GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES, 64 * 1024 * 1024);  // 64MB stream window
        args.SetInt(GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE, 8 * 1024 * 1024);  // 8MB write buffer
        
        // Increase message size limits
        args.SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, 100 * 1024 * 1024);  // 100MB
        args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 100 * 1024 * 1024);  // 100MB
        
        // Create gRPC channel to server with optimized settings
        auto channel = grpc::CreateCustomChannel(grpcServerAddress, 
                                                  grpc::InsecureChannelCredentials(), 
                                                  args);
        stub_ = KVStoreService::NewStub(channel);
        
        initialized_ = true;
        LogMessage(LogLevel::Information, "KVClient initialized - gRPC endpoint: " + grpcServerAddress + 
                   " (resource: " + resourceName_ + ", keepalive: 10s, max_streams: 200)");
        return true;
    }
    
    template<typename TokenIterator>
    LookupResult Lookup(const std::string& partitionKey,
                       const std::string& completionId,
                       TokenIterator tokensBegin,
                       TokenIterator tokensEnd,
                       const std::vector<hash_t>& precomputedHashes) const {
        LookupResult result;
        result.cachedBlocks = 0;
        result.lastHash = 0;
        
        if (!initialized_) {
            LogMessage(LogLevel::Error, "KVClient not initialized");
            return result;
        }
        
        // === MEASURE REQUEST SERIALIZATION ===
        auto serialize_start = std::chrono::high_resolution_clock::now();
        
        // Prepare gRPC request
        LookupRequest request;
        request.set_resource_name(resourceName_);
        request.set_container_name(containerName_);
        request.set_partition_key(partitionKey);
        request.set_completion_id(completionId);
        
        for (auto it = tokensBegin; it != tokensEnd; ++it) {
            request.add_tokens(static_cast<int64_t>(*it));
        }
        
        for (auto hash : precomputedHashes) {
            request.add_precomputed_hashes(hash);
        }
        
        // Force size calculation (triggers internal serialization prep, but doesn't serialize twice)
        size_t request_size = request.ByteSizeLong();
        
        auto serialize_end = std::chrono::high_resolution_clock::now();
        auto serialize_us = std::chrono::duration_cast<std::chrono::microseconds>(serialize_end - serialize_start).count();
        
        // === MAKE gRPC CALL (network + server processing) ===
        LookupResponse response;
        ClientContext context;
        
        auto grpc_start = std::chrono::high_resolution_clock::now();
        Status status = stub_->Lookup(&context, request, &response);
        auto grpc_end = std::chrono::high_resolution_clock::now();
        
        // === MEASURE RESPONSE DESERIALIZATION (data extraction) ===
        auto deserialize_start = std::chrono::high_resolution_clock::now();
        
        // Calculate gRPC call time (includes network + server + protobuf ser/deser by gRPC)
        auto grpc_us = std::chrono::duration_cast<std::chrono::microseconds>(grpc_end - grpc_start).count();
        
        // Log comprehensive metrics (client E2E + server-side breakdown)
        std::string metricsLog = "[Lookup] grpc=" + std::to_string(grpc_us) + "us";
        metricsLog += ", req_ser=" + std::to_string(serialize_us) + "us";
        metricsLog += ", req_size=" + std::to_string(request_size) + "B";
        if (response.has_server_metrics()) {
            const auto& sm = response.server_metrics();
            metricsLog += ", server_total=" + std::to_string(sm.total_latency_us()) + "us";
            metricsLog += ", storage=" + std::to_string(sm.storage_latency_us()) + "us";
            metricsLog += ", overhead=" + std::to_string(sm.overhead_us()) + "us";
        }
        metricsLog += ", partition=" + partitionKey + ", blocks=" + std::to_string(response.cached_blocks());
        LogMessage(LogLevel::Information, metricsLog);
        
        if (!status.ok()) {
            LogMessage(LogLevel::Error, "Lookup RPC failed: " + status.error_message());
            return result;
        }
        
        if (!response.success()) {
            LogMessage(LogLevel::Error, "Lookup failed: " + response.error());
            return result;
        }
        
        // Convert response (this is part of deserialization)
        result.cachedBlocks = response.cached_blocks();
        result.lastHash = response.last_hash();
        
        for (const auto& loc : response.locations()) {
            BlockLocation location;
            location.hash = loc.hash();
            location.location = loc.location();
            result.locations.push_back(location);
        }
        
        auto deserialize_end = std::chrono::high_resolution_clock::now();
        auto deserialize_us = std::chrono::duration_cast<std::chrono::microseconds>(deserialize_end - deserialize_start).count();
        
        // Populate server metrics
        if (response.has_server_metrics()) {
            const auto& sm = response.server_metrics();
            result.server_metrics.storage_latency_us = sm.storage_latency_us();
            result.server_metrics.total_latency_us = sm.total_latency_us();
            result.server_metrics.overhead_us = sm.overhead_us();
        }
        
        // Add client-measured times
        auto e2e_us = serialize_us + grpc_us + deserialize_us;
        result.server_metrics.client_e2e_us = e2e_us;
        result.server_metrics.serialize_us = serialize_us;
        result.server_metrics.deserialize_us = deserialize_us;
        
        // Pure network = gRPC call - server total (gRPC call includes protobuf ser/deser inside gRPC)
        if (result.server_metrics.total_latency_us > 0) {
            result.server_metrics.network_us = grpc_us - result.server_metrics.total_latency_us;
        }
        
        return result;
    }
    
    std::future<std::tuple<bool, PromptChunk, ServerMetrics>> ReadAsync(const std::string& location, 
                                                         const std::string& completionId = "") {
        return std::async(std::launch::async, [this, location, completionId]() -> std::tuple<bool, PromptChunk, ServerMetrics> {
            PromptChunk chunk;
            ServerMetrics metrics;
            
            if (!initialized_) {
                LogMessage(LogLevel::Error, "KVClient not initialized");
                return std::make_tuple(false, chunk, metrics);
            }
            
            // === DETAILED TRACING: Request Preparation ===
            auto t0_start = std::chrono::high_resolution_clock::now();
            
            // Prepare gRPC request
            ReadRequest request;
            request.set_resource_name(resourceName_);
            request.set_container_name(containerName_);
            request.set_location(location);
            request.set_completion_id(completionId);
            
            auto t1_request_built = std::chrono::high_resolution_clock::now();
            
            // Make gRPC call with E2E latency measurement
            ReadResponse response;
            ClientContext context;
            
            auto t2_grpc_start = std::chrono::high_resolution_clock::now();
            Status status = stub_->Read(&context, request, &response);
            auto t3_grpc_end = std::chrono::high_resolution_clock::now();
            
            // === DETAILED TRACING: Response Processing ===
            auto t4_deser_start = std::chrono::high_resolution_clock::now();
            
            // Calculate timing breakdown
            auto request_build_us = std::chrono::duration_cast<std::chrono::microseconds>(t1_request_built - t0_start).count();
            auto grpc_call_us = std::chrono::duration_cast<std::chrono::microseconds>(t3_grpc_end - t2_grpc_start).count();
            auto e2e_us = grpc_call_us;  // The gRPC call is the E2E time
            
            // Log comprehensive metrics (client E2E + server-side breakdown)
            std::string metricsLog = "[Read] e2e=" + std::to_string(e2e_us) + "us";
            metricsLog += ", req_build=" + std::to_string(request_build_us) + "us";
            
            int64_t server_total_us = 0;
            size_t response_size = 0;
            if (response.has_server_metrics()) {
                const auto& sm = response.server_metrics();
                server_total_us = sm.total_latency_us();
                metricsLog += ", server_total=" + std::to_string(sm.total_latency_us()) + "us";
                metricsLog += ", storage=" + std::to_string(sm.storage_latency_us()) + "us";
                metricsLog += ", overhead=" + std::to_string(sm.overhead_us()) + "us";
                metricsLog += ", network_rtt=" + std::to_string(e2e_us - sm.total_latency_us()) + "us";
            }
            
            if (!status.ok()) {
                LogMessage(LogLevel::Error, "Read RPC failed: " + status.error_message());
                return std::make_tuple(false, chunk, metrics);
            }
            
            if (!response.success() || !response.found()) {
                metricsLog += ", found=false";
                LogMessage(LogLevel::Information, metricsLog);
                return std::make_tuple(false, chunk, metrics);
            }
            
            // Convert response - measure deserialization/copy time
            const auto& protoChunk = response.chunk();
            chunk.hash = protoChunk.hash();
            chunk.partitionKey = protoChunk.partition_key();
            chunk.parentHash = protoChunk.parent_hash();
            chunk.completionId = protoChunk.completion_id();
            
            // Copy buffer - this is the largest data copy
            auto t5_buffer_copy_start = std::chrono::high_resolution_clock::now();
            const auto& bufferData = protoChunk.buffer();
            response_size = bufferData.size();
            chunk.buffer.assign(bufferData.begin(), bufferData.end());
            chunk.bufferSize = chunk.buffer.size();
            auto t6_buffer_copy_end = std::chrono::high_resolution_clock::now();
            
            // Copy tokens
            chunk.tokens.reserve(protoChunk.tokens().size());
            for (auto token : protoChunk.tokens()) {
                chunk.tokens.push_back(static_cast<Token>(token));
            }
            
            auto t7_end = std::chrono::high_resolution_clock::now();
            
            // Calculate detailed timing
            auto buffer_copy_us = std::chrono::duration_cast<std::chrono::microseconds>(t6_buffer_copy_end - t5_buffer_copy_start).count();
            auto total_deser_us = std::chrono::duration_cast<std::chrono::microseconds>(t7_end - t4_deser_start).count();
            auto pure_network_us = grpc_call_us - server_total_us - total_deser_us;
            
            metricsLog += ", buf_copy=" + std::to_string(buffer_copy_us) + "us";
            metricsLog += ", deser=" + std::to_string(total_deser_us) + "us";
            metricsLog += ", pure_net=" + std::to_string(pure_network_us) + "us";
            metricsLog += ", size=" + std::to_string(response_size) + "B";
            metricsLog += ", found=true";
            // Print directly to stderr to bypass log callback filtering
            std::cerr << metricsLog << std::endl;
            
            // Populate server metrics
            if (response.has_server_metrics()) {
                const auto& sm = response.server_metrics();
                metrics.storage_latency_us = sm.storage_latency_us();
                metrics.total_latency_us = sm.total_latency_us();
                metrics.overhead_us = sm.overhead_us();
            }
            // Add client-measured E2E time and serialization metrics
            metrics.client_e2e_us = e2e_us;
            metrics.serialize_us = request_build_us;
            metrics.deserialize_us = total_deser_us;
            // Pure network = gRPC call - server total - client deserialize
            if (server_total_us > 0) {
                metrics.network_us = grpc_call_us - server_total_us - total_deser_us;
            }
            
            return std::make_tuple(true, chunk, metrics);
        });
    }
    
    std::future<ServerMetrics> WriteAsync(const PromptChunk& chunk) {
        return std::async(std::launch::async, [this, chunk]() -> ServerMetrics {
            ServerMetrics metrics;
            
            if (!initialized_) {
                LogMessage(LogLevel::Error, "KVClient not initialized");
                throw std::runtime_error("KVClient not initialized");
            }
            
            // === DETAILED TRACING: Request Serialization ===
            auto t0_start = std::chrono::high_resolution_clock::now();
            
            // Prepare gRPC request
            WriteRequest request;
            request.set_resource_name(resourceName_);
            request.set_container_name(containerName_);
            
            auto* protoChunk = request.mutable_chunk();
            protoChunk->set_hash(chunk.hash);
            protoChunk->set_partition_key(chunk.partitionKey);
            protoChunk->set_parent_hash(chunk.parentHash);
            protoChunk->set_completion_id(chunk.completionId);
            
            auto t1_before_buffer = std::chrono::high_resolution_clock::now();
            protoChunk->set_buffer(chunk.buffer.data(), chunk.buffer.size());
            auto t2_after_buffer = std::chrono::high_resolution_clock::now();
            
            for (auto token : chunk.tokens) {
                protoChunk->add_tokens(static_cast<int64_t>(token));
            }
            
            auto t3_request_built = std::chrono::high_resolution_clock::now();
            
            // Make gRPC call with E2E latency measurement
            WriteResponse response;
            ClientContext context;
            
            auto t4_grpc_start = std::chrono::high_resolution_clock::now();
            Status status = stub_->Write(&context, request, &response);
            auto t5_grpc_end = std::chrono::high_resolution_clock::now();
            
            // Calculate timing breakdown
            auto buffer_ser_us = std::chrono::duration_cast<std::chrono::microseconds>(t2_after_buffer - t1_before_buffer).count();
            auto total_ser_us = std::chrono::duration_cast<std::chrono::microseconds>(t3_request_built - t0_start).count();
            auto grpc_call_us = std::chrono::duration_cast<std::chrono::microseconds>(t5_grpc_end - t4_grpc_start).count();
            auto e2e_us = grpc_call_us;
            
            // Log comprehensive metrics (only in verbose mode)
            if (logLevel_ >= LogLevel::Verbose) {
                std::string metricsLog = "[Write] e2e=" + std::to_string(e2e_us) + "us";
                metricsLog += ", ser=" + std::to_string(total_ser_us) + "us";
                metricsLog += ", buf_ser=" + std::to_string(buffer_ser_us) + "us";
                
                int64_t server_total_us = 0;
                if (response.has_server_metrics()) {
                    const auto& sm = response.server_metrics();
                    server_total_us = sm.total_latency_us();
                    metricsLog += ", server_total=" + std::to_string(sm.total_latency_us()) + "us";
                    metricsLog += ", storage=" + std::to_string(sm.storage_latency_us()) + "us";
                    metricsLog += ", overhead=" + std::to_string(sm.overhead_us()) + "us";
                    
                    // Pure network = gRPC call - server processing
                    auto pure_network_us = grpc_call_us - server_total_us;
                    metricsLog += ", pure_net=" + std::to_string(pure_network_us) + "us";
                }
                metricsLog += ", size=" + std::to_string(chunk.bufferSize) + "B";
                std::cerr << metricsLog << std::endl;
            }
            
            if (!status.ok()) {
                LogMessage(LogLevel::Error, "Write RPC failed: " + status.error_message());
                throw std::runtime_error("Write RPC failed: " + status.error_message());
            }
            
            if (!response.success()) {
                LogMessage(LogLevel::Error, "Write failed: " + response.error());
                throw std::runtime_error("Write failed: " + response.error());
            }
            
            // Populate and return server metrics
            int64_t server_total_us = 0;
            if (response.has_server_metrics()) {
                const auto& sm = response.server_metrics();
                metrics.storage_latency_us = sm.storage_latency_us();
                metrics.total_latency_us = sm.total_latency_us();
                metrics.overhead_us = sm.overhead_us();
                server_total_us = sm.total_latency_us();
            }
            // Add client-measured E2E time and serialization metrics
            metrics.client_e2e_us = e2e_us;
            metrics.serialize_us = total_ser_us;
            // Write response is small, deserialize is negligible
            metrics.deserialize_us = 0;
            // Pure network = gRPC call - server processing
            if (server_total_us > 0) {
                metrics.network_us = grpc_call_us - server_total_us;
            }
            
            return metrics;
        });
    }
    
    // Streaming Read - reads multiple locations with reduced network overhead
    std::future<std::vector<std::tuple<bool, PromptChunk, ServerMetrics>>> StreamingReadAsync(
        const std::vector<std::string>& locations,
        const std::string& completionId) {
        
        return std::async(std::launch::async, [this, locations, completionId]() 
            -> std::vector<std::tuple<bool, PromptChunk, ServerMetrics>> {
            
            std::vector<std::tuple<bool, PromptChunk, ServerMetrics>> results;
            results.reserve(locations.size());
            
            if (!initialized_) {
                LogMessage(LogLevel::Error, "KVClient not initialized");
                // Return empty results for all locations
                for (size_t i = 0; i < locations.size(); i++) {
                    results.emplace_back(false, PromptChunk{}, ServerMetrics{});
                }
                return results;
            }
            
            auto stream_start = std::chrono::high_resolution_clock::now();
            
            // Create bidirectional stream
            ClientContext context;
            auto stream = stub_->StreamingRead(&context);
            
            if (!stream) {
                LogMessage(LogLevel::Error, "Failed to create streaming read");
                for (size_t i = 0; i < locations.size(); i++) {
                    results.emplace_back(false, PromptChunk{}, ServerMetrics{});
                }
                return results;
            }
            
            // Send all requests first (pipelining)
            for (const auto& location : locations) {
                ReadRequest request;
                request.set_resource_name(resourceName_);
                request.set_container_name(containerName_);
                request.set_location(location);
                request.set_completion_id(completionId);
                
                if (!stream->Write(request)) {
                    LogMessage(LogLevel::Error, "Failed to write request to stream");
                    break;
                }
            }
            
            // Signal that we're done sending
            stream->WritesDone();
            
            // Read all responses
            ReadResponse response;
            size_t response_count = 0;
            int64_t total_storage_us = 0;
            int64_t min_storage_us = std::numeric_limits<int64_t>::max();
            int64_t max_storage_us = 0;
            
            while (stream->Read(&response)) {
                PromptChunk chunk;
                ServerMetrics metrics;
                bool found = response.found();
                
                if (found && response.has_chunk()) {
                    const auto& protoChunk = response.chunk();
                    chunk.hash = protoChunk.hash();
                    chunk.partitionKey = protoChunk.partition_key();
                    chunk.parentHash = protoChunk.parent_hash();
                    chunk.completionId = protoChunk.completion_id();
                    
                    const auto& bufferData = protoChunk.buffer();
                    chunk.buffer.assign(bufferData.begin(), bufferData.end());
                    chunk.bufferSize = chunk.buffer.size();
                    
                    chunk.tokens.reserve(protoChunk.tokens().size());
                    for (auto token : protoChunk.tokens()) {
                        chunk.tokens.push_back(static_cast<Token>(token));
                    }
                }
                
                if (response.has_server_metrics()) {
                    const auto& sm = response.server_metrics();
                    metrics.storage_latency_us = sm.storage_latency_us();
                    metrics.total_latency_us = sm.total_latency_us();
                    metrics.overhead_us = sm.overhead_us();
                    total_storage_us += sm.storage_latency_us();
                    min_storage_us = std::min(min_storage_us, sm.storage_latency_us());
                    max_storage_us = std::max(max_storage_us, sm.storage_latency_us());
                }
                
                results.emplace_back(found, std::move(chunk), metrics);
                response_count++;
            }
            
            Status status = stream->Finish();
            
            auto stream_end = std::chrono::high_resolution_clock::now();
            auto stream_us = std::chrono::duration_cast<std::chrono::microseconds>(stream_end - stream_start).count();
            
            // Fix min if no responses received
            if (response_count == 0) {
                min_storage_us = 0;
            }
            
            // Set stream-level metrics on first result for aggregate tracking:
            // - client_e2e_us = total stream time (how long client waited for all data)
            // - storage_latency_us = max storage latency (determines minimum possible stream time)
            // - total_latency_us = client e2e (so Server Overhead = e2e - max_storage shows network overhead)
            // - overhead_us = network + gRPC overhead (e2e - max_storage)
            if (!results.empty()) {
                auto& [found, chunk, metrics] = results[0];
                metrics.client_e2e_us = stream_us;
                metrics.storage_latency_us = max_storage_us;  // Max determines stream time
                metrics.total_latency_us = stream_us;         // Set to e2e so overhead calc works
                metrics.overhead_us = stream_us - max_storage_us;  // Network + gRPC overhead
            }
            
            // Log streaming metrics (only in verbose mode)
            if (logLevel_ >= LogLevel::Verbose) {
                std::string metricsLog = "[StreamingRead] e2e=" + std::to_string(stream_us) + "us";
                metricsLog += ", count=" + std::to_string(response_count);
                metricsLog += ", sum_storage=" + std::to_string(total_storage_us) + "us";
                metricsLog += ", min_storage=" + std::to_string(min_storage_us) + "us";
                metricsLog += ", max_storage=" + std::to_string(max_storage_us) + "us";
                metricsLog += ", parallel_savings=" + std::to_string(total_storage_us - max_storage_us) + "us";
                metricsLog += ", overhead=" + std::to_string(stream_us - max_storage_us) + "us";
                std::cerr << metricsLog << std::endl;
            }
            
            if (!status.ok()) {
                LogMessage(LogLevel::Error, "StreamingRead RPC failed: " + status.error_message());
            }
            
            // Fill remaining slots if we got fewer responses than requests
            while (results.size() < locations.size()) {
                results.emplace_back(false, PromptChunk{}, ServerMetrics{});
            }
            
            return results;
        });
    }
    
    void SetLogCallback(std::function<void(LogLevel, const std::string&)> callback) {
        logCallback_ = callback;
    }
    
    void SetLogLevel(LogLevel level) {
        logLevel_ = level;
    }
    
private:
    void LogMessage(LogLevel level, const std::string& message) const {
        if (logCallback_ && level <= logLevel_) {
            logCallback_(level, message);
        }
    }
    
    bool initialized_;
    std::string resourceName_;
    std::string containerName_;
    std::unique_ptr<KVStoreService::Stub> stub_;
    mutable std::function<void(LogLevel, const std::string&)> logCallback_;
    mutable LogLevel logLevel_ = LogLevel::Information;
};

// AzureStorageKVStoreLibV2 implementation
AzureStorageKVStoreLibV2::AzureStorageKVStoreLibV2() 
    : pImpl_(std::make_unique<Impl>()) {
}

AzureStorageKVStoreLibV2::~AzureStorageKVStoreLibV2() = default;

bool AzureStorageKVStoreLibV2::Initialize(const std::string& accountUrl,
                                          const std::string& containerName,
                                          HttpTransportProtocol transportProtocol,
                                          bool enableSdkLogging,
                                          bool enableMultiNic) {
    // For gRPC client, we need the server address
    // Default to localhost:50051, but should be configurable
    std::string grpcServer = "localhost:50051";
    
    // Check for environment variable
    const char* envServer = std::getenv("KVSTORE_GRPC_SERVER");
    if (envServer != nullptr) {
        grpcServer = envServer;
    }
    
    return pImpl_->Initialize(accountUrl, containerName, grpcServer);
}

void AzureStorageKVStoreLibV2::SetLogCallback(LogCallback callback) {
    pImpl_->SetLogCallback(callback);
}

void AzureStorageKVStoreLibV2::SetLogLevel(LogLevel level) {
    pImpl_->SetLogLevel(level);
}

template<typename TokenIterator>
LookupResult AzureStorageKVStoreLibV2::Lookup(const std::string& partitionKey,
                                               const std::string& completionId,
                                               TokenIterator tokensBegin,
                                               TokenIterator tokensEnd,
                                               const std::vector<hash_t>& precomputedHashes) const {
    return pImpl_->Lookup(partitionKey, completionId, tokensBegin, tokensEnd, precomputedHashes);
}

std::future<std::tuple<bool, PromptChunk, ServerMetrics>> AzureStorageKVStoreLibV2::ReadAsync(
    const std::string& location, 
    const std::string& completionId) const {
    return pImpl_->ReadAsync(location, completionId);
}

std::future<ServerMetrics> AzureStorageKVStoreLibV2::WriteAsync(const PromptChunk& chunk) {
    return pImpl_->WriteAsync(chunk);
}

std::future<std::vector<std::tuple<bool, PromptChunk, ServerMetrics>>> AzureStorageKVStoreLibV2::StreamingReadAsync(
    const std::vector<std::string>& locations,
    const std::string& completionId) const {
    return pImpl_->StreamingReadAsync(locations, completionId);
}

// Explicit template instantiations for common iterator types
template LookupResult AzureStorageKVStoreLibV2::Lookup<std::vector<Token>::iterator>(
    const std::string&, const std::string&,
    std::vector<Token>::iterator, std::vector<Token>::iterator,
    const std::vector<hash_t>&) const;

template LookupResult AzureStorageKVStoreLibV2::Lookup<std::vector<Token>::const_iterator>(
    const std::string&, const std::string&,
    std::vector<Token>::const_iterator, std::vector<Token>::const_iterator,
    const std::vector<hash_t>&) const;
