#pragma once

#include "IAccountResolver.h"
#include <unordered_map>
#include <shared_mutex>
#include <functional>

namespace kvstore {

// Configuration for InMemoryAccountResolver
struct AccountResolverConfig {
    // DNS suffix to append to resource names (e.g., ".blob.core.windows.net")
    std::string blobDnsSuffix = ".blob.core.windows.net";
    
    // URL scheme (http or https)
    std::string urlScheme = "https";
    
    // HTTP transport protocol for Azure SDK
    HttpTransportProtocol httpTransport = HttpTransportProtocol::LibCurl;
    
    // Whether to enable SDK logging
    bool enableSdkLogging = false;
    
    // Whether to enable multi-NIC support
    bool enableMultiNic = true;
    
    // Log level for KVStore instances
    LogLevel logLevel = LogLevel::Error;
};

// In-memory implementation of IAccountResolver
// Resolves resource names by appending configured DNS suffix
// Caches KVStore instances for reuse
class InMemoryAccountResolver : public IAccountResolver {
public:
    using LogCallback = std::function<void(LogLevel, const std::string&)>;

    explicit InMemoryAccountResolver(const AccountResolverConfig& config = {});
    ~InMemoryAccountResolver() override = default;

    // IAccountResolver interface
    std::shared_ptr<AzureStorageKVStoreLibV2> ResolveStore(
        const std::string& resourceName,
        const std::string& containerName) override;

    AccountInfo ResolveAccountInfo(
        const std::string& resourceName,
        const std::string& containerName) override;

    std::string GetLastError() const override;

    // Configuration
    void SetConfig(const AccountResolverConfig& config);
    const AccountResolverConfig& GetConfig() const { return config_; }

    // Set callback for logging
    void SetLogCallback(LogCallback callback) { logCallback_ = std::move(callback); }

private:
    // Build account URL from resource name
    std::string BuildAccountUrl(const std::string& resourceName) const;

    // Generate cache key for store lookup
    std::string GetStoreKey(const std::string& resourceName, const std::string& containerName) const;

    // Create a new KVStore instance
    std::shared_ptr<AzureStorageKVStoreLibV2> CreateStore(
        const std::string& accountUrl,
        const std::string& containerName);

    // Logging helpers
    void LogInfo(const std::string& message) const;
    void LogError(const std::string& message) const;

    // Configuration
    AccountResolverConfig config_;

    // Cache of KVStore instances
    mutable std::shared_mutex storesMutex_;
    std::unordered_map<std::string, std::shared_ptr<AzureStorageKVStoreLibV2>> stores_;

    // Last error
    mutable std::string lastError_;

    // Log callback
    LogCallback logCallback_;
};

} // namespace kvstore
