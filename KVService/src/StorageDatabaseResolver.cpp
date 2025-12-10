#include "StorageDatabaseResolver.h"
#include <azure/identity/default_azure_credential.hpp>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace kvstore {

namespace {
    // Simple JSON parsing helpers (avoiding external dependencies)
    
    // Trim whitespace from string
    std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, last - first + 1);
    }
    
    // Extract string value from JSON for a given key
    bool ExtractStringValue(const std::string& json, const std::string& key, std::string& value) {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) {
            return false;
        }
        
        size_t colonPos = json.find(':', keyPos + searchKey.length());
        if (colonPos == std::string::npos) {
            return false;
        }
        
        // Skip whitespace after colon
        size_t valueStart = colonPos + 1;
        while (valueStart < json.length() && std::isspace(json[valueStart])) {
            valueStart++;
        }
        
        if (valueStart >= json.length()) {
            return false;
        }
        
        // Check if value is null
        if (json.substr(valueStart, 4) == "null") {
            value = "";
            return true;
        }
        
        // Find the opening quote of the value
        if (json[valueStart] != '"') {
            return false;
        }
        
        size_t startQuote = valueStart;
        size_t endQuote = startQuote + 1;
        while (endQuote < json.length()) {
            if (json[endQuote] == '"' && json[endQuote - 1] != '\\') {
                break;
            }
            endQuote++;
        }
        
        if (endQuote >= json.length()) {
            return false;
        }
        
        value = json.substr(startQuote + 1, endQuote - startQuote - 1);
        return true;
    }
    
    // Extract regionStorageMap from JSON
    // Format: "regionStorageMap": { "westus2": ["account1", "account2"], ... }
    bool ExtractRegionStorageMap(const std::string& json, 
                                  std::unordered_map<std::string, std::vector<std::string>>& regionMap) {
        std::string searchKey = "\"regionStorageMap\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) {
            return false;
        }
        
        // Find the opening brace of the map
        size_t bracePos = json.find('{', keyPos + searchKey.length());
        if (bracePos == std::string::npos) {
            return false;
        }
        
        // Find matching closing brace
        int braceCount = 1;
        size_t endBrace = bracePos + 1;
        while (endBrace < json.length() && braceCount > 0) {
            if (json[endBrace] == '{') braceCount++;
            else if (json[endBrace] == '}') braceCount--;
            endBrace++;
        }
        
        if (braceCount != 0) {
            return false;
        }
        
        std::string mapContent = json.substr(bracePos + 1, endBrace - bracePos - 2);
        
        // Parse each region entry
        size_t pos = 0;
        while (pos < mapContent.length()) {
            // Find region name
            size_t quoteStart = mapContent.find('"', pos);
            if (quoteStart == std::string::npos) break;
            
            size_t quoteEnd = mapContent.find('"', quoteStart + 1);
            if (quoteEnd == std::string::npos) break;
            
            std::string regionName = mapContent.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            
            // Find the array
            size_t arrayStart = mapContent.find('[', quoteEnd);
            if (arrayStart == std::string::npos) break;
            
            size_t arrayEnd = mapContent.find(']', arrayStart);
            if (arrayEnd == std::string::npos) break;
            
            std::string arrayContent = mapContent.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
            
            // Parse array elements
            std::vector<std::string> accounts;
            size_t arrPos = 0;
            while (arrPos < arrayContent.length()) {
                size_t elemStart = arrayContent.find('"', arrPos);
                if (elemStart == std::string::npos) break;
                
                size_t elemEnd = arrayContent.find('"', elemStart + 1);
                if (elemEnd == std::string::npos) break;
                
                accounts.push_back(arrayContent.substr(elemStart + 1, elemEnd - elemStart - 1));
                arrPos = elemEnd + 1;
            }
            
            if (!accounts.empty()) {
                regionMap[regionName] = accounts;
            }
            
            pos = arrayEnd + 1;
        }
        
        return !regionMap.empty();
    }
}

StorageDatabaseResolver::StorageDatabaseResolver(const StorageDatabaseResolverConfig& config)
    : config_(config) {
}

bool StorageDatabaseResolver::Initialize() {
    return InitializeConfigStoreClient();
}

bool StorageDatabaseResolver::InitializeConfigStoreClient() {
    if (configStoreInitialized_) {
        return true;
    }
    
    const auto& serviceConfig = config_.serviceConfig;
    
    if (!serviceConfig.IsValid()) {
        lastError_ = "Invalid service configuration: " + serviceConfig.GetValidationError();
        LogError(lastError_);
        return false;
    }
    
    try {
        // Build the config store URL
        std::string configStoreUrl = config_.urlScheme + "://" + 
                                      serviceConfig.configurationStore + 
                                      serviceConfig.domainSuffix;
        
        LogInfo("Initializing config store client:");
        LogInfo("  Account: " + serviceConfig.configurationStore);
        LogInfo("  Container: " + serviceConfig.configurationContainer);
        LogInfo("  URL: " + configStoreUrl + "/" + serviceConfig.configurationContainer);
        
        // Create credential using DefaultAzureCredential (supports MI, CLI, etc.)
        auto credential = std::make_shared<Azure::Identity::DefaultAzureCredential>();
        
        // Create blob service client
        Azure::Storage::Blobs::BlobClientOptions options;
        auto serviceClient = Azure::Storage::Blobs::BlobServiceClient(configStoreUrl, credential, options);
        
        // Get container client for the configuration container
        configStoreClient_ = std::make_shared<Azure::Storage::Blobs::BlobContainerClient>(
            serviceClient.GetBlobContainerClient(serviceConfig.configurationContainer));
        
        configStoreInitialized_ = true;
        LogInfo("Config store client initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Failed to initialize config store client: " << e.what();
        lastError_ = oss.str();
        LogError(lastError_);
        return false;
    }
}

PromptAccountConfig StorageDatabaseResolver::FetchAccountConfig(const std::string& resourceName) {
    PromptAccountConfig result;
    result.success = false;
    
    // Check cache first
    {
        std::shared_lock<std::shared_mutex> lock(accountConfigMutex_);
        auto it = accountConfigCache_.find(resourceName);
        if (it != accountConfigCache_.end()) {
            LogVerbose("Using cached account config for: " + resourceName);
            return it->second;
        }
    }
    
    // Ensure config store is initialized
    if (!configStoreInitialized_ && !InitializeConfigStoreClient()) {
        result.error = "Config store not initialized: " + lastError_;
        return result;
    }
    
    try {
        // The blob name is the resource name (prompt account name) with .json extension
        std::string blobName = resourceName + ".json";
        
        const auto& serviceConfig = config_.serviceConfig;
        LogInfo("Fetching account config:");
        LogInfo("  Config Store Account: " + serviceConfig.configurationStore);
        LogInfo("  Config Store Container: " + serviceConfig.configurationContainer);
        LogInfo("  Blob Name: " + blobName);
        LogInfo("  Full Path: " + serviceConfig.configurationStore + serviceConfig.domainSuffix + 
                "/" + serviceConfig.configurationContainer + "/" + blobName);
        
        auto blobClient = configStoreClient_->GetBlobClient(blobName);
        auto downloadResponse = blobClient.Download();
        
        // Read blob content
        auto& bodyStream = downloadResponse.Value.BodyStream;
        std::vector<uint8_t> buffer(static_cast<size_t>(downloadResponse.Value.BlobSize));
        bodyStream->ReadToCount(buffer.data(), buffer.size());
        
        std::string jsonContent(buffer.begin(), buffer.end());
        
        LogVerbose("Account config JSON: " + jsonContent);
        
        // Parse the JSON
        result = ParseAccountConfigJson(jsonContent, resourceName);
        
        // Cache the result if successful
        if (result.success) {
            std::unique_lock<std::shared_mutex> lock(accountConfigMutex_);
            accountConfigCache_[resourceName] = result;
        }
        
    } catch (const Azure::Core::RequestFailedException& e) {
        std::ostringstream oss;
        oss << "Failed to fetch account config for '" << resourceName << "': " 
            << e.what() << " (Status: " << static_cast<int>(e.StatusCode) << ")";
        result.error = oss.str();
        LogError(result.error);
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Failed to fetch account config for '" << resourceName << "': " << e.what();
        result.error = oss.str();
        LogError(result.error);
    }
    
    return result;
}

PromptAccountConfig StorageDatabaseResolver::ParseAccountConfigJson(
    const std::string& jsonContent, 
    const std::string& resourceName) {
    
    PromptAccountConfig config;
    config.success = false;
    
    // Extract fields
    ExtractStringValue(jsonContent, "promptAccountId", config.promptAccountId);
    ExtractStringValue(jsonContent, "promptAccountName", config.promptAccountName);
    ExtractStringValue(jsonContent, "location", config.location);
    ExtractStringValue(jsonContent, "kind", config.kind);
    
    // Extract regionStorageMap
    if (!ExtractRegionStorageMap(jsonContent, config.regionStorageMap)) {
        config.error = "Failed to parse regionStorageMap from account config";
        LogError(config.error);
        return config;
    }
    
    // Validate required fields
    if (config.regionStorageMap.empty()) {
        config.error = "regionStorageMap is empty in account config";
        LogError(config.error);
        return config;
    }
    
    config.success = true;
    
    LogInfo("Parsed account config for '" + resourceName + "': " +
            "promptAccountName=" + config.promptAccountName + 
            ", location=" + config.location +
            ", regions=" + std::to_string(config.regionStorageMap.size()));
    
    return config;
}

std::string StorageDatabaseResolver::GetStorageAccountForCurrentRegion(const PromptAccountConfig& config) {
    const std::string& currentLocation = config_.serviceConfig.currentLocation;
    
    auto it = config.regionStorageMap.find(currentLocation);
    if (it == config.regionStorageMap.end()) {
        LogError("No storage account found for region: " + currentLocation);
        return "";
    }
    
    if (it->second.empty()) {
        LogError("Empty storage account list for region: " + currentLocation);
        return "";
    }
    
    // Return the first storage account in the list for this region
    // TODO: Could implement load balancing across multiple accounts
    const std::string& storageAccount = it->second[0];
    
    LogInfo("Resolved storage account for region '" + currentLocation + "': " + storageAccount);
    
    return storageAccount;
}

std::string StorageDatabaseResolver::BuildAccountUrl(const std::string& storageAccountName) const {
    std::ostringstream oss;
    oss << config_.urlScheme << "://" << storageAccountName << config_.serviceConfig.domainSuffix;
    return oss.str();
}

std::string StorageDatabaseResolver::GetStoreKey(
    const std::string& resourceName,
    const std::string& containerName) const {
    // Key is still based on user resource name + container (not the resolved storage account)
    return resourceName + "|" + containerName;
}

AccountInfo StorageDatabaseResolver::ResolveAccountInfo(
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
    
    // Fetch account config
    PromptAccountConfig accountConfig = FetchAccountConfig(resourceName);
    if (!accountConfig.success) {
        info.success = false;
        info.error = accountConfig.error;
        lastError_ = info.error;
        return info;
    }
    
    // Get storage account for current region
    std::string storageAccount = GetStorageAccountForCurrentRegion(accountConfig);
    if (storageAccount.empty()) {
        info.success = false;
        info.error = "No storage account found for current region: " + config_.serviceConfig.currentLocation;
        lastError_ = info.error;
        return info;
    }
    
    info.accountUrl = BuildAccountUrl(storageAccount);
    info.containerName = containerName;
    info.success = true;
    
    return info;
}

std::shared_ptr<AzureStorageKVStoreLibV2> StorageDatabaseResolver::ResolveStore(
    const std::string& resourceName,
    const std::string& containerName) {
    
    // Validate inputs
    if (resourceName.empty() || containerName.empty()) {
        lastError_ = "Resource name and container name are required";
        LogError(lastError_);
        return nullptr;
    }
    
    // Key is based on user resource name + container
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
    
    // Fetch account configuration
    PromptAccountConfig accountConfig = FetchAccountConfig(resourceName);
    if (!accountConfig.success) {
        lastError_ = "Failed to fetch account config: " + accountConfig.error;
        LogError(lastError_);
        return nullptr;
    }
    
    // Get storage account for current region
    std::string storageAccount = GetStorageAccountForCurrentRegion(accountConfig);
    if (storageAccount.empty()) {
        lastError_ = "No storage account found for region: " + config_.serviceConfig.currentLocation;
        LogError(lastError_);
        return nullptr;
    }
    
    // Build account URL
    std::string accountUrl = BuildAccountUrl(storageAccount);
    
    // Create new store instance
    auto store = CreateStore(accountUrl, containerName);
    if (!store) {
        return nullptr;
    }
    
    stores_[key] = store;
    
    std::ostringstream oss;
    oss << "Created KV Store instance for resource: " << resourceName 
        << " -> storage account: " << storageAccount
        << " (URL: " << accountUrl << "), container: " << containerName;
    LogInfo(oss.str());
    
    return store;
}

std::shared_ptr<AzureStorageKVStoreLibV2> StorageDatabaseResolver::CreateStore(
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

std::string StorageDatabaseResolver::GetLastError() const {
    return lastError_;
}

void StorageDatabaseResolver::SetConfig(const StorageDatabaseResolverConfig& config) {
    config_ = config;
}

void StorageDatabaseResolver::LogInfo(const std::string& message) const {
    if (logCallback_ && config_.logLevel >= LogLevel::Information) {
        logCallback_(LogLevel::Information, "[StorageDatabaseResolver] " + message);
    }
}

void StorageDatabaseResolver::LogError(const std::string& message) const {
    if (logCallback_) {
        logCallback_(LogLevel::Error, "[StorageDatabaseResolver] " + message);
    }
}

void StorageDatabaseResolver::LogVerbose(const std::string& message) const {
    if (logCallback_ && config_.logLevel >= LogLevel::Verbose) {
        logCallback_(LogLevel::Verbose, "[StorageDatabaseResolver] " + message);
    }
}

} // namespace kvstore
