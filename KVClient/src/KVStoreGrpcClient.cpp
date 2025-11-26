// KVStoreGrpcClient.cpp - gRPC client implementation of AzureStorageKVStoreLibV2 interface
// This allows Linux applications to use the same API while communicating with Windows gRPC service

#include "AzureStorageKVStoreLibV2.h"
#include "kvstore.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <iostream>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using kvstore::KVStoreService;
using kvstore::LookupRequest;
using kvstore::LookupResponse;
using kvstore::ReadRequest;
using kvstore::ReadResponse;
using kvstore::WriteRequest;
using kvstore::WriteResponse;

class AzureStorageKVStoreLibV2::Impl {
public:
    Impl() : initialized_(false) {}
    
    bool Initialize(const std::string& accountUrl, 
                   const std::string& containerName,
                   const std::string& grpcServerAddress) {
        accountUrl_ = accountUrl;
        containerName_ = containerName;
        
        // Create gRPC channel to server
        auto channel = grpc::CreateChannel(grpcServerAddress, grpc::InsecureChannelCredentials());
        stub_ = KVStoreService::NewStub(channel);
        
        initialized_ = true;
        LogMessage(LogLevel::Information, "KVClient initialized - gRPC endpoint: " + grpcServerAddress);
        return true;
    }
    
    template<typename TokenIterator>
    LookupResult Lookup(const std::string& partitionKey,
                       const std::string& completionId,
                       TokenIterator tokensBegin,
                       TokenIterator tokensEnd,
                       const std::vector<hash_t>& precomputedHashes) const {
        LookupResult result;
        result.cachedBlocks = 0;
        result.lastHash = 0;
        
        if (!initialized_) {
            LogMessage(LogLevel::Error, "KVClient not initialized");
            return result;
        }
        
        // Prepare gRPC request
        LookupRequest request;
        request.set_account_url(accountUrl_);
        request.set_container_name(containerName_);
        request.set_partition_key(partitionKey);
        request.set_completion_id(completionId);
        
        for (auto it = tokensBegin; it != tokensEnd; ++it) {
            request.add_tokens(static_cast<int64_t>(*it));
        }
        
        for (auto hash : precomputedHashes) {
            request.add_precomputed_hashes(hash);
        }
        
        // Make gRPC call
        LookupResponse response;
        ClientContext context;
        Status status = stub_->Lookup(&context, request, &response);
        
        if (!status.ok()) {
            LogMessage(LogLevel::Error, "Lookup RPC failed: " + status.error_message());
            return result;
        }
        
        if (!response.success()) {
            LogMessage(LogLevel::Error, "Lookup failed: " + response.error());
            return result;
        }
        
        // Convert response
        result.cachedBlocks = response.cached_blocks();
        result.lastHash = response.last_hash();
        
        for (const auto& loc : response.locations()) {
            BlockLocation location;
            location.hash = loc.hash();
            location.location = loc.location();
            result.locations.push_back(location);
        }
        
        return result;
    }
    
    std::future<std::pair<bool, PromptChunk>> ReadAsync(const std::string& location, 
                                                         const std::string& completionId = "") {
        return std::async(std::launch::async, [this, location, completionId]() {
            std::pair<bool, PromptChunk> result;
            result.first = false;
            
            if (!initialized_) {
                LogMessage(LogLevel::Error, "KVClient not initialized");
                return result;
            }
            
            // Prepare gRPC request
            ReadRequest request;
            request.set_account_url(accountUrl_);
            request.set_container_name(containerName_);
            request.set_location(location);
            request.set_completion_id(completionId);
            
            // Make gRPC call
            ReadResponse response;
            ClientContext context;
            Status status = stub_->Read(&context, request, &response);
            
            if (!status.ok()) {
                LogMessage(LogLevel::Error, "Read RPC failed: " + status.error_message());
                return result;
            }
            
            if (!response.success() || !response.found()) {
                return result;
            }
            
            // Convert response
            result.first = true;
            auto& chunk = result.second;
            
            const auto& protoChunk = response.chunk();
            chunk.hash = protoChunk.hash();
            chunk.partitionKey = protoChunk.partition_key();
            chunk.parentHash = protoChunk.parent_hash();
            chunk.completionId = protoChunk.completion_id();
            
            // Copy buffer
            const auto& bufferData = protoChunk.buffer();
            chunk.buffer.assign(bufferData.begin(), bufferData.end());
            chunk.bufferSize = chunk.buffer.size();
            
            // Copy tokens
            chunk.tokens.reserve(protoChunk.tokens().size());
            for (auto token : protoChunk.tokens()) {
                chunk.tokens.push_back(static_cast<Token>(token));
            }
            
            return result;
        });
    }
    
    std::future<void> WriteAsync(const PromptChunk& chunk) {
        return std::async(std::launch::async, [this, chunk]() {
            if (!initialized_) {
                LogMessage(LogLevel::Error, "KVClient not initialized");
                throw std::runtime_error("KVClient not initialized");
            }
            
            // Prepare gRPC request
            WriteRequest request;
            request.set_account_url(accountUrl_);
            request.set_container_name(containerName_);
            
            auto* protoChunk = request.mutable_chunk();
            protoChunk->set_hash(chunk.hash);
            protoChunk->set_partition_key(chunk.partitionKey);
            protoChunk->set_parent_hash(chunk.parentHash);
            protoChunk->set_completion_id(chunk.completionId);
            protoChunk->set_buffer(chunk.buffer.data(), chunk.buffer.size());
            
            for (auto token : chunk.tokens) {
                protoChunk->add_tokens(static_cast<int64_t>(token));
            }
            
            // Make gRPC call
            WriteResponse response;
            ClientContext context;
            Status status = stub_->Write(&context, request, &response);
            
            if (!status.ok()) {
                LogMessage(LogLevel::Error, "Write RPC failed: " + status.error_message());
                throw std::runtime_error("Write RPC failed: " + status.error_message());
            }
            
            if (!response.success()) {
                LogMessage(LogLevel::Error, "Write failed: " + response.error());
                throw std::runtime_error("Write failed: " + response.error());
            }
        });
    }
    
    void SetLogCallback(std::function<void(LogLevel, const std::string&)> callback) {
        logCallback_ = callback;
    }
    
    void SetLogLevel(LogLevel level) {
        logLevel_ = level;
    }
    
private:
    void LogMessage(LogLevel level, const std::string& message) const {
        if (logCallback_ && level <= logLevel_) {
            logCallback_(level, message);
        }
    }
    
    bool initialized_;
    std::string accountUrl_;
    std::string containerName_;
    std::unique_ptr<KVStoreService::Stub> stub_;
    mutable std::function<void(LogLevel, const std::string&)> logCallback_;
    mutable LogLevel logLevel_ = LogLevel::Information;
};

// AzureStorageKVStoreLibV2 implementation
AzureStorageKVStoreLibV2::AzureStorageKVStoreLibV2() 
    : pImpl_(std::make_unique<Impl>()) {
}

AzureStorageKVStoreLibV2::~AzureStorageKVStoreLibV2() = default;

bool AzureStorageKVStoreLibV2::Initialize(const std::string& accountUrl,
                                          const std::string& containerName,
                                          HttpTransportProtocol transportProtocol,
                                          bool enableSdkLogging,
                                          bool enableMultiNic) {
    // For gRPC client, we need the server address
    // Default to localhost:50051, but should be configurable
    std::string grpcServer = "localhost:50051";
    
    // Check for environment variable
    const char* envServer = std::getenv("KVSTORE_GRPC_SERVER");
    if (envServer != nullptr) {
        grpcServer = envServer;
    }
    
    return pImpl_->Initialize(accountUrl, containerName, grpcServer);
}

void AzureStorageKVStoreLibV2::SetLogCallback(LogCallback callback) {
    pImpl_->SetLogCallback(callback);
}

void AzureStorageKVStoreLibV2::SetLogLevel(LogLevel level) {
    pImpl_->SetLogLevel(level);
}

template<typename TokenIterator>
LookupResult AzureStorageKVStoreLibV2::Lookup(const std::string& partitionKey,
                                               const std::string& completionId,
                                               TokenIterator tokensBegin,
                                               TokenIterator tokensEnd,
                                               const std::vector<hash_t>& precomputedHashes) const {
    return pImpl_->Lookup(partitionKey, completionId, tokensBegin, tokensEnd, precomputedHashes);
}

std::future<std::pair<bool, PromptChunk>> AzureStorageKVStoreLibV2::ReadAsync(
    const std::string& location, 
    const std::string& completionId) const {
    return pImpl_->ReadAsync(location, completionId);
}

std::future<void> AzureStorageKVStoreLibV2::WriteAsync(const PromptChunk& chunk) {
    return pImpl_->WriteAsync(chunk);
}

// Explicit template instantiations for common iterator types
template LookupResult AzureStorageKVStoreLibV2::Lookup<std::vector<Token>::iterator>(
    const std::string&, const std::string&,
    std::vector<Token>::iterator, std::vector<Token>::iterator,
    const std::vector<hash_t>&) const;

template LookupResult AzureStorageKVStoreLibV2::Lookup<std::vector<Token>::const_iterator>(
    const std::string&, const std::string&,
    std::vector<Token>::const_iterator, std::vector<Token>::const_iterator,
    const std::vector<hash_t>&) const;
