#pragma once

#include <grpcpp/grpcpp.h>
#include "kvstore.grpc.pb.h"
#include "AzureStorageKVStoreLibV2.h"
#include <unordered_map>
#include <shared_mutex>
#include <memory>

namespace kvstore {

// Forward declarations of reactor classes
class LookupReactor;
class ReadReactor;
class WriteReactor;
class StreamingReadReactor;

// KV Store gRPC Service Implementation (Async Callback API)
// This service provides high-performance, low-latency access to the KV Store
// Uses async callback API for better performance (no thread pool blocking)
// Each request includes account credentials to support multi-tenant scenarios
class KVStoreServiceImpl final : public KVStoreService::CallbackService {
public:
    KVStoreServiceImpl();
    ~KVStoreServiceImpl() override;
    
    // Friend declarations for reactor classes
    friend class LookupReactor;
    friend class ReadReactor;
    friend class WriteReactor;
    friend class StreamingReadReactor;

    // Lookup operation - finds cached blocks matching the token sequence (async callback)
    grpc::ServerUnaryReactor* Lookup(
        grpc::CallbackServerContext* context,
        const LookupRequest* request,
        LookupResponse* response) override;

    // Read operation - retrieves a chunk by location (async callback)
    grpc::ServerUnaryReactor* Read(
        grpc::CallbackServerContext* context,
        const ReadRequest* request,
        ReadResponse* response) override;

    // Write operation - stores a new chunk (async callback)
    grpc::ServerUnaryReactor* Write(
        grpc::CallbackServerContext* context,
        const WriteRequest* request,
        WriteResponse* response) override;

    // Streaming Read operation - retrieves multiple chunks with pipelined requests
    grpc::ServerBidiReactor<ReadRequest, ReadResponse>* StreamingRead(
        grpc::CallbackServerContext* context) override;

    // Configuration
    void SetLogLevel(LogLevel level) { logLevel_ = level; }
    void SetHttpTransport(HttpTransportProtocol transport) { httpTransport_ = transport; }
    void EnableSdkLogging(bool enable) { enableSdkLogging_ = enable; }
    void EnableMultiNic(bool enable) { enableMultiNic_ = enable; }
    void EnableMetricsLogging(bool enable);

private:
    // Get or create a KV Store instance for the given account
    std::shared_ptr<AzureStorageKVStoreLibV2> GetOrCreateStore(
        const std::string& accountUrl,
        const std::string& containerName);

    // Generate a key for the store cache
    std::string GetStoreKey(const std::string& accountUrl, const std::string& containerName) const;

    // Cache of KV Store instances per account/container
    mutable std::shared_mutex storesMutex_;
    std::unordered_map<std::string, std::shared_ptr<AzureStorageKVStoreLibV2>> stores_;

    // Configuration
    LogLevel logLevel_ = LogLevel::Error;
    HttpTransportProtocol httpTransport_ = HttpTransportProtocol::LibCurl;  // Use LibCurl for multi-NIC support
    bool enableSdkLogging_ = true;
    bool enableMultiNic_ = true;  // Enable multi-NIC support by default

    // Service-level logging
    void LogInfo(const std::string& message) const;
    void LogError(const std::string& message) const;
};

} // namespace kvstore
