#include "KVStoreServiceImpl.h"
#include <iostream>
#include <sstream>

namespace kvstore {

KVStoreServiceImpl::KVStoreServiceImpl() {
    LogInfo("KVStore gRPC Service initialized");
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

grpc::Status KVStoreServiceImpl::Lookup(
    grpc::ServerContext* context,
    const LookupRequest* request,
    LookupResponse* response) {
    
    // Validate request
    if (request->account_url().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "account_url is required");
    }
    if (request->container_name().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "container_name is required");
    }
    if (request->tokens().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "tokens list cannot be empty");
    }
    
    // Get or create store instance
    auto store = GetOrCreateStore(request->account_url(), request->container_name());
    if (!store) {
        response->set_success(false);
        response->set_error("Failed to initialize storage for account");
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to initialize storage");
    }
    
    try {
        std::ostringstream logStream;
        logStream << "[Lookup] partition_key=" << request->partition_key() 
                  << ", completion_id=" << request->completion_id()
                  << ", tokens=" << request->tokens().size()
                  << ", precomputed_hashes=" << request->precomputed_hashes().size();
        LogInfo(logStream.str());
        
        // Convert tokens from protobuf to vector
        std::vector<Token> tokens;
        tokens.reserve(request->tokens().size());
        for (auto token : request->tokens()) {
            tokens.push_back(static_cast<Token>(token));
        }
        
        // Convert precomputed hashes if provided
        std::vector<hash_t> precomputedHashes;
        if (request->precomputed_hashes().size() > 0) {
            precomputedHashes.reserve(request->precomputed_hashes().size());
            for (auto hash : request->precomputed_hashes()) {
                precomputedHashes.push_back(static_cast<hash_t>(hash));
            }
            logStream.str("");
            logStream << "[Lookup] precomputed_hashes[0]=" << precomputedHashes[0];
            LogInfo(logStream.str());
        }
        
        // Perform lookup
        LookupResult result = store->Lookup(
            request->partition_key(),
            request->completion_id(),
            tokens.begin(),
            tokens.end(),
            precomputedHashes
        );
        
        logStream.str("");
        logStream << "[Lookup] Result: cachedBlocks=" << result.cachedBlocks 
                  << ", lastHash=" << result.lastHash
                  << ", locations=" << result.locations.size();
        LogInfo(logStream.str());
        
        // Populate response
        response->set_success(true);
        response->set_cached_blocks(result.cachedBlocks);
        response->set_last_hash(result.lastHash);
        
        for (const auto& location : result.locations) {
            auto* blockLoc = response->add_locations();
            blockLoc->set_hash(location.hash);
            blockLoc->set_location(location.location);
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Lookup failed: " << e.what();
        LogError(oss.str());
        response->set_success(false);
        response->set_error(e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status KVStoreServiceImpl::Read(
    grpc::ServerContext* context,
    const ReadRequest* request,
    ReadResponse* response) {
    
    // Validate request
    if (request->account_url().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "account_url is required");
    }
    if (request->container_name().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "container_name is required");
    }
    if (request->location().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "location is required");
    }
    
    // Get or create store instance
    auto store = GetOrCreateStore(request->account_url(), request->container_name());
    if (!store) {
        response->set_success(false);
        response->set_error("Failed to initialize storage for account");
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to initialize storage");
    }
    
    try {
        // Perform async read and wait for result
        auto future = store->ReadAsync(request->location(), request->completion_id());
        auto [found, chunk] = future.get();
        
        // Populate response
        response->set_success(true);
        response->set_found(found);
        
        if (found) {
            auto* protoChunk = response->mutable_chunk();
            protoChunk->set_hash(chunk.hash);
            protoChunk->set_partition_key(chunk.partitionKey);
            protoChunk->set_parent_hash(chunk.parentHash);
            protoChunk->set_buffer(chunk.buffer.data(), chunk.buffer.size());
            protoChunk->set_completion_id(chunk.completionId);
            
            for (auto token : chunk.tokens) {
                protoChunk->add_tokens(static_cast<int64_t>(token));
            }
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Read failed: " << e.what();
        LogError(oss.str());
        response->set_success(false);
        response->set_error(e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status KVStoreServiceImpl::Write(
    grpc::ServerContext* context,
    const WriteRequest* request,
    WriteResponse* response) {
    
    // Validate request
    if (request->account_url().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "account_url is required");
    }
    if (request->container_name().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "container_name is required");
    }
    if (!request->has_chunk()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "chunk is required");
    }
    
    // Get or create store instance
    auto store = GetOrCreateStore(request->account_url(), request->container_name());
    if (!store) {
        response->set_success(false);
        response->set_error("Failed to initialize storage for account");
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to initialize storage");
    }
    
    try {
        // Convert protobuf chunk to native PromptChunk
        const auto& protoChunk = request->chunk();
        
        // Use :: prefix to disambiguate from kvstore::PromptChunk (protobuf)
        ::PromptChunk chunk;
        chunk.hash = protoChunk.hash();
        chunk.partitionKey = protoChunk.partition_key();
        chunk.parentHash = protoChunk.parent_hash();
        chunk.completionId = protoChunk.completion_id();
        
        // Copy buffer
        const std::string& bufferData = protoChunk.buffer();
        chunk.buffer.assign(bufferData.begin(), bufferData.end());
        chunk.bufferSize = chunk.buffer.size();
        
        // Copy tokens
        chunk.tokens.reserve(protoChunk.tokens().size());
        for (auto token : protoChunk.tokens()) {
            chunk.tokens.push_back(static_cast<Token>(token));
        }
        
        // Perform async write and wait for completion
        auto future = store->WriteAsync(chunk);
        future.get();
        
        response->set_success(true);
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Write failed: " << e.what();
        LogError(oss.str());
        response->set_success(false);
        response->set_error(e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

void KVStoreServiceImpl::LogInfo(const std::string& message) const {
    if (logLevel_ >= LogLevel::Information) {
        std::cout << "[INFO] [KVStoreService] " << message << std::endl;
    }
}

void KVStoreServiceImpl::LogError(const std::string& message) const {
    std::cerr << "[ERROR] [KVStoreService] " << message << std::endl;
}

} // namespace kvstore
