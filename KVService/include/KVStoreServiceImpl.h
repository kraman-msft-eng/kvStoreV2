#pragma once

#include <grpcpp/grpcpp.h>
#include "kvstore.grpc.pb.h"
#include "IAccountResolver.h"
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
// Uses IAccountResolver to resolve resource names to storage accounts
class KVStoreServiceImpl final : public KVStoreService::CallbackService {
public:
    // Constructor requires an account resolver
    explicit KVStoreServiceImpl(std::shared_ptr<IAccountResolver> accountResolver);
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
    void EnableMetricsLogging(bool enable);

    // Get the account resolver (for reactors to use)
    IAccountResolver* GetAccountResolver() { return accountResolver_.get(); }

private:
    // Account resolver for resource name -> KVStore mapping
    std::shared_ptr<IAccountResolver> accountResolver_;

    // Configuration
    LogLevel logLevel_ = LogLevel::Error;

    // Service-level logging
    void LogInfo(const std::string& message) const;
    void LogError(const std::string& message) const;
};

} // namespace kvstore
