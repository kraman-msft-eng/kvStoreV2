#include "AzureStorageKVStoreLib.h"
#include "BloomFilter.h"
#include <future>
#include <azure/storage/blobs.hpp>
#include <azure/identity/default_azure_credential.hpp>
#include <cassert>
#include <sstream>
#include <iomanip>
#include <random>

bool AzureStorageKVStoreLib::Initialize(const std::string& accountUrl, const std::string& containerName) {
    azureAccountUrl_ = accountUrl;
    azureContainerName_ = containerName;

    // Reset bloom filter (re-create)
    //blobNameFilter_ = std::make_unique<BloomFilter>(1'000'000, 0.05);

    if (!azureAccountUrl_.empty() && !azureContainerName_.empty()) {
        auto credential = std::make_shared<Azure::Identity::DefaultAzureCredential>();
        blobContainerClient_ = std::make_shared<Azure::Storage::Blobs::BlobContainerClient>(
            azureAccountUrl_ + "/" + azureContainerName_, credential);

        /*
        // Create BlobClient for __bloomfilter__ blob
        bloomFilterBlobClient_ = std::make_shared<Azure::Storage::Blobs::BlobClient>(
            blobContainerClient_->GetBlobClient("__bloomfilter__"));

        // Load bloom filter from blob (if exists), else initialize empty
        blobNameFilter_->LoadFromBlob(bloomFilterBlobClient_);

        // Optionally, start polling for changes every 10 seconds
        blobNameFilter_->StartPolling(bloomFilterBlobClient_, std::chrono::seconds(10));
        */
    }
    return true;
}

template<typename TokenIterator>
std::tuple<int, hash_t> AzureStorageKVStoreLib::Lookup(
    const std::string& partitionKey,
    const std::string& completionId,
    TokenIterator begin,
    TokenIterator end,
    const std::vector<hash_t>& precomputedHashes
) const {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    constexpr size_t blockSize = 128;
    
    // Only process full blocks
    size_t totalTokens = std::distance(begin, end);
    size_t numFullBlocks = totalTokens / blockSize;

    Log("[KVStore Lookup] Starting lookup for " + std::to_string(numFullBlocks) + " blocks");
    Log("[KVStore Lookup] Precomputed hashes count: " + std::to_string(precomputedHashes.size()) + ")");

    // Prepare all block data and blob names
    struct BlockInfo {
        std::vector<Token> tokens;
        std::string blobName;
        hash_t expectedHash;
    };
    
    std::vector<BlockInfo> blocks;
    blocks.reserve(numFullBlocks);
    
    TokenIterator it = begin;
    for (size_t blockNum = 0; blockNum < numFullBlocks; ++blockNum) {
        if (blockNum >= precomputedHashes.size()) {
            Log("[KVStore Lookup] Block " + std::to_string(blockNum) + ": No precomputed hash available");
            break;
        }
        
        BlockInfo info;
        for (size_t i = 0; i < blockSize; ++i, ++it) {
            info.tokens.push_back(*it);
        }
        info.blobName = EncodeTokensToBlobName(info.tokens.begin(), info.tokens.end());
        info.expectedHash = precomputedHashes[blockNum];
        blocks.push_back(std::move(info));
    }

    // Launch parallel blob property fetches
    struct BlobResult {
        bool found = false;
        hash_t storedHash = 0;
        hash_t parentHash = 0;
        std::string error;
    };
    
    std::vector<std::future<BlobResult>> futures;
    futures.reserve(blocks.size());
    
    for (size_t i = 0; i < blocks.size(); ++i) {
        futures.push_back(std::async(std::launch::async, [this, &blocks, i]() -> BlobResult {
            BlobResult result;
            try {
                auto blobClient = blobContainerClient_->GetBlobClient(blocks[i].blobName);
                auto properties = blobClient.GetProperties();
                
                auto hashIt = properties.Value.Metadata.find("hash");
                if (hashIt != properties.Value.Metadata.end()) {
                    result.storedHash = std::stoull(hashIt->second);
                }
                
                auto parentIt = properties.Value.Metadata.find("parentHash");
                if (parentIt != properties.Value.Metadata.end()) {
                    result.parentHash = std::stoull(parentIt->second);
                }
                
                result.found = true;
            } catch (const Azure::Storage::StorageException& ex) {
                result.error = ex.what();
            }
            return result;
        }));
    }

    // Process results sequentially to validate parent chain
    int matchedBlocks = 0;
    hash_t lastHash = 0;
    hash_t expectedParentHash = 0;
    
    for (size_t blockNum = 0; blockNum < blocks.size(); ++blockNum) {
        auto result = futures[blockNum].get();
        
        Log("[KVStore Lookup] Block " + std::to_string(blockNum) + ":");
        Log("[KVStore Lookup]   BlobName: " + blocks[blockNum].blobName);
        Log("[KVStore Lookup]   Expected Hash: " + std::to_string(blocks[blockNum].expectedHash));
        Log("[KVStore Lookup]   Expected ParentHash: " + std::to_string(expectedParentHash));
        
        if (!result.found) {
            Log("[KVStore Lookup]   ✗ Blob not found or error: " + result.error);
            Log("[KVStore Lookup]   Breaking lookup chain.");
            break;
        }
        
        Log("[KVStore Lookup]   Blob Found!");
        Log("[KVStore Lookup]   Stored Hash: " + std::to_string(result.storedHash));
        Log("[KVStore Lookup]   Stored ParentHash: " + std::to_string(result.parentHash));
        
        if (matchedBlocks == 0 || result.parentHash == expectedParentHash) {
            Log("[KVStore Lookup]   ✓ Parent chain matches! (matchedBlocks=" + std::to_string(matchedBlocks) + ")");
            lastHash = blocks[blockNum].expectedHash;
            expectedParentHash = blocks[blockNum].expectedHash;
            ++matchedBlocks;
        } else {
            Log("[KVStore Lookup]   ✗ Parent chain mismatch! Expected parent=" + std::to_string(expectedParentHash) 
                      + " but got=" + std::to_string(result.parentHash));
            Log("[KVStore Lookup]   Breaking lookup chain.");
            break;
        }
    }
    
    Log("[KVStore Lookup] Lookup complete: matched " + std::to_string(matchedBlocks) + " blocks");
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    Log("[KVStore Lookup] ⏱️  Lookup took " + std::to_string(duration) + "ms for " + std::to_string(numFullBlocks) + " blocks");

    // Return matched token length instead of matched blocks
    int matchedTokenLength = matchedBlocks * static_cast<int>(blockSize);
    return std::make_tuple(matchedTokenLength, lastHash);
}

// Explicit instantiation for TokenIterator = std::vector<Token>::const_iterator
template std::tuple<int, hash_t> AzureStorageKVStoreLib::Lookup<std::vector<Token>::const_iterator>(
    const std::string&,
    const std::string&,
    std::vector<Token>::const_iterator,
    std::vector<Token>::const_iterator,
    const std::vector<hash_t>&
) const;

std::future<void> AzureStorageKVStoreLib::WriteAsync(const PromptChunk& chunk) {
    return std::async(std::launch::async, [this, chunk]() {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        std::string blobName = EncodeTokensToBlobName(chunk.tokens.cbegin(), chunk.tokens.cend());
        /*
        hash_t blockHash = 0;
        for (auto t : chunk.tokens) blockHash = blockHash * 31 + std::hash<Token>{}(t);
        std::string bloomKey = std::to_string(blockHash);
        */

        Log("[KVStore Write] Writing blob:");
        Log("[KVStore Write]   BlobName: " + blobName);
        Log("[KVStore Write]   Hash: " + std::to_string(chunk.hash));
        Log("[KVStore Write]   ParentHash: " + std::to_string(chunk.parentHash));
        Log("[KVStore Write]   PartitionKey: " + chunk.partitionKey);
        Log("[KVStore Write]   Buffer size: " + std::to_string(chunk.bufferSize) + " bytes");

        Azure::Storage::Blobs::UploadBlockBlobFromOptions options;
        options.Metadata["partitionKey"] = chunk.partitionKey;
        options.Metadata["parentHash"] = std::to_string(chunk.parentHash);
		options.Metadata["hash"] = std::to_string(chunk.hash);

        try {
            blobContainerClient_->GetBlobClient(blobName)
                .AsBlockBlobClient()
                .UploadFrom(chunk.buffer.data(), chunk.bufferSize, options);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
            Log("[KVStore Write]   ✓ Write successful! (" + std::to_string(duration) + "ms)");
            // Add blob name to bloom filter after successful write
            // blobNameFilter_->Add(bloomKey);
        } catch (const Azure::Storage::StorageException& ex) {
            // Log and assert on failure
            Log("[KVStore Write]   ✗ Azure StorageException during WriteAsync: " + std::string(ex.what()));
            assert(false && "Azure StorageException during WriteAsync");
        }
    });
}

template<typename TokenIterator>
std::future<std::pair<bool, PromptChunk>> AzureStorageKVStoreLib::ReadAsync(TokenIterator begin, TokenIterator end) const {
    return std::async(std::launch::async, [this, begin, end]() -> std::pair<bool, PromptChunk> {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        std::string blobName = EncodeTokensToBlobName(begin, end);
        try {
            auto blobClient = blobContainerClient_->GetBlobClient(blobName);
            
            // Download includes metadata, so we can get everything in one call
            auto downloadResponse = blobClient.Download();
            
            // Extract metadata from download response
            std::string partitionKey;
            hash_t parentHash = 0;
            hash_t hash = 0;
            
            const auto& metadata = downloadResponse.Value.Details.Metadata;
            auto metaIt = metadata.find("partitionKey");
            if (metaIt != metadata.end()) {
                partitionKey = metaIt->second;
            }
            auto parentIt = metadata.find("parentHash");
            if (parentIt != metadata.end()) {
                parentHash = std::stoull(parentIt->second);
            }
            auto hashIt = metadata.find("hash");
            if (hashIt != metadata.end()) {
                hash = std::stoull(hashIt->second);
            }
            
            // Read blob content
            auto& bodyStream = downloadResponse.Value.BodyStream;
            std::vector<uint8_t> buffer;
            buffer.resize(static_cast<size_t>(downloadResponse.Value.BlobSize));
            bodyStream->Read(buffer.data(), buffer.size());
            
            PromptChunk chunk;
            chunk.partitionKey = partitionKey;
            chunk.parentHash = parentHash;
            chunk.hash = hash;
            chunk.buffer = buffer;
            chunk.bufferSize = buffer.size();
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
            Log("[KVStore Read] ✓ Read successful! (" + std::to_string(duration) + "ms)");
            
            return {true, chunk};
        } catch (const Azure::Storage::StorageException& ex) {
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
            Log("[KVStore Read] ✗ Read failed: " + std::string(ex.what()) + " (" + std::to_string(duration) + "ms)");
            return {false, PromptChunk()};
        }
    });
}

// Explicit instantiation for TokenIterator = std::vector<Token>::const_iterator
template std::future<std::pair<bool, PromptChunk>> AzureStorageKVStoreLib::ReadAsync<std::vector<Token>::const_iterator>(
    std::vector<Token>::const_iterator,
    std::vector<Token>::const_iterator
) const;
