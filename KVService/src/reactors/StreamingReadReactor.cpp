#include "StreamingReadReactor.h"
#include <thread>

namespace kvstore {

StreamingReadReactor::StreamingReadReactor(KVStoreServiceImpl* service, grpc::CallbackServerContext* context)
    : service_(service), context_(context) {
    
    stream_start_ = std::chrono::high_resolution_clock::now();
    
    // Start reading the first request
    StartRead(&current_request_);
}

void StreamingReadReactor::OnReadDone(bool ok) {
    if (!ok) {
        // Client finished sending requests
        // Wait for all pending writes to complete, then finish
        std::unique_lock<std::mutex> lock(mutex_);
        client_done_ = true;
        if (pending_writes_ == 0 && active_threads_ == 0) {
            lock.unlock();
            Finish(grpc::Status::OK);
        }
        return;
    }
    
    // Process this request
    ProcessRequest(current_request_);
    
    // Read the next request (pipelining)
    StartRead(&current_request_);
}

void StreamingReadReactor::OnWriteDone(bool ok) {
    std::unique_lock<std::mutex> lock(mutex_);
    pending_writes_--;
    write_in_progress_ = false;  // Reset write flag
    
    if (!ok) {
        // Write failed, finish with error
        lock.unlock();
        Finish(grpc::Status(grpc::StatusCode::INTERNAL, "Write failed"));
        return;
    }
    
    // Try to send next response if available
    TrySendNextResponse();
    
    // If client is done and no more pending work, finish
    if (client_done_ && pending_writes_ == 0 && active_threads_ == 0 && response_queue_.empty()) {
        lock.unlock();
        Finish(grpc::Status::OK);
    }
}

void StreamingReadReactor::OnDone() {
    // Wait for all active threads to complete before destroying
    {
        std::unique_lock<std::mutex> lock(mutex_);
        shutdown_ = true;
        shutdown_cv_.wait(lock, [this]() { return active_threads_ == 0; });
    }
    
    auto stream_end = std::chrono::high_resolution_clock::now();
    auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(stream_end - stream_start_).count();
    LogMetric("StreamingRead", "stream", 0, total_us, 0, true);
    delete this;
}

void StreamingReadReactor::ProcessRequest(const ReadRequest& request) {
    // Validate request
    if (request.resource_name().empty() || request.container_name().empty() || request.location().empty()) {
        auto response = std::make_unique<ReadResponse>();
        response->set_success(false);
        response->set_found(false);
        response->set_error("Invalid request: missing required fields");
        
        std::unique_lock<std::mutex> lock(mutex_);
        response_queue_.push(std::move(response));
        TrySendNextResponse();
        return;
    }
    
    // Resolve store using AccountResolver
    auto store = service_->GetAccountResolver()->ResolveStore(request.resource_name(), request.container_name());
    if (!store) {
        auto response = std::make_unique<ReadResponse>();
        response->set_success(false);
        response->set_found(false);
        response->set_error("Failed to initialize storage");
        
        std::unique_lock<std::mutex> lock(mutex_);
        response_queue_.push(std::move(response));
        TrySendNextResponse();
        return;
    }
    
    // Increment active thread count before spawning
    {
        std::unique_lock<std::mutex> lock(mutex_);
        active_threads_++;
    }
    
    // Process async in thread to not block reactor
    std::string location = request.location();
    std::string completion_id = request.completion_id();
    
    std::thread([this, store, location, completion_id]() {
        auto response = std::make_unique<ReadResponse>();
        
        try {
            auto storage_start = std::chrono::high_resolution_clock::now();
            
            auto future = store->ReadAsync(location, completion_id);
            auto [found, chunk] = future.get();
            
            auto storage_end = std::chrono::high_resolution_clock::now();
            auto storage_us = std::chrono::duration_cast<std::chrono::microseconds>(storage_end - storage_start).count();
            
            response->set_success(true);
            response->set_found(found);
            
            if (found) {
                auto* protoChunk = response->mutable_chunk();
                protoChunk->set_hash(chunk.hash);
                protoChunk->set_partition_key(chunk.partitionKey);
                protoChunk->set_parent_hash(chunk.parentHash);
                protoChunk->set_completion_id(chunk.completionId);
                protoChunk->set_buffer(chunk.buffer.data(), chunk.buffer.size());
                
                for (auto token : chunk.tokens) {
                    protoChunk->add_tokens(static_cast<int64_t>(token));
                }
            }
            
            auto* metrics = response->mutable_server_metrics();
            metrics->set_storage_latency_us(storage_us);
            metrics->set_total_latency_us(storage_us);
            metrics->set_overhead_us(0);
            
        } catch (const std::exception& e) {
            response->set_success(false);
            response->set_found(false);
            response->set_error(e.what());
        }
        
        // Queue response and decrement thread count
        std::unique_lock<std::mutex> lock(mutex_);
        bool was_shutdown = shutdown_;
        
        if (!was_shutdown) {
            response_queue_.push(std::move(response));
            TrySendNextResponse();
        }
        
        active_threads_--;
        
        // Check if we need to finish or signal shutdown
        if (was_shutdown) {
            shutdown_cv_.notify_one();
        } else if (client_done_ && pending_writes_ == 0 && active_threads_ == 0 && response_queue_.empty()) {
            lock.unlock();
            Finish(grpc::Status::OK);
        }
    }).detach();
}

void StreamingReadReactor::TrySendNextResponse() {
    // Must be called with mutex held
    if (write_in_progress_ || response_queue_.empty()) {
        return;
    }
    
    current_response_ = std::move(response_queue_.front());
    response_queue_.pop();
    write_in_progress_ = true;
    pending_writes_++;
    
    StartWrite(current_response_.get());
}

} // namespace kvstore
