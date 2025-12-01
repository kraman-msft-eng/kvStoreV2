#pragma once

#include <grpcpp/grpcpp.h>
#include "KVStoreServiceImpl.h"
#include "ReactorCommon.h"
#include <chrono>
#include <thread>

namespace kvstore {

// Async Write Reactor
class WriteReactor : public grpc::ServerUnaryReactor {
public:
    WriteReactor(KVStoreServiceImpl* service,
                 grpc::CallbackServerContext* context,
                 const WriteRequest* request,
                 WriteResponse* response);
    
    void OnDone() override;
    
private:
    void StartProcessing();
    
    KVStoreServiceImpl* service_;
    grpc::CallbackServerContext* context_;
    const WriteRequest* request_;
    WriteResponse* response_;
    std::chrono::high_resolution_clock::time_point rpc_start_;
    std::string request_id_ = "unknown";
};

} // namespace kvstore
