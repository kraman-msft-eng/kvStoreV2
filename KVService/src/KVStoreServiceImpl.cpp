#include "KVStoreServiceImpl.h"
#include "reactors/ReactorCommon.h"
#include "reactors/LookupReactor.h"
#include "reactors/ReadReactor.h"
#include "reactors/WriteReactor.h"
#include "reactors/StreamingReadReactor.h"
#include <iostream>
#include <sstream>

namespace kvstore {

void KVStoreServiceImpl::EnableMetricsLogging(bool enable) {
    g_enableMetricsLogging = enable;
    g_enableConsoleMetrics = enable;  // Console follows main flag by default
}

KVStoreServiceImpl::KVStoreServiceImpl(std::shared_ptr<IAccountResolver> accountResolver)
    : accountResolver_(std::move(accountResolver)) {
    LogInfo("KVStore gRPC Service initialized (Async Callback API with AccountResolver)");
}

KVStoreServiceImpl::~KVStoreServiceImpl() {
    LogInfo("KVStore gRPC Service shutting down");
}

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

grpc::ServerBidiReactor<ReadRequest, ReadResponse>* KVStoreServiceImpl::StreamingRead(
    grpc::CallbackServerContext* context) {
    
    return new StreamingReadReactor(this, context);
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
