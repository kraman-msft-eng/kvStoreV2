#pragma once

#include <cstdint>
#include <vector>
#include <string>

// Type representing an inference token
using Token = int64_t;

// Type representing a hash value
using hash_t = uint64_t;

// Type representing a vector of hash values                
using HashVector = std::vector<hash_t>;

// Struct representing a prompt chunk
struct PromptChunk {
    hash_t hash;
    std::string partitionKey;
    hash_t parentHash;
    std::vector<uint8_t> buffer; // binary buffer
    size_t bufferSize;           // size of the buffer
    std::vector<Token> tokens;   // vector of tokens for this chunk
    std::string completionId;    // completion/run identifier for logging

    PromptChunk()
        : hash(0), partitionKey(), parentHash(0), buffer(), bufferSize(0), tokens(), completionId() {}

    PromptChunk(hash_t h, const std::string& pk, hash_t ph, const std::vector<uint8_t>& buf, const std::vector<Token>& toks = {}, const std::string& cid = "")
        : hash(h), partitionKey(pk), parentHash(ph), buffer(buf), bufferSize(buf.size()), tokens(toks), completionId(cid) {}
};

// V2 API: Block location (token-based blob name OR GUID for multi-version)
struct BlockLocation {
    hash_t hash;
    std::string location;  // Blob location (either encoded token name or GUID)
    
    BlockLocation() : hash(0), location() {}
    BlockLocation(hash_t h, const std::string& loc) : hash(h), location(loc) {}
};

// V2 API: Lookup result with locations
struct LookupResult {
    int cachedBlocks;
    hash_t lastHash;
    std::vector<BlockLocation> locations;
    
    LookupResult() : cachedBlocks(0), lastHash(0), locations() {}
    LookupResult(int blocks, hash_t hash) : cachedBlocks(blocks), lastHash(hash), locations() {}
};