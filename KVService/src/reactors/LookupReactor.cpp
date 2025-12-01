#include "LookupReactor.h"
#include <thread>

namespace kvstore {

LookupReactor::LookupReactor(KVStoreServiceImpl* service,
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

void LookupReactor::OnDone() {
    delete this;  // Reactor deletes itself when done
}

void LookupReactor::StartProcessing() {
    // Validate request
    if (request_->resource_name().empty()) {
        Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "resource_name is required"));
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
    
    // Resolve store using AccountResolver
    auto store = service_->GetAccountResolver()->ResolveStore(request_->resource_name(), request_->container_name());
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

} // namespace kvstore
