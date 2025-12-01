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
