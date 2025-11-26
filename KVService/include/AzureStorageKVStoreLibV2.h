#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <tuple>
#include <future>
#include <functional>
#include "KVTypes.h"
#include "BloomFilter.h"
#include <azure/storage/blobs.hpp>
#include <memory>
#include <sstream>
#include <iomanip>
#include <azure/core/base64.hpp>
#include <chrono>

// Log levels
enum class LogLevel {
    Error = 0,      // Errors and failures
    Information = 1, // Important operations and results (default)
    Verbose = 2     // Detailed diagnostic information
};

// HTTP Transport Protocol options
enum class HttpTransportProtocol {
    WinHTTP,  // Use native Windows HTTP stack (default, faster on Windows)
    LibCurl   // Use libcurl for cross-platform compatibility
};

// Logging callback type - includes log level
using LogCallback = std::function<void(LogLevel level, const std::string&)>;

// V2 API: Multi-version blob support with conflict detection
class AzureStorageKVStoreLibV2 {
public:
    bool Initialize(const std::string& azureAccountUrl_, const std::string& containerName, HttpTransportProtocol transport = HttpTransportProtocol::WinHTTP, bool enableSdkLogging = true, bool enableMultiNic = false);
    
    // Logging configuration
    void SetLogCallback(LogCallback callback) { logCallback_ = callback; }
    void SetLogLevel(LogLevel level) { logLevel_ = level; }

    // V2 API: Multi-version support with location-based reads
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

public:
    template<typename TokenIterator>
    std::string EncodeTokensToBlobName(TokenIterator begin, TokenIterator end) const {
        std::vector<uint8_t> bytes;
        for (auto it = begin; it != end; ++it) {
            uint32_t token = static_cast<uint32_t>(*it);
            bytes.push_back((token >> 24) & 0xFF);
            bytes.push_back((token >> 16) & 0xFF);
            bytes.push_back((token >> 8) & 0xFF);
            bytes.push_back(token & 0xFF);
        }
        return Azure::Core::_internal::Base64Url::Base64UrlEncode(bytes);
    }

    // Reverse method: decode blob name back to token vector
    std::vector<Token> DecodeBlobNameToTokens(const std::string& blobName) const {
        std::vector<uint8_t> bytes = Azure::Core::_internal::Base64Url::Base64UrlDecode(blobName);
        std::vector<Token> tokens;
        size_t n = bytes.size() / 4;
        tokens.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            uint32_t token = (static_cast<uint32_t>(bytes[i * 4]) << 24) |
                             (static_cast<uint32_t>(bytes[i * 4 + 1]) << 16) |
                             (static_cast<uint32_t>(bytes[i * 4 + 2]) << 8) |
                             (static_cast<uint32_t>(bytes[i * 4 + 3]));
            tokens.push_back(static_cast<Token>(token));
        }
        return tokens;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<hash_t, PromptChunk> store_;

    std::string azureAccountUrl_;
    std::string azureContainerName_;
    std::shared_ptr<Azure::Storage::Blobs::BlobContainerClient> blobContainerClient_;

    // std::unique_ptr<BloomFilter> blobNameFilter_;
    std::shared_ptr<Azure::Storage::Blobs::BlobClient> bloomFilterBlobClient_;
    
    // Logging
    LogLevel logLevel_ = LogLevel::Error;  // Default to Error level
    LogCallback logCallback_;
    
    std::string GetTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        struct tm timeinfo;
#ifdef _WIN32
        localtime_s(&timeinfo, &now_c);
#else
        localtime_r(&now_c, &timeinfo);
#endif
        ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }
    
    void Log(LogLevel level, const std::string& message, const std::string& completionId = "") const {
        if (logCallback_ && level <= logLevel_) {
            std::string timestamp = GetTimestamp();
            std::string fullMessage = "[" + timestamp + "]";
            if (!completionId.empty()) {
                fullMessage += " [Run: " + completionId + "]";
            }
            fullMessage += " " + message;
            logCallback_(level, fullMessage);
        }
    }
};

extern template LookupResult AzureStorageKVStoreLibV2::Lookup<std::vector<Token>::const_iterator>(
    const std::string&,
    const std::string&,
    std::vector<Token>::const_iterator,
    std::vector<Token>::const_iterator,
    const std::vector<hash_t>&
) const;
