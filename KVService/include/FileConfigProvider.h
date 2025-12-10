#pragma once

#include "ServiceConfig.h"
#include <string>

namespace kvstore {

// File-based configuration provider
// Reads configuration from a JSON file
class FileConfigProvider : public IConfigProvider {
public:
    // Construct with path to configuration file
    // If path is empty, uses default path "service-config.json" in current directory
    explicit FileConfigProvider(const std::string& configFilePath = "");
    
    ~FileConfigProvider() override = default;
    
    // IConfigProvider interface
    bool Load() override;
    const ServiceConfig& GetConfig() const override { return config_; }
    std::string GetLastError() const override { return lastError_; }
    bool IsLoaded() const override { return isLoaded_; }
    
    // Get the configuration file path being used
    const std::string& GetConfigFilePath() const { return configFilePath_; }

private:
    std::string configFilePath_;
    ServiceConfig config_;
    std::string lastError_;
    bool isLoaded_ = false;
    
    // Parse JSON content into ServiceConfig
    bool ParseJson(const std::string& jsonContent);
};

} // namespace kvstore
