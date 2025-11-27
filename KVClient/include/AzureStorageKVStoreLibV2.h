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
    
    // Returns: tuple<found, chunk, server_metrics>
    std::future<std::tuple<bool, PromptChunk, ServerMetrics>> ReadAsync(const std::string& location, const std::string& completionId = "") const;
    // Returns: future<server_metrics>
    std::future<ServerMetrics> WriteAsync(const PromptChunk& chunk);
    
    // Streaming Read - reads multiple locations with reduced network overhead
    // Returns: vector of tuple<found, chunk, server_metrics> in same order as locations
    std::future<std::vector<std::tuple<bool, PromptChunk, ServerMetrics>>> StreamingReadAsync(
        const std::vector<std::string>& locations,
        const std::string& completionId = "") const;

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
