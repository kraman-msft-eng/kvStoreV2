#include "KVStoreServiceImpl.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>

namespace kvstore {

// Global flag to control metrics logging (can be disabled for performance)
static bool g_enableMetricsLogging = true;

// Helper function to log metrics in JSON format
void LogMetric(const std::string& method, const std::string& request_id,
               int64_t storage_latency_us, int64_t total_latency_us,
               int64_t e2e_latency_us,
               bool success, const std::string& error = "") {
    // Skip logging if disabled (for performance)
    if (!g_enableMetricsLogging) {
        return;
    }
    
    // Log to stdout in JSON format
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

void KVStoreServiceImpl::EnableMetricsLogging(bool enable) {
    g_enableMetricsLogging = enable;
}

KVStoreServiceImpl::KVStoreServiceImpl() {
    LogInfo("KVStore gRPC Service initialized (Async Callback API)");
}

KVStoreServiceImpl::~KVStoreServiceImpl() {
    LogInfo("KVStore gRPC Service shutting down");
}

std::string KVStoreServiceImpl::GetStoreKey(
    const std::string& accountUrl,
    const std::string& containerName) const {
    return accountUrl + "|" + containerName;
}

std::shared_ptr<AzureStorageKVStoreLibV2> KVStoreServiceImpl::GetOrCreateStore(
    const std::string& accountUrl,
    const std::string& containerName) {
    
    std::string key = GetStoreKey(accountUrl, containerName);
    
    // Try read lock first
    {
        std::shared_lock<std::shared_mutex> lock(storesMutex_);
        auto it = stores_.find(key);
        if (it != stores_.end()) {
            return it->second;
        }
    }
    
    // Need to create new instance - acquire write lock
    std::unique_lock<std::shared_mutex> lock(storesMutex_);
    
    // Double-check in case another thread created it
    auto it = stores_.find(key);
    if (it != stores_.end()) {
        return it->second;
    }
    
    // Create new store instance
    auto store = std::make_shared<AzureStorageKVStoreLibV2>();
    
    // Set up logging callback
    store->SetLogCallback([this](LogLevel level, const std::string& message) {
        if (level == LogLevel::Error) {
            LogError(message);
        } else if (level <= logLevel_) {
            LogInfo(message);
        }
    });
    
    store->SetLogLevel(logLevel_);
    
    // Initialize the store
    bool success = store->Initialize(accountUrl, containerName, httpTransport_, enableSdkLogging_, enableMultiNic_);
    
    if (!success) {
        std::ostringstream oss;
        oss << "Failed to initialize KV Store for account: " << accountUrl << ", container: " << containerName;
        LogError(oss.str());
        return nullptr;
    }
    
    stores_[key] = store;
    
    std::ostringstream oss;
    oss << "Created KV Store instance for: " << accountUrl << ", container: " << containerName;
    LogInfo(oss.str());
    
    return store;
}

// Async Lookup Reactor
class LookupReactor : public grpc::ServerUnaryReactor {
public:
    LookupReactor(KVStoreServiceImpl* service,
                  grpc::CallbackServerContext* context,
                  const LookupRequest* request,
                  LookupResponse* response)
        : service_(service), context_(context), request_(request), response_(response) {
        
        rpc_start_ = std::chrono::high_resolution_clock::now();
        
        // Extract request ID from metadata
        auto metadata = context->client_metadata();
        auto req_id = metadata.find("request-id");
        if (req_id != metadata.end()) {
            request_id_ = std::string(req_id->second.data(), req_id->second.length());
        }
        
        // Start async work
        StartProcessing();
    }
    
    void OnDone() override {
        delete this;  // Reactor deletes itself when done
    }
    
private:
    void StartProcessing() {
        // Validate request
        if (request_->account_url().empty()) {
            Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "account_url is required"));
            return;
        }
        if (request_->container_name().empty()) {
            Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "container_name is required"));
            return;
        }
        if (request_->tokens().empty()) {
            Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "tokens list cannot be empty"));
            return;
        }
        
        // Get store instance
        auto store = service_->GetOrCreateStore(request_->account_url(), request_->container_name());
        if (!store) {
            response_->set_success(false);
            response_->set_error("Failed to initialize storage for account");
            auto rpc_end = std::chrono::high_resolution_clock::now();
            auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(rpc_end - rpc_start_).count();
            
            auto* metrics = response_->mutable_server_metrics();
            metrics->set_storage_latency_us(0);
            metrics->set_total_latency_us(total_us);
            metrics->set_overhead_us(total_us);
            
            LogMetric("Lookup", request_id_, 0, total_us, 0, false, "Failed to initialize storage");
            Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to initialize storage"));
            return;
        }
        
        // Execute async storage operation in thread pool
        // This prevents blocking the gRPC event loop
        std::thread([this, store]() {
            try {
                auto storage_start = std::chrono::high_resolution_clock::now();
                
                // Convert tokens
                std::vector<Token> tokens;
                tokens.reserve(request_->tokens().size());
                for (auto token : request_->tokens()) {
                    tokens.push_back(static_cast<Token>(token));
                }
                
                // Convert precomputed hashes
                std::vector<hash_t> precomputedHashes;
                if (request_->precomputed_hashes().size() > 0) {
                    precomputedHashes.reserve(request_->precomputed_hashes().size());
                    for (auto hash : request_->precomputed_hashes()) {
                        precomputedHashes.push_back(hash);
                    }
                }
                
                // Perform lookup
                auto result = store->Lookup(
                    request_->partition_key(),
                    request_->completion_id(),
                    tokens.begin(),
                    tokens.end(),
                    precomputedHashes
                );
                
                auto storage_end = std::chrono::high_resolution_clock::now();
                
                // Populate response
                response_->set_success(true);
                response_->set_cached_blocks(result.cachedBlocks);
                response_->set_last_hash(result.lastHash);
                
                for (const auto& location : result.locations) {
                    auto* blockLoc = response_->add_locations();
                    blockLoc->set_hash(location.hash);
                    blockLoc->set_location(location.location);
                }
                
                auto rpc_end = std::chrono::high_resolution_clock::now();
                auto storage_us = std::chrono::duration_cast<std::chrono::microseconds>(storage_end - storage_start).count();
                auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(rpc_end - rpc_start_).count();
                
                // Populate server metrics
                auto* metrics = response_->mutable_server_metrics();
                metrics->set_storage_latency_us(storage_us);
                metrics->set_total_latency_us(total_us);
                metrics->set_overhead_us(total_us - storage_us);
                
                LogMetric("Lookup", request_id_, storage_us, total_us, 0, true);
                
                Finish(grpc::Status::OK);
                
            } catch (const std::exception& e) {
                response_->set_success(false);
                response_->set_error(e.what());
                auto rpc_end = std::chrono::high_resolution_clock::now();
                auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(rpc_end - rpc_start_).count();
                
                auto* metrics = response_->mutable_server_metrics();
                metrics->set_storage_latency_us(0);
                metrics->set_total_latency_us(total_us);
                metrics->set_overhead_us(total_us);
                
                LogMetric("Lookup", request_id_, 0, total_us, 0, false, e.what());
                Finish(grpc::Status(grpc::StatusCode::INTERNAL, e.what()));
            }
        }).detach();
    }
    
    KVStoreServiceImpl* service_;
    grpc::CallbackServerContext* context_;
    const LookupRequest* request_;
    LookupResponse* response_;
    std::chrono::high_resolution_clock::time_point rpc_start_;
    std::string request_id_ = "unknown";
};

// Async Read Reactor
class ReadReactor : public grpc::ServerUnaryReactor {
public:
    ReadReactor(KVStoreServiceImpl* service,
                grpc::CallbackServerContext* context,
                const ReadRequest* request,
                ReadResponse* response)
        : service_(service), context_(context), request_(request), response_(response) {
        
        rpc_start_ = std::chrono::high_resolution_clock::now();
        
        auto metadata = context->client_metadata();
        auto req_id = metadata.find("request-id");
        if (req_id != metadata.end()) {
            request_id_ = std::string(req_id->second.data(), req_id->second.length());
        }
        
        StartProcessing();
    }
    
    void OnDone() override {
        delete this;
    }
    
private:
    void StartProcessing() {
        if (request_->account_url().empty()) {
            Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "account_url is required"));
            return;
        }
        if (request_->container_name().empty()) {
            Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "container_name is required"));
            return;
        }
        if (request_->location().empty()) {
            Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "location is required"));
            return;
        }
        
        auto store = service_->GetOrCreateStore(request_->account_url(), request_->container_name());
        if (!store) {
            response_->set_success(false);
            response_->set_error("Failed to initialize storage for account");
            auto rpc_end = std::chrono::high_resolution_clock::now();
            auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(rpc_end - rpc_start_).count();
            
            auto* metrics = response_->mutable_server_metrics();
            metrics->set_storage_latency_us(0);
            metrics->set_total_latency_us(total_us);
            metrics->set_overhead_us(total_us);
            
            LogMetric("Read", request_id_, 0, total_us, 0, false, "Failed to initialize storage");
            Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to initialize storage"));
            return;
        }
        
        std::thread([this, store]() {
            try {
                auto storage_start = std::chrono::high_resolution_clock::now();
                
                auto future = store->ReadAsync(request_->location(), request_->completion_id());
                auto [found, chunk] = future.get();
                
                auto storage_end = std::chrono::high_resolution_clock::now();
                
                response_->set_success(true);
                response_->set_found(found);
                
                if (found) {
                    auto* protoChunk = response_->mutable_chunk();
                    protoChunk->set_hash(chunk.hash);
                    protoChunk->set_partition_key(chunk.partitionKey);
                    protoChunk->set_parent_hash(chunk.parentHash);
                    protoChunk->set_completion_id(chunk.completionId);
                    protoChunk->set_buffer(chunk.buffer.data(), chunk.buffer.size());
                    
                    for (auto token : chunk.tokens) {
                        protoChunk->add_tokens(static_cast<int64_t>(token));
                    }
                }
                
                auto rpc_end = std::chrono::high_resolution_clock::now();
                auto storage_us = std::chrono::duration_cast<std::chrono::microseconds>(storage_end - storage_start).count();
                auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(rpc_end - rpc_start_).count();
                
                auto* metrics = response_->mutable_server_metrics();
                metrics->set_storage_latency_us(storage_us);
                metrics->set_total_latency_us(total_us);
                metrics->set_overhead_us(total_us - storage_us);
                
                LogMetric("Read", request_id_, storage_us, total_us, 0, true);
                
                Finish(grpc::Status::OK);
                
            } catch (const std::exception& e) {
                response_->set_success(false);
                response_->set_error(e.what());
                auto rpc_end = std::chrono::high_resolution_clock::now();
                auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(rpc_end - rpc_start_).count();
                
                auto* metrics = response_->mutable_server_metrics();
                metrics->set_storage_latency_us(0);
                metrics->set_total_latency_us(total_us);
                metrics->set_overhead_us(total_us);
                
                LogMetric("Read", request_id_, 0, total_us, 0, false, e.what());
                Finish(grpc::Status(grpc::StatusCode::INTERNAL, e.what()));
            }
        }).detach();
    }
    
    KVStoreServiceImpl* service_;
    grpc::CallbackServerContext* context_;
    const ReadRequest* request_;
    ReadResponse* response_;
    std::chrono::high_resolution_clock::time_point rpc_start_;
    std::string request_id_ = "unknown";
};

// Async Write Reactor
class WriteReactor : public grpc::ServerUnaryReactor {
public:
    WriteReactor(KVStoreServiceImpl* service,
                 grpc::CallbackServerContext* context,
                 const WriteRequest* request,
                 WriteResponse* response)
        : service_(service), context_(context), request_(request), response_(response) {
        
        rpc_start_ = std::chrono::high_resolution_clock::now();
        
        auto metadata = context->client_metadata();
        auto req_id = metadata.find("request-id");
        if (req_id != metadata.end()) {
            request_id_ = std::string(req_id->second.data(), req_id->second.length());
        }
        
        StartProcessing();
    }
    
    void OnDone() override {
        delete this;
    }
    
private:
    void StartProcessing() {
        if (request_->account_url().empty()) {
            Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "account_url is required"));
            return;
        }
        if (request_->container_name().empty()) {
            Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "container_name is required"));
            return;
        }
        if (!request_->has_chunk()) {
            Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "chunk is required"));
            return;
        }
        
        auto store = service_->GetOrCreateStore(request_->account_url(), request_->container_name());
        if (!store) {
            response_->set_success(false);
            response_->set_error("Failed to initialize storage for account");
            auto rpc_end = std::chrono::high_resolution_clock::now();
            auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(rpc_end - rpc_start_).count();
            
            auto* metrics = response_->mutable_server_metrics();
            metrics->set_storage_latency_us(0);
            metrics->set_total_latency_us(total_us);
            metrics->set_overhead_us(total_us);
            
            LogMetric("Write", request_id_, 0, total_us, 0, false, "Failed to initialize storage");
            Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to initialize storage"));
            return;
        }
        
        std::thread([this, store]() {
            try {
                auto storage_start = std::chrono::high_resolution_clock::now();
                
                // Convert protobuf chunk to native PromptChunk
                const auto& protoChunk = request_->chunk();
                
                ::PromptChunk chunk;  // Use global namespace to avoid collision with kvstore::PromptChunk
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
                
                // Perform async write
                auto future = store->WriteAsync(chunk);
                future.get();
                
                auto storage_end = std::chrono::high_resolution_clock::now();
                
                response_->set_success(true);
                
                auto rpc_end = std::chrono::high_resolution_clock::now();
                auto storage_us = std::chrono::duration_cast<std::chrono::microseconds>(storage_end - storage_start).count();
                auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(rpc_end - rpc_start_).count();
                
                auto* metrics = response_->mutable_server_metrics();
                metrics->set_storage_latency_us(storage_us);
                metrics->set_total_latency_us(total_us);
                metrics->set_overhead_us(total_us - storage_us);
                
                LogMetric("Write", request_id_, storage_us, total_us, 0, true);
                
                Finish(grpc::Status::OK);
                
            } catch (const std::exception& e) {
                response_->set_success(false);
                response_->set_error(e.what());
                auto rpc_end = std::chrono::high_resolution_clock::now();
                auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(rpc_end - rpc_start_).count();
                
                auto* metrics = response_->mutable_server_metrics();
                metrics->set_storage_latency_us(0);
                metrics->set_total_latency_us(total_us);
                metrics->set_overhead_us(total_us);
                
                LogMetric("Write", request_id_, 0, total_us, 0, false, e.what());
                Finish(grpc::Status(grpc::StatusCode::INTERNAL, e.what()));
            }
        }).detach();
    }
    
    KVStoreServiceImpl* service_;
    grpc::CallbackServerContext* context_;
    const WriteRequest* request_;
    WriteResponse* response_;
    std::chrono::high_resolution_clock::time_point rpc_start_;
    std::string request_id_ = "unknown";
};

// Service implementation - returns reactor for each RPC
grpc::ServerUnaryReactor* KVStoreServiceImpl::Lookup(
    grpc::CallbackServerContext* context,
    const LookupRequest* request,
    LookupResponse* response) {
    
    return new LookupReactor(this, context, request, response);
}

grpc::ServerUnaryReactor* KVStoreServiceImpl::Read(
    grpc::CallbackServerContext* context,
    const ReadRequest* request,
    ReadResponse* response) {
    
    return new ReadReactor(this, context, request, response);
}

grpc::ServerUnaryReactor* KVStoreServiceImpl::Write(
    grpc::CallbackServerContext* context,
    const WriteRequest* request,
    WriteResponse* response) {
    
    return new WriteReactor(this, context, request, response);
}

void KVStoreServiceImpl::LogInfo(const std::string& message) const {
    if (logLevel_ >= LogLevel::Information) {
        std::cout << "[INFO] " << message << std::endl;
    }
}

void KVStoreServiceImpl::LogError(const std::string& message) const {
    std::cerr << "[ERROR] " << message << std::endl;
}

} // namespace kvstore
