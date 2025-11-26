#pragma once

#include <string>
#include <vector>
#include <tuple>
#include <future>
#include <functional>
#include "KVTypes.h"
#include <memory>

// Log levels
enum class LogLevel {
    Error = 0,      // Errors and failures
    Information = 1, // Important operations and results (default)
    Verbose = 2     // Detailed diagnostic information
};

// HTTP Transport Protocol options (ignored in gRPC client, kept for API compatibility)
enum class HttpTransportProtocol {
    WinHTTP,  // Ignored in gRPC client
    LibCurl   // Ignored in gRPC client
};

// Logging callback type
using LogCallback = std::function<void(LogLevel level, const std::string&)>;

// gRPC Client implementation of AzureStorageKVStoreLibV2 interface
// Provides same API as server-side library but communicates via gRPC
class AzureStorageKVStoreLibV2 {
public:
    // Constructor/Destructor
    AzureStorageKVStoreLibV2();
    ~AzureStorageKVStoreLibV2();

    // Initialize gRPC client connection
    bool Initialize(const std::string& azureAccountUrl, 
                   const std::string& containerName, 
                   HttpTransportProtocol transport = HttpTransportProtocol::WinHTTP, 
                   bool enableSdkLogging = true, 
                   bool enableMultiNic = false);
    
    // Logging configuration
    void SetLogCallback(LogCallback callback);
    void SetLogLevel(LogLevel level);

    // Core API methods
    template<typename TokenIterator>
    LookupResult Lookup(
        const std::string& partitionKey,
        const std::string& completionId,
        TokenIterator begin,
        TokenIterator end,
        const std::vector<hash_t>& precomputedHashes
    ) const;
    
    std::future<std::pair<bool, PromptChunk>> ReadAsync(const std::string& location, const std::string& completionId = "") const;
    std::future<void> WriteAsync(const PromptChunk& chunk);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

// Explicit template instantiation declarations
extern template LookupResult AzureStorageKVStoreLibV2::Lookup<std::vector<Token>::const_iterator>(
    const std::string&,
    const std::string&,
    std::vector<Token>::const_iterator,
    std::vector<Token>::const_iterator,
    const std::vector<hash_t>&
) const;
