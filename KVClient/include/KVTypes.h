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

// Server-side performance metrics (from gRPC responses)
struct ServerMetrics {
    int64_t storage_latency_us = 0;  // Storage layer latency
    int64_t total_latency_us = 0;    // Total server-side latency
    int64_t overhead_us = 0;         // Server overhead (total - storage)
    int64_t client_e2e_us = 0;       // Client-measured E2E latency (from gRPC client)
    int64_t serialize_us = 0;        // Client-side request serialization time
    int64_t deserialize_us = 0;      // Client-side response deserialization time
    int64_t network_us = 0;          // Pure network time (e2e - server_total - serialize - deserialize)
    
    ServerMetrics() = default;
    ServerMetrics(int64_t storage, int64_t total, int64_t overhead, int64_t client_e2e = 0)
        : storage_latency_us(storage), total_latency_us(total), overhead_us(overhead), client_e2e_us(client_e2e) {}
};

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
    ServerMetrics server_metrics;  // Server-side performance metrics
    
    LookupResult() : cachedBlocks(0), lastHash(0), locations(), server_metrics() {}
    LookupResult(int blocks, hash_t hash) : cachedBlocks(blocks), lastHash(hash), locations(), server_metrics() {}
};