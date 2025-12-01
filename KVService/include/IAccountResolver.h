#pragma once

#include "AzureStorageKVStoreLibV2.h"
#include <memory>
#include <string>

namespace kvstore {

// Account resolution result
struct AccountInfo {
    std::string accountUrl;      // Full account URL (e.g., "https://account.blob.core.windows.net")
    std::string containerName;   // Container name
    bool success = false;        // Whether resolution succeeded
    std::string error;           // Error message if resolution failed
};

// Interface for resolving resource names to KVStore instances
// This abstraction allows different resolution strategies:
// - InMemoryAccountResolver: Simple DNS suffix appending
// - Future: DatabaseAccountResolver for complex account lookups
class IAccountResolver {
public:
    virtual ~IAccountResolver() = default;

    // Resolve a resource name and container to a KVStore instance
    // Returns nullptr if resolution fails
    virtual std::shared_ptr<AzureStorageKVStoreLibV2> ResolveStore(
        const std::string& resourceName,
        const std::string& containerName) = 0;

    // Resolve a resource name to account info (for diagnostics/logging)
    virtual AccountInfo ResolveAccountInfo(
        const std::string& resourceName,
        const std::string& containerName) = 0;

    // Get the last error message
    virtual std::string GetLastError() const = 0;
};

} // namespace kvstore
