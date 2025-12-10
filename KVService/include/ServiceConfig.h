#pragma once

#include <string>
#include <memory>

namespace kvstore {

// Service configuration structure
// Contains all configuration properties for the KV Store service
struct ServiceConfig {
    // The region/location where this service instance is running
    // Example: "eastus", "westus2", "westeurope"
    std::string currentLocation;
    
    // Storage account name for configuration store
    // This is where prompt account configuration is stored
    std::string configurationStore;
    
    // Container name in the configuration store where prompt account config is populated
    std::string configurationContainer;
    
    // Domain suffix used to create URLs from account names
    // Default: ".blob.core.windows.net"
    std::string domainSuffix = ".blob.core.windows.net";
    
    // Validates that required configuration is present
    bool IsValid() const {
        return !currentLocation.empty() &&
               !configurationStore.empty() &&
               !configurationContainer.empty() &&
               !domainSuffix.empty();
    }
    
    // Returns validation error message if config is invalid
    std::string GetValidationError() const {
        if (currentLocation.empty()) {
            return "currentLocation is required";
        }
        if (configurationStore.empty()) {
            return "configurationStore is required";
        }
        if (configurationContainer.empty()) {
            return "configurationContainer is required";
        }
        if (domainSuffix.empty()) {
            return "domainSuffix is required";
        }
        return "";
    }
    
    // Builds the full URL for the configuration store
    std::string GetConfigurationStoreUrl() const {
        return "https://" + configurationStore + domainSuffix;
    }
};

// Interface for configuration providers
// Allows for different configuration sources (file, environment, remote, etc.)
class IConfigProvider {
public:
    virtual ~IConfigProvider() = default;
    
    // Load configuration from the provider's source
    // Returns true if configuration was loaded successfully
    virtual bool Load() = 0;
    
    // Get the loaded service configuration
    virtual const ServiceConfig& GetConfig() const = 0;
    
    // Get the last error message if Load() failed
    virtual std::string GetLastError() const = 0;
    
    // Check if configuration has been loaded
    virtual bool IsLoaded() const = 0;
};

} // namespace kvstore
