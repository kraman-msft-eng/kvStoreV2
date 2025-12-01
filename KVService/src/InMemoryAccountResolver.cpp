#include "InMemoryAccountResolver.h"
#include <sstream>
#include <iostream>

namespace kvstore {

InMemoryAccountResolver::InMemoryAccountResolver(const AccountResolverConfig& config)
    : config_(config) {
}

std::string InMemoryAccountResolver::BuildAccountUrl(const std::string& resourceName) const {
    std::ostringstream oss;
    oss << config_.urlScheme << "://" << resourceName << config_.blobDnsSuffix;
    return oss.str();
}

std::string InMemoryAccountResolver::GetStoreKey(
    const std::string& resourceName,
    const std::string& containerName) const {
    return resourceName + "|" + containerName;
}

AccountInfo InMemoryAccountResolver::ResolveAccountInfo(
    const std::string& resourceName,
    const std::string& containerName) {
    
    AccountInfo info;
    
    if (resourceName.empty()) {
        info.success = false;
        info.error = "Resource name cannot be empty";
        lastError_ = info.error;
        return info;
    }
    
    if (containerName.empty()) {
        info.success = false;
        info.error = "Container name cannot be empty";
        lastError_ = info.error;
        return info;
    }
    
    info.accountUrl = BuildAccountUrl(resourceName);
    info.containerName = containerName;
    info.success = true;
    
    return info;
}

std::shared_ptr<AzureStorageKVStoreLibV2> InMemoryAccountResolver::ResolveStore(
    const std::string& resourceName,
    const std::string& containerName) {
    
    // Validate inputs
    if (resourceName.empty() || containerName.empty()) {
        lastError_ = "Resource name and container name are required";
        LogError(lastError_);
        return nullptr;
    }
    
    std::string key = GetStoreKey(resourceName, containerName);
    
    // Try read lock first (fast path)
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
    
    // Build account URL
    std::string accountUrl = BuildAccountUrl(resourceName);
    
    // Create new store instance
    auto store = CreateStore(accountUrl, containerName);
    if (!store) {
        return nullptr;
    }
    
    stores_[key] = store;
    
    std::ostringstream oss;
    oss << "Created KV Store instance for resource: " << resourceName 
        << " (URL: " << accountUrl << "), container: " << containerName;
    LogInfo(oss.str());
    
    return store;
}

std::shared_ptr<AzureStorageKVStoreLibV2> InMemoryAccountResolver::CreateStore(
    const std::string& accountUrl,
    const std::string& containerName) {
    
    auto store = std::make_shared<AzureStorageKVStoreLibV2>();
    
    // Set up logging callback
    if (logCallback_) {
        store->SetLogCallback(logCallback_);
    }
    
    store->SetLogLevel(config_.logLevel);
    
    // Initialize the store
    bool success = store->Initialize(
        accountUrl, 
        containerName, 
        config_.httpTransport, 
        config_.enableSdkLogging, 
        config_.enableMultiNic);
    
    if (!success) {
        std::ostringstream oss;
        oss << "Failed to initialize KV Store for account: " << accountUrl 
            << ", container: " << containerName;
        lastError_ = oss.str();
        LogError(lastError_);
        return nullptr;
    }
    
    return store;
}

std::string InMemoryAccountResolver::GetLastError() const {
    return lastError_;
}

void InMemoryAccountResolver::SetConfig(const AccountResolverConfig& config) {
    config_ = config;
}

void InMemoryAccountResolver::LogInfo(const std::string& message) const {
    if (logCallback_ && config_.logLevel >= LogLevel::Information) {
        logCallback_(LogLevel::Information, message);
    }
}

void InMemoryAccountResolver::LogError(const std::string& message) const {
    if (logCallback_) {
        logCallback_(LogLevel::Error, message);
    }
}

} // namespace kvstore
