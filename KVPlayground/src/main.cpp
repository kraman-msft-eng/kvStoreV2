#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <ctime>
#include <future>
#include <chrono>
#include <thread>
#include <mutex>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif
#include "AzureStorageKVStoreLibV2.h"
#include "KVTypes.h"
#include <nlohmann/json.hpp>
#include <azure/core/diagnostics/logger.hpp>

using json = nlohmann::json;

// Global binary chunk buffer loaded from chunk.bin
std::vector<uint8_t> g_binaryChunk;

// Precomputed prompt data
struct PrecomputedPrompt {
    std::string text;
    std::vector<Token> tokens;
    int tokenCount;
};

// Performance statistics for operations
struct OperationStats {
    std::vector<int64_t> lookupTimes;  // microseconds
    std::vector<int64_t> readTimes;    // microseconds
    std::vector<int64_t> writeTimes;   // microseconds
    
    void addLookupTime(int64_t us) { lookupTimes.push_back(us); }
    void addReadTime(int64_t us) { readTimes.push_back(us); }
    void addWriteTime(int64_t us) { writeTimes.push_back(us); }
    
    int64_t getPercentile(const std::vector<int64_t>& data, double percentile) const {
        if (data.empty()) return 0;
        auto sorted = data;
        std::sort(sorted.begin(), sorted.end());
        size_t index = static_cast<size_t>(data.size() * percentile / 100.0);
        if (index >= data.size()) index = data.size() - 1;
        return sorted[index];
    }
    
    void merge(const OperationStats& other) {
        lookupTimes.insert(lookupTimes.end(), other.lookupTimes.begin(), other.lookupTimes.end());
        readTimes.insert(readTimes.end(), other.readTimes.begin(), other.readTimes.end());
        writeTimes.insert(writeTimes.end(), other.writeTimes.begin(), other.writeTimes.end());
    }
};

// Cache statistics
struct CacheStats {
    int totalTokensProcessed = 0;
    int tokensFromCache = 0;
    int tokensComputed = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
    
    // Validation tracking
    int validationAttempts = 0;
    int validationSuccesses = 0;
    int validationFailures = 0;
    int tokensMissingFromCache = 0;
    int tokensFailedToRead = 0;
    int tokensMismatched = 0;
    
    void reset() {
        totalTokensProcessed = 0;
        tokensFromCache = 0;
        tokensComputed = 0;
        cacheHits = 0;
        cacheMisses = 0;
        validationAttempts = 0;
        validationSuccesses = 0;
        validationFailures = 0;
        tokensMissingFromCache = 0;
        tokensFailedToRead = 0;
        tokensMismatched = 0;
    }
    
    void display() const {
        std::cout << "\n=== Cache Statistics ===\n";
        std::cout << "Total tokens processed: " << totalTokensProcessed << "\n";
        std::cout << "Tokens from cache: " << tokensFromCache 
                  << " (" << (totalTokensProcessed > 0 ? (tokensFromCache * 100.0 / totalTokensProcessed) : 0) << "%)\n";
        std::cout << "Tokens computed: " << tokensComputed 
                  << " (" << (totalTokensProcessed > 0 ? (tokensComputed * 100.0 / totalTokensProcessed) : 0) << "%)\n";
        std::cout << "Cache hits: " << cacheHits << "\n";
        std::cout << "Cache misses: " << cacheMisses << "\n";
        std::cout << "\n=== Cache Validation ===\n";
        std::cout << "Validation attempts: " << validationAttempts << "\n";
        std::cout << "Validation successes: " << validationSuccesses << "\n";
        std::cout << "Validation failures: " << validationFailures << "\n";
        if (validationFailures > 0) {
            std::cout << "\n--- Validation Failure Details ---\n";
            std::cout << "Tokens missing from cache: " << tokensMissingFromCache << "\n";
            std::cout << "Tokens failed to read: " << tokensFailedToRead << "\n";
            std::cout << "Tokens with mismatch: " << tokensMismatched << "\n";
        }
        std::cout << "========================\n";
    }
};

// Token buffer manager for 128-token blocks
struct TokenBufferManager {
    static constexpr size_t BLOCK_SIZE = 128;
    
    std::vector<std::vector<Token>> completedBlocks;  // Fully filled 128-token blocks that have been written
    std::vector<Token> currentBlock;                  // Current incomplete block being filled
    size_t totalTokensWritten = 0;                    // Total tokens written to cache
    size_t totalTokensProcessed = 0;                  // Total tokens processed (written + in buffer)
    hash_t lastWrittenHash = 0;                       // Hash of the last written block (for parent chaining)
    std::vector<hash_t> writtenBlockHashes;           // Hashes of all written blocks (for lookup)
    std::vector<Token> allWrittenTokens;              // All tokens that have been written to cache (for validation)
    
    // Add tokens to the buffer
    void addTokens(const std::vector<Token>& tokens) {
        for (const auto& token : tokens) {
            currentBlock.push_back(token);
            totalTokensProcessed++;  // Track all processed tokens
            
            // If current block is full, move it to completed blocks
            if (currentBlock.size() >= BLOCK_SIZE) {
                completedBlocks.push_back(currentBlock);
                currentBlock.clear();
            }
        }
    }
    
    // Get number of complete blocks ready to write
    size_t getCompleteBlockCount() const {
        return completedBlocks.size();
    }
    
    // Get total tokens (completed blocks + current partial block)
    size_t getTotalTokens() const {
        return (completedBlocks.size() * BLOCK_SIZE) + currentBlock.size();
    }
    
    // Flush completed blocks to cache
    void flushCompletedBlocks(AzureStorageKVStoreLibV2& kvStore, const std::string& partitionKey, const std::string& completionId, OperationStats& opStats, bool verbose = true) {
        if (completedBlocks.empty()) {
            if (verbose) {
                std::cout << "[Buffer] No complete blocks to flush\n";
            }
            return;
        }
        
        if (verbose) {
            std::cout << "\n[Buffer Flush] Flushing " << completedBlocks.size() << " block(s) of " 
                      << BLOCK_SIZE << " tokens each\n";
        }
        
        std::vector<std::future<void>> writeFutures;
        
        hash_t currentParentHash = lastWrittenHash;  // Start with last written hash from previous flush
        
        for (size_t i = 0; i < completedBlocks.size(); ++i) {
            const auto& block = completedBlocks[i];
            
            if (verbose) {
                std::cout << "[Buffer Flush] Block " << (i + 1) << "/" << completedBlocks.size() 
                          << ": tokens [" << totalTokensWritten << ".." << (totalTokensWritten + BLOCK_SIZE - 1) << "]\n";
                std::cout << "[Buffer Flush]   First 10: [";
                for (size_t j = 0; j < std::min(size_t(10), block.size()); ++j) {
                    std::cout << block[j];
                    if (j < 9 && j < block.size() - 1) std::cout << ", ";
                }
                std::cout << "...]\n";
                std::cout << "[Buffer Flush]   Last 5: [...";
                for (size_t j = std::max(size_t(0), block.size() - 5); j < block.size(); ++j) {
                    std::cout << block[j];
                    if (j < block.size() - 1) std::cout << ", ";
                }
                std::cout << "]\n";
                std::cout << "[Buffer Flush]   PartitionKey: " << partitionKey << "\n";
            }
            
            // Compute hash for this block (order-sensitive, like test_kvstore.cpp)
            hash_t combinedHash = 0;
            for (const auto& token : block) {
                combinedHash = combinedHash * 31 + std::hash<Token>{}(token);
            }
            
            // Set parentHash: use the hash from the previous block in this flush
            hash_t parentHash = currentParentHash;
            
            if (verbose) {
                std::cout << "[Buffer Flush]   Hash: " << combinedHash << "\n";
                std::cout << "[Buffer Flush]   ParentHash: " << parentHash << "\n";
            }
            
            // Use loaded binary chunk as buffer (should be exactly 1.2MB)
            std::vector<uint8_t> dummyBuffer;
            if (!g_binaryChunk.empty()) {
                dummyBuffer = g_binaryChunk;  // Direct copy since chunk.bin is 1.2MB
            } else {
                // Fallback to empty buffer if chunk.bin not loaded
                dummyBuffer.resize(1.2 * 1024 * 1024);
            }
            
            PromptChunk chunk;
            chunk.partitionKey = partitionKey;
            chunk.tokens = block;
            chunk.buffer = dummyBuffer;
            chunk.bufferSize = dummyBuffer.size();
            chunk.parentHash = parentHash;
            chunk.hash = combinedHash;
            chunk.completionId = completionId;
            
            writeFutures.push_back(kvStore.WriteAsync(chunk));
            
            // Save hash for lookup operations
            writtenBlockHashes.push_back(combinedHash);
            
            // Save the actual tokens written (for validation)
            allWrittenTokens.insert(allWrittenTokens.end(), block.begin(), block.end());
            
            // Update for next block in this flush AND for next flush call
            currentParentHash = combinedHash;
            lastWrittenHash = combinedHash;
            totalTokensWritten += BLOCK_SIZE;
        }
        
        // Wait for all writes to complete
        if (verbose) {
            std::cout << "[Buffer Flush] Waiting for " << writeFutures.size() << " write(s) to complete...\n";
        }
        try {
            for (size_t i = 0; i < writeFutures.size(); ++i) {
                auto writeStart = std::chrono::high_resolution_clock::now();
                writeFutures[i].get();
                auto writeEnd = std::chrono::high_resolution_clock::now();
                opStats.addWriteTime(std::chrono::duration_cast<std::chrono::microseconds>(writeEnd - writeStart).count());
                if (verbose) {
                    std::cout << "[Buffer Flush] Block " << (i + 1) << " write completed\n";
                }
            }
            if (verbose) {
                std::cout << "[Buffer Flush] ✓ Successfully wrote " << (completedBlocks.size() * BLOCK_SIZE) 
                          << " tokens to cache\n";
            }
            
            // Clear completed blocks after writing
            completedBlocks.clear();
        } catch (const std::exception& e) {
            std::cerr << "[Buffer Flush] ✗ ERROR during write: " << e.what() << "\n";
            std::cerr << "[Buffer Flush] This usually means the KVStore is not properly initialized\n";
            std::cerr << "[Buffer Flush] Check Azure Storage connection string and container name\n";
            // Don't clear blocks - keep them for potential retry
            throw; // Re-throw to let caller know
        }
    }
    
    void displayStatus() const {
        std::cout << "[Buffer Status] Total processed: " << totalTokensProcessed << " tokens\n";
        std::cout << "[Buffer Status] Written to cache: " << totalTokensWritten << " tokens in " 
                  << (totalTokensWritten / BLOCK_SIZE) << " blocks\n";
        std::cout << "[Buffer Status] Pending complete blocks: " << completedBlocks.size() << "\n";
        std::cout << "[Buffer Status] Current incomplete block: " << currentBlock.size() << "/" 
                  << BLOCK_SIZE << " tokens\n";
    }
};

// Helper to call Python tiktoken for accurate tokenization
struct TokenizationResult {
    int tokenCount;
    std::vector<int> tokenIds;
    std::vector<std::string> tokenStrings;
    bool success;
    std::string error;
};

// Semaphore to limit concurrent Python tokenizer calls (avoid "pipe" errors with high concurrency)
class TokenizerSemaphore {
public:
    class Guard { public: Guard() {} }; // No-op guard for non-Windows simplified implementation
#ifdef _WIN32
private:
    static std::mutex mutex;
    static std::condition_variable cv;
    static int activeCount;
    static constexpr int MAX_CONCURRENT = 10; // Limit concurrent Python processes
public:
    static void Acquire() {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, []{ return activeCount < MAX_CONCURRENT; });
        activeCount++;
    }
    static void Release() {
        std::unique_lock<std::mutex> lock(mutex);
        activeCount--;
        cv.notify_one();
    }
#endif
};

// Load precomputed tokens from JSON file
std::vector<PrecomputedPrompt> loadPrecomputedTokens(const std::string& filename) {
    std::vector<PrecomputedPrompt> prompts;
    
    std::ifstream ifs(filename);
    if (!ifs) {
        std::cerr << "ERROR: Could not open " << filename << "\n";
        std::cerr << "Please run: python precompute_tokens.py conversation_template.txt conversation_tokens.json\n";
        return prompts;
    }
    
    json j;
    ifs >> j;
    
    for (const auto& item : j["prompts"]) {
        PrecomputedPrompt prompt;
        prompt.text = item["text"];
        prompt.tokenCount = item["token_count"];
        for (const auto& token : item["tokens"]) {
            prompt.tokens.push_back(static_cast<Token>(token.get<int>()));
        }
        prompts.push_back(prompt);
    }
    
    std::cout << "✓ Loaded " << prompts.size() << " precomputed prompts\n";
    return prompts;
}

// Generate synthetic agent response tokens with unique ID tokens at BOTH start and end
// Uses 64-bit uniqueRunId (timestamp + runId) to ensure complete uniqueness across concurrent runs
// Pads both beginning and end to handle block boundary splits
std::vector<Token> generateSyntheticResponse(uint64_t uniqueRunId, int threadId, int turnNumber, int baseResponseSize = 50) {
    std::vector<Token> tokens;
    
    // Add PREFIX tokens based on uniqueRunId (split into high/low 32 bits) to ensure uniqueness
    tokens.push_back(static_cast<Token>((uniqueRunId >> 32) & 0xFFFFFFFF)); // High 32 bits of unique run ID
    tokens.push_back(static_cast<Token>(uniqueRunId & 0xFFFFFFFF));          // Low 32 bits of unique run ID
    tokens.push_back(static_cast<Token>(20000 + threadId));                   // Thread ID marker  
    tokens.push_back(static_cast<Token>(30000 + turnNumber));                 // Turn number marker
    
    // Add synthetic response tokens - each token incorporates uniqueRunId to ensure total uniqueness
    for (int i = 0; i < baseResponseSize - 8; i++) { // Reserve 8 tokens total (4 prefix + 4 suffix)
        // Generate tokens that incorporate bits of uniqueRunId for guaranteed uniqueness
        Token token = static_cast<Token>(1000 + i);
        
        // Mix in different parts of uniqueRunId based on position
        if (i % 5 == 0) {
            token += static_cast<Token>((uniqueRunId >> 16) & 0xFFFF);
        } else if (i % 5 == 1) {
            token += static_cast<Token>((uniqueRunId >> 8) & 0xFFFF);
        } else if (i % 5 == 2) {
            token += turnNumber * 10 + static_cast<Token>(uniqueRunId & 0xFF);
        } else if (i % 5 == 3) {
            token += static_cast<Token>((uniqueRunId >> 24) & 0xFF) * 7;
        } else {
            token += (turnNumber + i) * 3 + static_cast<Token>((uniqueRunId >> 12) & 0xF);
        }
        
        tokens.push_back(token);
    }
    
    // Add SUFFIX tokens based on uniqueRunId to ensure uniqueness even if block splits the response
    tokens.push_back(static_cast<Token>(40000 + turnNumber));                 // Turn number marker (suffix)
    tokens.push_back(static_cast<Token>(50000 + threadId));                   // Thread ID marker (suffix)
    tokens.push_back(static_cast<Token>(uniqueRunId & 0xFFFFFFFF));          // Low 32 bits of unique run ID (suffix)
    tokens.push_back(static_cast<Token>((uniqueRunId >> 32) & 0xFFFFFFFF)); // High 32 bits of unique run ID (suffix)
    
    return tokens;
}

#ifdef _WIN32
std::mutex TokenizerSemaphore::mutex;
std::condition_variable TokenizerSemaphore::cv;
int TokenizerSemaphore::activeCount = 0;
#endif

#ifdef _WIN32
TokenizationResult callPythonTokenizer(const std::string& text) {
    // Original Windows-specific implementation retained (truncated for Linux portability work)
    TokenizationResult result;
    result.success = false;
    result.error = "Windows-only tokenizer path not invoked in current build path";
    return result;
}
#else
TokenizationResult callPythonTokenizer(const std::string& text) {
    TokenizationResult result;
    result.success = false;
    result.error = "Tokenizer not implemented on Linux build. Use precomputed tokens JSON.";
    return result;
}
#endif

void displayTokens(const std::vector<int>& tokens) {
    std::cout << "Token IDs (" << tokens.size() << "): [";
    for (size_t i = 0; i < std::min(tokens.size(), size_t(50)); ++i) {
        std::cout << tokens[i];
        if (i < tokens.size() - 1) {
            std::cout << ", ";
        }
    }
    if (tokens.size() > 50) {
        std::cout << " ... (" << (tokens.size() - 50) << " more)";
    }
    std::cout << "]\n";
}

// Validate and retrieve tokens from cache
bool validateCacheRetrieval(
    AzureStorageKVStoreLibV2& kvStore,
    const std::vector<Token>& expectedTokens,
    const std::vector<hash_t>& blockHashes,
    const std::string& partitionKey,
    const std::string& completionId,
    CacheStats& stats,
    OperationStats& opStats,
    bool verbose = true)
{
    if (expectedTokens.empty()) {
        return true;
    }
    
    stats.validationAttempts++;
    bool hasErrors = false;
    
    if (verbose) {
        std::cout << "\n[Cache Validation] Checking if " << expectedTokens.size() 
                  << " written tokens can be retrieved...\n";
        std::cout << "[Cache Validation] PartitionKey: " << partitionKey << "\n";
        std::cout << "[Cache Validation] CompletionId: " << completionId << "\n";
        std::cout << "[Cache Validation] Block hashes count: " << blockHashes.size() << "\n";
        std::cout << "[Cache Validation] First 10 expected: [";
        for (size_t i = 0; i < std::min(size_t(10), expectedTokens.size()); ++i) {
            std::cout << expectedTokens[i];
            if (i < 9 && i < expectedTokens.size() - 1) std::cout << ", ";
        }
        std::cout << "...]\n";
        std::cout << "[Cache Validation] Last 5 expected: [...";
        for (size_t i = std::max(size_t(0), expectedTokens.size() - 5); i < expectedTokens.size(); ++i) {
            std::cout << expectedTokens[i];
            if (i < expectedTokens.size() - 1) std::cout << ", ";
        }
        std::cout << "]\n";
        
        // Print all block hashes and their parent relationships
        std::cout << "[Cache Validation] Block hash details for lookup:\n";
        hash_t lookupParentHash = 0;
        for (size_t i = 0; i < blockHashes.size(); ++i) {
            std::cout << "[Cache Validation]   Block " << i << ":\n";
            std::cout << "[Cache Validation]     Hash: " << blockHashes[i] << "\n";
            std::cout << "[Cache Validation]     ParentHash: " << lookupParentHash << "\n";
            lookupParentHash = blockHashes[i]; // Next block's parent is this block's hash
        }
    }
    
    // Perform lookup with precomputed hashes (V2 API)
    auto lookupStart = std::chrono::high_resolution_clock::now();
    auto lookupResult = kvStore.Lookup(
        partitionKey,
        completionId,
        expectedTokens.begin(),
        expectedTokens.end(),
        blockHashes  // Use the hashes that were computed during write
    );
    auto lookupEnd = std::chrono::high_resolution_clock::now();
    opStats.addLookupTime(std::chrono::duration_cast<std::chrono::microseconds>(lookupEnd - lookupStart).count());
    
    int matchedLength = lookupResult.cachedBlocks * 128;
    hash_t lastHash = lookupResult.lastHash;
    
    if (lookupResult.cachedBlocks == 0) {
        std::cerr << "\n[Cache Validation] ✗ ERROR: No tokens found in cache!\n";
        std::cerr << "[Cache Validation] Expected to find " << expectedTokens.size() << " tokens\n";
        stats.tokensMissingFromCache += expectedTokens.size();
        stats.validationFailures++;
        return false;
    }
    
    // Check if we found ALL expected tokens
    if (matchedLength != static_cast<int>(expectedTokens.size())) {
        std::cerr << "\n[Cache Validation] ✗ ERROR: Partial match in cache!\n";
        std::cerr << "[Cache Validation] Expected: " << expectedTokens.size() << " tokens\n";
        std::cerr << "[Cache Validation] Found: " << matchedLength << " tokens\n";
        std::cerr << "[Cache Validation] Missing: " << (expectedTokens.size() - matchedLength) << " tokens\n";
        stats.tokensMissingFromCache += (expectedTokens.size() - matchedLength);
        hasErrors = true;
    } else if (verbose) {
        std::cout << "[Cache Validation] ✓ Found " << matchedLength << " tokens in cache\n";
        std::cout << "[Cache Validation] Last matched hash: " << lastHash << "\n";
    }
    
    // Read cached content block by block using V2 API with locations
    if (verbose) {
        std::cout << "[Cache Validation] Reading " << lookupResult.locations.size() << " blocks...\n";
    }
    
    std::vector<std::future<std::pair<bool, PromptChunk>>> readFutures;
    for (size_t i = 0; i < lookupResult.locations.size(); ++i) {
        const auto& location = lookupResult.locations[i];
        size_t start = i * 128;
        size_t end = std::min(start + 128, expectedTokens.size());
        if (verbose) {
            std::cout << "[Cache Validation] Queuing read for block " << i << " (tokens " << start << ".." << (end-1) << ")\n";
            std::cout << "[Cache Validation]   Block " << i << " location: " << location.location << "\n";
            std::cout << "[Cache Validation]   Block " << i << " first 5: [";
            for (size_t j = 0; j < std::min(size_t(5), end - start); ++j) {
                std::cout << expectedTokens[start + j];
                if (j < 4) std::cout << ", ";
            }
            std::cout << "...]\n";
        }
        // Read using V2 API with location
        readFutures.push_back(kvStore.ReadAsync(location.location));
    }
    
    // Verify all blocks
    for (size_t i = 0; i < lookupResult.locations.size(); ++i) {
        auto readStart = std::chrono::high_resolution_clock::now();
        auto [success, chunk] = readFutures[i].get();
        auto readEnd = std::chrono::high_resolution_clock::now();
        opStats.addReadTime(std::chrono::duration_cast<std::chrono::microseconds>(readEnd - readStart).count());
        if (verbose) {
            std::cout << "[Cache Validation] Block " << i << " read result: success=" << success 
                      << ", bufferSize=" << chunk.bufferSize << "\n";
        }
        if (!success || chunk.bufferSize == 0) {
            std::cerr << "\n[Cache Validation] ✗ ERROR: Could not read block " << i << " from cache!\n";
            std::cerr << "[Cache Validation] Read success: " << success << "\n";
            std::cerr << "[Cache Validation] Buffer size: " << chunk.bufferSize << "\n";
            stats.tokensFailedToRead += 128;
            hasErrors = true;
            continue;
        }
        
        // Verify tokens match for this block
        size_t start = i * 128;
        for (size_t j = 0; j < chunk.tokens.size(); ++j) {
            if (chunk.tokens[j] != expectedTokens[start + j]) {
                std::cerr << "\n[Cache Validation] ✗ ERROR: Token MISMATCH in block " << i << " at position " << j << "\n";
                std::cerr << "[Cache Validation] Expected: " << expectedTokens[start + j] << "\n";
                std::cerr << "[Cache Validation] Got: " << chunk.tokens[j] << "\n";
                stats.tokensMismatched++;
                hasErrors = true;
                break; // Only report first mismatch per block
            }
        }
    }
    
    // Update stats
    if (hasErrors) {
        std::cerr << "\n[Cache Validation] ✗ VALIDATION FAILED - See errors above\n";
        stats.validationFailures++;
        return false;
    } else {
        if (verbose) {
            std::cout << "[Cache Validation] ✓✓✓ SUCCESS ✓✓✓\n";
            std::cout << "[Cache Validation] All " << matchedLength 
                      << " tokens retrieved and verified across " << lookupResult.locations.size() << " blocks!\n";
        }
        stats.tokensFromCache += matchedLength;
        stats.cacheHits++;
        stats.validationSuccesses++;
        return true;
    }
}

// Process tokens with KVStore: lookup cache, compute missing
std::vector<Token> processWithCache(
    AzureStorageKVStoreLibV2& kvStore,
    const std::vector<Token>& promptTokens,
    const std::string& partitionKey,
    const std::string& completionId,
    size_t alreadyWrittenCount,
    CacheStats& stats)
{
    std::vector<Token> result;
    
    std::cout << "\n[Cache Lookup] Checking " << promptTokens.size() << " tokens...\n";
    std::cout << "[Cache Lookup] First 10 tokens in query: [";
    for (size_t i = 0; i < std::min(size_t(10), promptTokens.size()); ++i) {
        std::cout << promptTokens[i];
        if (i < 9 && i < promptTokens.size() - 1) std::cout << ", ";
    }
    std::cout << "...]\n";
    
    // Prepare for lookup
    std::vector<hash_t> precomputedHashes;
    // For simplicity, using empty hashes (library will compute internally)
    
    // Perform lookup (V2 API)
    auto lookupResult = kvStore.Lookup(
        partitionKey,
        completionId,
        promptTokens.begin(),
        promptTokens.end(),
        precomputedHashes
    );
    
    int matchedLength = lookupResult.cachedBlocks * 128;
    hash_t lastHash = lookupResult.lastHash;
    
    stats.totalTokensProcessed += promptTokens.size();
    
    if (matchedLength > 0) {
        std::cout << "[Cache Lookup] ✓ Found " << matchedLength << " cached tokens (out of " 
                  << promptTokens.size() << ")!\n";
        stats.tokensFromCache += matchedLength;
        stats.cacheHits++;
        
        // Read cached content using first location (V2 API)
        auto readFuture = kvStore.ReadAsync(lookupResult.locations[0].location, completionId);
        auto [success, chunk] = readFuture.get();
        
        if (success && !chunk.tokens.empty()) {
            std::cout << "[Cache Read] ✓ Retrieved " << chunk.tokens.size() << " tokens from cache\n";
            std::cout << "[Cache Read] First 10 retrieved tokens: [";
            for (size_t i = 0; i < std::min(size_t(10), chunk.tokens.size()); ++i) {
                std::cout << chunk.tokens[i];
                if (i < 9 && i < chunk.tokens.size() - 1) std::cout << ", ";
            }
            std::cout << "...]\n";
            
            // Verify tokens match
            bool tokensMatch = true;
            for (size_t i = 0; i < std::min(chunk.tokens.size(), size_t(matchedLength)); ++i) {
                if (chunk.tokens[i] != promptTokens[i]) {
                    tokensMatch = false;
                    std::cout << "[Cache Read] ✗ MISMATCH at position " << i 
                              << ": expected " << promptTokens[i] 
                              << ", got " << chunk.tokens[i] << "\n";
                    break;
                }
            }
            if (tokensMatch) {
                std::cout << "[Cache Read] ✓ Token verification passed - all tokens match!\n";
            }
            
            result.insert(result.end(), chunk.tokens.begin(), chunk.tokens.end());
        }
    } else {
        std::cout << "[Cache Lookup] ✗ No cached tokens found (cache miss)\n";
    }
    
    // Compute remaining tokens
    size_t remainingTokens = promptTokens.size() - matchedLength;
    if (remainingTokens > 0) {
        std::cout << "[Compute] Computing " << remainingTokens << " new tokens (starting from position " 
                  << matchedLength << ")\n";
        stats.tokensComputed += remainingTokens;
        if (matchedLength == 0) {
            stats.cacheMisses++;
        }
        
        // Add the remaining prompt tokens
        result.insert(result.end(), 
                     promptTokens.begin() + matchedLength,
                     promptTokens.end());
    }
    
    return result;
}

// Load conversation prompts from a file
std::vector<std::string> loadConversationFromFile(const std::string& filename) {
    std::vector<std::string> prompts;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        return prompts;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Trim leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            prompts.push_back(line.substr(start, end - start + 1));
        }
    }
    
    return prompts;
}

// Singleton KVStore instance - shared across all concurrent runs
class KVStoreManager {
private:
    static std::unique_ptr<AzureStorageKVStoreLibV2> instance;
    static std::mutex initMutex;
    static bool initialized;
    static bool verboseLogging;
    static std::string storageUrl;
    static std::string container;

public:
    static AzureStorageKVStoreLibV2& GetInstance(LogLevel logLevel = LogLevel::Error, 
                                                 const std::string& azureUrl = "", 
                                                 const std::string& containerName = "") {
        std::lock_guard<std::mutex> lock(initMutex);
        
        if (!initialized) {
            instance = std::make_unique<AzureStorageKVStoreLibV2>();
            
            // Use provided values or fall back to defaults
            storageUrl = !azureUrl.empty() ? azureUrl : "https://azureaoaikv.blob.core.windows.net/";
            container = !containerName.empty() ? containerName : "gpt41-promptcache";
            
            // Set up logging callback with log levels
            instance->SetLogCallback([](LogLevel level, const std::string& msg) {
                const char* levelStr = "";
                switch (level) {
                    case LogLevel::Error: levelStr = "[ERROR] "; break;
                    case LogLevel::Information: levelStr = "[INFO] "; break;
                    case LogLevel::Verbose: levelStr = "[VERBOSE] "; break;
                }
                std::cout << "[KVStore] " << levelStr << msg << "\n";
            });
            
            // Set log level
            verboseLogging = (logLevel == LogLevel::Verbose);
            instance->SetLogLevel(logLevel);
            
            if (!instance->Initialize(storageUrl, container)) {
                throw std::runtime_error("Failed to initialize KVStore singleton!");
            }
            
            initialized = true;
            std::cout << "[KVStore] Singleton instance initialized\n";
            std::cout << "[KVStore] Storage: " << storageUrl << "\n";
            std::cout << "[KVStore] Container: " << container << "\n";
            if (verboseLogging) {
                std::cout << "[KVStore] Verbose logging ENABLED\n";
            }
        }
        
        return *instance;
    }
    
    static void Shutdown() {
        std::lock_guard<std::mutex> lock(initMutex);
        instance.reset();
        initialized = false;
    }
};

// Static member initialization
std::unique_ptr<AzureStorageKVStoreLibV2> KVStoreManager::instance = nullptr;
std::mutex KVStoreManager::initMutex;
bool KVStoreManager::initialized = false;
bool KVStoreManager::verboseLogging = false;
std::string KVStoreManager::storageUrl;
std::string KVStoreManager::container;

// Run a single conversation test - returns (status, OperationStats)
std::pair<int, OperationStats> runConversation(const std::vector<PrecomputedPrompt>& precomputedPrompts, int runId, 
                    const std::string& storageAccountUrl, const std::string& containerName,
                    bool verbose = false, LogLevel logLevel = LogLevel::Error) {
    // Create unique run identifier: timestamp (milliseconds since epoch) + processId + runId
    auto now = std::chrono::system_clock::now();
    auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    uint32_t processId = 0;
#ifdef _WIN32
    processId = GetCurrentProcessId();
#else
    processId = static_cast<uint32_t>(getpid());
#endif
    // Pack: 40 bits timestamp (supports ~34 years), 12 bits processId, 12 bits runId
    uint64_t uniqueRunId = ((timestamp_ms & 0xFFFFFFFFFFULL) << 24) | ((processId & 0xFFF) << 12) | (runId & 0xFFF);
    
    OperationStats opStats;  // Collect performance statistics
    
    if (verbose) {
        std::cout << "\n========================================\n";
        std::cout << "=== Conversation Run #" << runId << " (Unique ID: " << uniqueRunId << ") ===\n";
        std::cout << "========================================\n\n";
    }
    
    // Create KVStore instance (gRPC client)
    AzureStorageKVStoreLibV2 kvStore;
    kvStore.SetLogLevel(logLevel);
    if (!kvStore.Initialize(storageAccountUrl, containerName)) {
        std::cerr << "Failed to initialize KVStore gRPC client\n";
        return {1, opStats};
    }
    
    CacheStats stats;
    std::string partitionKey = "playground_session_run" + std::to_string(uniqueRunId);
    std::string completionId = std::to_string(runId);
    
    // Token buffer manager for 128-token blocks
    TokenBufferManager bufferManager;
    
    // Validate precomputed prompts
    if (precomputedPrompts.empty()) {
        std::cerr << "ERROR: No precomputed prompts provided\n";
        return {1, opStats};
    }
    if (verbose) {
        std::cout << "✓ Using " << precomputedPrompts.size() << " precomputed prompts\n\n";
    }
    
    // Main loop with conversation history
    int turnNumber = 0;
    std::vector<Token> conversationTokens; // Accumulate all tokens across turns
    
    for (const auto& prompt : precomputedPrompts) {
        turnNumber++;
        
        try {
            if (verbose) {
                std::cout << "\n========== Turn " << turnNumber << " ==========\n";
                std::cout << "\nUser: " << prompt.text << "\n";
            }
            
            // Step 1: Validate what we've written so far can be retrieved
            if (bufferManager.totalTokensWritten > 0) {
                // Use the actual tokens that were written, not re-tokenized conversation
                // Always validate cache, but only log details in verbose mode
                validateCacheRetrieval(kvStore, bufferManager.allWrittenTokens, bufferManager.writtenBlockHashes, partitionKey, completionId, stats, opStats, verbose);
            }
            
            // Step 2: Add user prompt tokens to conversation
            conversationTokens.insert(conversationTokens.end(), prompt.tokens.begin(), prompt.tokens.end());
            
            if (verbose) {
                std::cout << "\n--- Conversation After User Input ---\n";
                std::cout << "Total tokens: " << conversationTokens.size() << "\n";
            }
            
            // Step 3: Check if tokens are already in cache before writing (for first turn or when resuming)
            int cachedTokenCount = 0;
            if (bufferManager.totalTokensWritten == 0 && conversationTokens.size() >= 128) {
                if (verbose) {
                    std::cout << "\n[Cache Check] First write - checking if " << conversationTokens.size() 
                              << " tokens already exist in cache...\n";
                }
                
                // Compute hashes for the blocks we want to lookup
                std::vector<hash_t> lookupHashes;
                size_t numFullBlocks = conversationTokens.size() / 128;
                hash_t currentHash = 0;
                
                for (size_t blockNum = 0; blockNum < numFullBlocks; ++blockNum) {
                    currentHash = 0;
                    for (size_t i = blockNum * 128; i < (blockNum + 1) * 128; ++i) {
                        currentHash = currentHash * 31 + std::hash<Token>{}(conversationTokens[i]);
                    }
                    lookupHashes.push_back(currentHash);
                }
                
                auto lookupStart = std::chrono::high_resolution_clock::now();
                auto lookupResult = kvStore.Lookup(
                    partitionKey,
                    completionId,
                    conversationTokens.cbegin(),
                    conversationTokens.cend(),
                    lookupHashes
                );
                auto lookupEnd = std::chrono::high_resolution_clock::now();
                opStats.addLookupTime(std::chrono::duration_cast<std::chrono::microseconds>(lookupEnd - lookupStart).count());
                
                int matchedLength = lookupResult.cachedBlocks * 128;
                hash_t lastHash = lookupResult.lastHash;
                
                if (matchedLength > 0) {
                    cachedTokenCount = matchedLength;
                    if (verbose) {
                        std::cout << "[Cache Check] ✓ Found " << cachedTokenCount 
                                  << " tokens already in cache (will skip writing these)\n";
                    }
                    
                    // Update buffer manager to reflect what's already cached
                    // We need to mark these tokens as "processed" and "written"
                    bufferManager.totalTokensProcessed = cachedTokenCount;
                    bufferManager.totalTokensWritten = (cachedTokenCount / 128) * 128; // Only complete blocks
                    
                    // Store the cached tokens for validation
                    bufferManager.allWrittenTokens.insert(
                        bufferManager.allWrittenTokens.end(),
                        conversationTokens.begin(),
                        conversationTokens.begin() + bufferManager.totalTokensWritten
                    );
                    
                    // Store the hashes (only for matched blocks)
                    size_t numCachedBlocks = bufferManager.totalTokensWritten / 128;
                    for (size_t i = 0; i < numCachedBlocks; ++i) {
                        bufferManager.writtenBlockHashes.push_back(lookupHashes[i]);
                    }
                    bufferManager.lastWrittenHash = lastHash;
                    
                    if (verbose) {
                        std::cout << "[Cache Check] Skipping write for " << bufferManager.totalTokensWritten 
                                  << " cached tokens in " << numCachedBlocks << " blocks\n";
                    }
                }
            }
            
            // Step 4: Add new tokens to buffer (tokens beyond what we've processed)
            if (conversationTokens.size() > bufferManager.totalTokensProcessed) {
                std::vector<Token> newTokens(
                    conversationTokens.begin() + bufferManager.totalTokensProcessed,
                    conversationTokens.end()
                );
                if (verbose) {
                    std::cout << "\n[Buffer] Adding " << newTokens.size() << " new tokens from user input\n";
                }
                bufferManager.addTokens(newTokens);
                if (verbose) {
                    bufferManager.displayStatus();
                }
                
                // CRITICAL: Flush complete blocks BEFORE adding synthetic response
                // This ensures precomputed prompt blocks are isolated from unique agent tokens
                // allowing all concurrent runs to share the same prompt cache blocks
                if (bufferManager.getCompleteBlockCount() > 0) {
                    if (verbose) {
                        std::cout << "[Buffer] Flushing " << bufferManager.getCompleteBlockCount() 
                                  << " complete block(s) to isolate shared prompts from unique responses\n";
                    }
                    bufferManager.flushCompletedBlocks(kvStore, partitionKey, completionId, opStats, verbose);
                    if (verbose) {
                        bufferManager.displayStatus();
                    }
                }
            }
            
            // Step 5: Generate synthetic agent response tokens
            // Each response is unique based on uniqueRunId (timestamp+runId), turnNumber to prevent cache collisions
            auto agentTokens = generateSyntheticResponse(uniqueRunId, 0, turnNumber);
            conversationTokens.insert(conversationTokens.end(), agentTokens.begin(), agentTokens.end());
            
            if (verbose) {
                std::cout << "\n--- After Agent Response ---\n";
                std::cout << "Total tokens: " << conversationTokens.size() << "\n";
            }
            
            // Step 6: Add agent response tokens to buffer
            if (conversationTokens.size() > bufferManager.totalTokensProcessed) {
                std::vector<Token> newAgentTokens(
                    conversationTokens.begin() + bufferManager.totalTokensProcessed,
                    conversationTokens.end()
                );
                if (verbose) {
                    std::cout << "\n[Buffer] Adding " << newAgentTokens.size() << " new tokens from agent response\n";
                }
                bufferManager.addTokens(newAgentTokens);
                if (verbose) {
                    bufferManager.displayStatus();
                }
                
                // Flush if we have complete blocks
                if (bufferManager.getCompleteBlockCount() > 0) {
                    bufferManager.flushCompletedBlocks(kvStore, partitionKey, completionId, opStats, verbose);
                    if (verbose) {
                        bufferManager.displayStatus();
                    }
                }
            }
            
            // Display statistics only in verbose mode
            if (verbose) {
                std::cout << "\n";
                stats.display();
            }
            
        } catch (const std::exception& e) {
            std::cerr << "\n!!! ERROR in turn " << turnNumber << ": " << e.what() << "\n";
            std::cerr << "Continuing with next turn...\n";
        }
    }
    
    // Print final summary
    if (verbose) {
        std::cout << "\n=== Run #" << runId << " Completed ===\n";
        std::cout << "Total turns: " << turnNumber << "\n";
        std::cout << "Final conversation tokens: " << conversationTokens.size() << "\n";
        
        std::cout << "\n";
        bufferManager.displayStatus();
        std::cout << "\n";
        stats.display();
    }
    
    return {0, opStats};
}

int main(int argc, char* argv[]) {
    // Note: Using gRPC client - no need to disable Azure SDK logging
    // Azure::Core::Diagnostics::Logger::SetListener(nullptr);

    std::cout << "=== KV Playground - GPT-4 Tokenizer with Cache ===\n";
    std::cout << "Using precomputed tokens (cl100k_base encoding)\n";
    std::cout << "Powered by Azure KVStore with synthetic agent responses\n";
    
    // Load binary chunk file
    std::ifstream chunkFile("chunk.bin", std::ios::binary | std::ios::ate);
    if (chunkFile.is_open()) {
        std::streamsize size = chunkFile.tellg();
        chunkFile.seekg(0, std::ios::beg);
        g_binaryChunk.resize(size);
        if (chunkFile.read(reinterpret_cast<char*>(g_binaryChunk.data()), size)) {
            std::cout << "Loaded chunk.bin: " << size << " bytes\n";
        } else {
            std::cerr << "Warning: Failed to read chunk.bin, using empty buffers\n";
            g_binaryChunk.clear();
        }
        chunkFile.close();
    } else {
        std::cerr << "Warning: chunk.bin not found, using empty buffers\n";
    }
    
    // Parse command-line arguments
    std::string tokensFile;
    int iterations = 1;
    int concurrency = 1;
    bool verboseLogging = false;
    ;
    ;
    LogLevel logLevel = LogLevel::Error;  // Default to Error
    std::string storageAccountUrl;
    std::string containerName;
    
    // Check for named arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            verboseLogging = true;
            logLevel = LogLevel::Verbose;
        } else if ((arg == "--log-level" || arg == "-l") && i + 1 < argc) {
            std::string level = argv[++i];
            if (level == "error" || level == "Error" || level == "ERROR") {
                logLevel = LogLevel::Error;
            } else if (level == "info" || level == "information" || level == "Information" || level == "INFO") {
                logLevel = LogLevel::Information;
            } else if (level == "verbose" || level == "Verbose" || level == "VERBOSE") {
                logLevel = LogLevel::Verbose;
                verboseLogging = true;
            } else {
                std::cerr << "Warning: Unknown log level '" << level << "', using Information\n";
            }
        } else if ((arg == "--storage" || arg == "-s") && i + 1 < argc) {
            storageAccountUrl = argv[++i];
        } else if ((arg == "--container" || arg == "-c") && i + 1 < argc) {
            containerName = argv[++i];
        }
    }
    
    // Set defaults if not provided
    if (storageAccountUrl.empty()) {
        storageAccountUrl = "https://azureaoaikv.blob.core.windows.net/";
    }
    if (containerName.empty()) {
        containerName = "gpt41-promptcache";
    }
    
    // Get tokens file (first positional argument)
    if (argc > 1) {
        tokensFile = argv[1];
        // Skip if it's a flag
        if (tokensFile == "--verbose" || tokensFile == "-v" || 
            tokensFile == "--log-level" || tokensFile == "-l" ||
            tokensFile == "--storage" || tokensFile == "-s" ||
            tokensFile == "--container" || tokensFile == "-c" ||
            tokensFile == "--transport" || tokensFile == "-t") {
            tokensFile.clear();
        }
        
        // Check for optional iteration count
        if (argc > 2) {
            std::string arg2 = argv[2];
            if (arg2 != "--verbose" && arg2 != "-v" && 
                arg2 != "--log-level" && arg2 != "-l" &&
                arg2 != "--storage" && arg2 != "-s" &&
                arg2 != "--container" && arg2 != "-c" &&
                arg2 != "--transport" && arg2 != "-t") {
                iterations = std::atoi(argv[2]);
                if (iterations <= 0) iterations = 1;
            }
        }
        
        // Check for optional concurrency
        if (argc > 3) {
            std::string arg3 = argv[3];
            if (arg3 != "--verbose" && arg3 != "-v" && 
                arg3 != "--log-level" && arg3 != "-l" &&
                arg3 != "--storage" && arg3 != "-s" &&
                arg3 != "--container" && arg3 != "-c" &&
                arg3 != "--transport" && arg3 != "-t") {
                concurrency = std::atoi(argv[3]);
                if (concurrency <= 0) concurrency = 1;
                if (concurrency > 200) concurrency = 200; // Cap at 200 for safety
            }
        }
    }
    
    std::cout << "Azure Storage: " << storageAccountUrl << "\n";
    std::cout << "Container: " << containerName << "\n";
    
    if (tokensFile.empty()) {
        std::cerr << "\nERROR: No tokens file specified!\n\n";
        std::cout << "Usage:\n";
        std::cout << "  " << argv[0] << " <tokens_file.json> [iterations] [concurrency] [options]\n";
        std::cout << "\nOptions:\n";
        std::cout << "  --verbose, -v              Enable verbose logging (same as --log-level verbose)\n";
        std::cout << "  --log-level, -l <level>    Set log level: error, information, verbose (default: information)\n";
        std::cout << "  --storage, -s <url>        Azure Storage account URL (default: https://azureaoaikv.blob.core.windows.net/)\n";
        std::cout << "  --container, -c <name>     Container name (default: gpt41-promptcache)\n";
        std::cout << "\nExamples:\n";
        std::cout << "  " << argv[0] << " conversation_tokens.json 5 2 --verbose\n";
        std::cout << "  " << argv[0] << " conversation_tokens.json 5 2 --log-level error\n";
        std::cout << "  " << argv[0] << " conversation_tokens.json 10 5 -s https://myaccount.blob.core.windows.net/ -c mycontainer\n";
        std::cout << "\nTo generate tokens file:\n";
        std::cout << "  python precompute_tokens.py conversation_template.txt conversation_tokens.json\n";
        return 1;
    }
    
    std::cout << "Tokens File: " << tokensFile << "\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Concurrency: " << concurrency << "\n";
    
    const char* logLevelStr = "Information";
    if (logLevel == LogLevel::Error) logLevelStr = "Error";
    else if (logLevel == LogLevel::Verbose) logLevelStr = "Verbose";
    std::cout << "Log Level: " << logLevelStr << "\n";
    
    std::cout << "===================================================\n\n";
    
    // Load precomputed tokens
    auto precomputedPrompts = loadPrecomputedTokens(tokensFile);
    if (precomputedPrompts.empty()) {
        std::cerr << "ERROR: Failed to load precomputed tokens from " << tokensFile << "\n";
        return 1;
    }
    
    // Run with concurrency
    if (concurrency > 1) {
        std::cout << "Starting " << iterations << " iterations with concurrency " << concurrency << "...\n\n";
        
        std::vector<std::future<std::pair<int, OperationStats>>> futures;
        OperationStats aggregatedStats;
        int runId = 1;
        int completed = 0;
        int lastReportedMilestone = 0;
        
        while (completed < iterations) {
            // Launch up to 'concurrency' tasks
            while (futures.size() < static_cast<size_t>(concurrency) && runId <= iterations) {
                int currentRunId = runId++;
                LogLevel level = logLevel;
                std::string storage = storageAccountUrl;
                std::string container = containerName;
                auto prompts = precomputedPrompts; // Copy for thread safety
                futures.push_back(std::async(std::launch::async, [prompts, currentRunId, storage, container, level]() {
                    return runConversation(prompts, currentRunId, storage, container, false, level); // verbose output disabled for cleaner concurrent logs
                }));
            }
            
            // Wait for any to complete
            if (!futures.empty()) {
                auto it = futures.begin();
                while (it != futures.end()) {
                    if (it->wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
                        auto [result, opStats] = it->get();
                        if (result != 0) {
                            std::cerr << "ERROR: Run failed with code " << result << "\n";
                            return result;
                        }
                        aggregatedStats.merge(opStats);
                        completed++;
                        
                        // Report every 1000 completions
                        if (completed % 1000 == 0 || completed == iterations) {
                            int64_t lookupP50 = aggregatedStats.getPercentile(aggregatedStats.lookupTimes, 50);
                            int64_t lookupP90 = aggregatedStats.getPercentile(aggregatedStats.lookupTimes, 90);
                            int64_t readP50 = aggregatedStats.getPercentile(aggregatedStats.readTimes, 50);
                            int64_t readP90 = aggregatedStats.getPercentile(aggregatedStats.readTimes, 90);
                            int64_t writeP50 = aggregatedStats.getPercentile(aggregatedStats.writeTimes, 50);
                            int64_t writeP90 = aggregatedStats.getPercentile(aggregatedStats.writeTimes, 90);
                            
                            std::cout << "[Progress] Completed " << completed << "/" << iterations << " runs\n";
                            std::cout << "  Lookup: p50=" << (lookupP50 / 1000.0) << "ms, p90=" << (lookupP90 / 1000.0) << "ms\n";
                            std::cout << "  Read:   p50=" << (readP50 / 1000.0) << "ms, p90=" << (readP90 / 1000.0) << "ms\n";
                            std::cout << "  Write:  p50=" << (writeP50 / 1000.0) << "ms, p90=" << (writeP90 / 1000.0) << "ms\n";
                        }
                        
                        it = futures.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }
        
        std::cout << "\n=== All Runs Completed Successfully ===\n";
        std::cout << "Total runs: " << iterations << "\n";
        std::cout << "Concurrency: " << concurrency << "\n";
        
        // Cleanup singleton
        KVStoreManager::Shutdown();
        return 0;
    } else {
        // Sequential execution
        std::cout << "Starting " << iterations << " sequential iteration(s)...\n\n";
        for (int i = 1; i <= iterations; ++i) {
            auto [result, opStats] = runConversation(precomputedPrompts, i, storageAccountUrl, containerName, true, logLevel); // verbose output enabled for single runs
            if (result != 0) {
                std::cerr << "ERROR: Run " << i << " failed with code " << result << "\n";
                return result;
            }
            if (i < iterations) {
                std::cout << "\n[Progress] Completed " << i << "/" << iterations << " runs\n\n";
            }
        }
        std::cout << "\n=== All " << iterations << " Run(s) Completed Successfully ===\n";
        
        // Cleanup singleton
        KVStoreManager::Shutdown();
        return 0;
    }
}
