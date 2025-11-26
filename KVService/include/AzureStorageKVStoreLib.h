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

// Logging callback type
using LogCallback = std::function<void(const std::string&)>;

class AzureStorageKVStoreLib {
public:
    bool Initialize(const std::string& azureAccountUrl_, const std::string& containerName);
    
    // Enable/disable verbose logging with optional callback
    void SetLogCallback(LogCallback callback) { logCallback_ = callback; }
    void EnableVerboseLogging(bool enable) { verboseLogging_ = enable; }

    // V1 API (backward compatible)
    template<typename TokenIterator>
    std::tuple<int, hash_t> Lookup(
        const std::string& partitionKey,
        const std::string& completionId,
        TokenIterator begin,
        TokenIterator end,
        const std::vector<hash_t>& precomputedHashes
    ) const;

    template<typename TokenIterator>
    std::future<std::pair<bool, PromptChunk>> ReadAsync(TokenIterator begin, TokenIterator end) const;

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
    bool verboseLogging_ = false;
    LogCallback logCallback_;
    
    void Log(const std::string& message) const {
        if (verboseLogging_ && logCallback_) {
            logCallback_(message);
        }
    }
};

extern template std::future<std::pair<bool, PromptChunk>> AzureStorageKVStoreLib::ReadAsync<std::vector<Token>::const_iterator>(
    std::vector<Token>::const_iterator,
    std::vector<Token>::const_iterator
) const;

