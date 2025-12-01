#pragma once

#include <grpcpp/grpcpp.h>
#include "KVStoreServiceImpl.h"
#include "ReactorCommon.h"
#include <chrono>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>

namespace kvstore {

// Streaming Read Reactor - handles bidirectional streaming for bulk reads
class StreamingReadReactor : public grpc::ServerBidiReactor<ReadRequest, ReadResponse> {
public:
    StreamingReadReactor(KVStoreServiceImpl* service, grpc::CallbackServerContext* context);
    
    void OnReadDone(bool ok) override;
    void OnWriteDone(bool ok) override;
    void OnDone() override;
    
private:
    void ProcessRequest(const ReadRequest& request);
    void TrySendNextResponse();
    
    KVStoreServiceImpl* service_;
    grpc::CallbackServerContext* context_;
    std::chrono::high_resolution_clock::time_point stream_start_;
    
    ReadRequest current_request_;
    std::unique_ptr<ReadResponse> current_response_;
    
    std::mutex mutex_;
    std::condition_variable shutdown_cv_;
    std::queue<std::unique_ptr<ReadResponse>> response_queue_;
    bool write_in_progress_ = false;
    bool client_done_ = false;
    bool shutdown_ = false;
    int pending_writes_ = 0;
    int active_threads_ = 0;
};

} // namespace kvstore
