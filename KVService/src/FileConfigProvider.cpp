#include "FileConfigProvider.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

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
    // Simple parser - handles basic JSON format
    bool ExtractStringValue(const std::string& json, const std::string& key, std::string& value) {
        // Look for "key": "value" pattern
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) {
            return false;
        }
        
        // Find the colon after the key
        size_t colonPos = json.find(':', keyPos + searchKey.length());
        if (colonPos == std::string::npos) {
            return false;
        }
        
        // Find the opening quote of the value
        size_t startQuote = json.find('"', colonPos + 1);
        if (startQuote == std::string::npos) {
            return false;
        }
        
        // Find the closing quote (handle escaped quotes)
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
        
        // Unescape common escape sequences
        size_t pos = 0;
        while ((pos = value.find("\\\"", pos)) != std::string::npos) {
            value.replace(pos, 2, "\"");
            pos++;
        }
        pos = 0;
        while ((pos = value.find("\\\\", pos)) != std::string::npos) {
            value.replace(pos, 2, "\\");
            pos++;
        }
        
        return true;
    }
}

FileConfigProvider::FileConfigProvider(const std::string& configFilePath)
    : configFilePath_(configFilePath.empty() ? "service-config.json" : configFilePath) {
}

bool FileConfigProvider::Load() {
    isLoaded_ = false;
    lastError_.clear();
    
    // Open and read the configuration file
    std::ifstream file(configFilePath_);
    if (!file.is_open()) {
        lastError_ = "Failed to open configuration file: " + configFilePath_;
        return false;
    }
    
    // Read entire file content
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    if (content.empty()) {
        lastError_ = "Configuration file is empty: " + configFilePath_;
        return false;
    }
    
    // Parse JSON content
    if (!ParseJson(content)) {
        return false;
    }
    
    // Validate configuration
    if (!config_.IsValid()) {
        lastError_ = "Invalid configuration: " + config_.GetValidationError();
        return false;
    }
    
    isLoaded_ = true;
    return true;
}

bool FileConfigProvider::ParseJson(const std::string& jsonContent) {
    // Extract required fields
    if (!ExtractStringValue(jsonContent, "currentLocation", config_.currentLocation)) {
        lastError_ = "Missing required field: currentLocation";
        return false;
    }
    
    if (!ExtractStringValue(jsonContent, "configurationStore", config_.configurationStore)) {
        lastError_ = "Missing required field: configurationStore";
        return false;
    }
    
    if (!ExtractStringValue(jsonContent, "configurationContainer", config_.configurationContainer)) {
        lastError_ = "Missing required field: configurationContainer";
        return false;
    }
    
    // Extract optional fields (use defaults if not present)
    std::string domainSuffix;
    if (ExtractStringValue(jsonContent, "domainSuffix", domainSuffix)) {
        config_.domainSuffix = domainSuffix;
    }
    // else: keep the default value from ServiceConfig
    
    return true;
}

} // namespace kvstore
