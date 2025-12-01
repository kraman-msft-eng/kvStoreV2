#include "ReadReactor.h"
#include <thread>

namespace kvstore {

ReadReactor::ReadReactor(KVStoreServiceImpl* service,
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

void ReadReactor::OnDone() {
    delete this;
}

void ReadReactor::StartProcessing() {
    if (request_->resource_name().empty()) {
        Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "resource_name is required"));
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

} // namespace kvstore
