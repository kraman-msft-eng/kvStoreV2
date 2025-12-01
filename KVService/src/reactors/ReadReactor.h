#pragma once

#include <grpcpp/grpcpp.h>
#include "KVStoreServiceImpl.h"
#include "ReactorCommon.h"
#include <chrono>
#include <thread>

namespace kvstore {

// Async Read Reactor
class ReadReactor : public grpc::ServerUnaryReactor {
public:
    ReadReactor(KVStoreServiceImpl* service,
                grpc::CallbackServerContext* context,
                const ReadRequest* request,
                ReadResponse* response);
    
    void OnDone() override;
    
private:
    void StartProcessing();
    
    KVStoreServiceImpl* service_;
    grpc::CallbackServerContext* context_;
    const ReadRequest* request_;
    ReadResponse* response_;
    std::chrono::high_resolution_clock::time_point rpc_start_;
    std::string request_id_ = "unknown";
};

} // namespace kvstore
