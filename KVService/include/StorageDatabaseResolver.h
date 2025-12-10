#pragma once

#include "IAccountResolver.h"
#include "ServiceConfig.h"
#include <unordered_map>
#include <shared_mutex>
#include <functional>
#include <azure/storage/blobs.hpp>

namespace kvstore {

// Configuration for StorageDatabaseResolver
struct StorageDatabaseResolverConfig {
    // Service configuration (current location, config store, etc.)
    ServiceConfig serviceConfig;
    
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

// Parsed account configuration from the config store
struct PromptAccountConfig {
    std::string promptAccountId;
    std::string promptAccountName;
    std::string location;
    std::string kind;
    std::unordered_map<std::string, std::vector<std::string>> regionStorageMap;
    bool success = false;
    std::string error;
};

// Storage Database implementation of IAccountResolver
// Resolves resource names by looking up account configuration from a config store (Azure Blob Storage)
// The config store contains JSON blobs mapping user resource names to actual storage accounts
// Uses regionStorageMap to find the storage account for the current region
// Caches both account configurations and KVStore instances for reuse
class StorageDatabaseResolver : public IAccountResolver {
public:
    using LogCallback = std::function<void(LogLevel, const std::string&)>;

    explicit StorageDatabaseResolver(const StorageDatabaseResolverConfig& config);
    ~StorageDatabaseResolver() override = default;

    // IAccountResolver interface
    std::shared_ptr<AzureStorageKVStoreLibV2> ResolveStore(
        const std::string& resourceName,
        const std::string& containerName) override;

    AccountInfo ResolveAccountInfo(
        const std::string& resourceName,
        const std::string& containerName) override;

    std::string GetLastError() const override;

    // Configuration
    void SetConfig(const StorageDatabaseResolverConfig& config);
    const StorageDatabaseResolverConfig& GetConfig() const { return config_; }

    // Set callback for logging
    void SetLogCallback(LogCallback callback) { logCallback_ = std::move(callback); }
    
    // Initialize the resolver (connects to config store)
    bool Initialize();
    
    // Check if resolver is initialized
    bool IsInitialized() const { return configStoreInitialized_; }

private:
    // Initialize the config store blob client
    bool InitializeConfigStoreClient();
    
    // Fetch and parse account configuration from the config store
    PromptAccountConfig FetchAccountConfig(const std::string& resourceName);
    
    // Parse account config JSON
    PromptAccountConfig ParseAccountConfigJson(const std::string& jsonContent, const std::string& resourceName);
    
    // Get the storage account for the current region from account config
    std::string GetStorageAccountForCurrentRegion(const PromptAccountConfig& config);

    // Build account URL from storage account name using domainSuffix from config
    std::string BuildAccountUrl(const std::string& storageAccountName) const;

    // Generate cache key for store lookup (uses user resource name + container)
    std::string GetStoreKey(const std::string& resourceName, const std::string& containerName) const;

    // Create a new KVStore instance
    std::shared_ptr<AzureStorageKVStoreLibV2> CreateStore(
        const std::string& accountUrl,
        const std::string& containerName);

    // Logging helpers
    void LogInfo(const std::string& message) const;
    void LogError(const std::string& message) const;
    void LogVerbose(const std::string& message) const;

    // Configuration
    StorageDatabaseResolverConfig config_;
    
    // Config store blob container client
    std::shared_ptr<Azure::Storage::Blobs::BlobContainerClient> configStoreClient_;
    bool configStoreInitialized_ = false;
    
    // Cache of resolved account configs (resourceName -> PromptAccountConfig)
    mutable std::shared_mutex accountConfigMutex_;
    std::unordered_map<std::string, PromptAccountConfig> accountConfigCache_;

    // Cache of KVStore instances (resourceName|containerName -> KVStore)
    mutable std::shared_mutex storesMutex_;
    std::unordered_map<std::string, std::shared_ptr<AzureStorageKVStoreLibV2>> stores_;

    // Last error
    mutable std::string lastError_;

    // Log callback
    LogCallback logCallback_;
};

} // namespace kvstore
