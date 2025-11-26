#pragma once
#include <vector>
#include <string>
#include <functional>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <azure/core/etag.hpp>
#include <mutex>

// Forward declaration for Azure BlobClient
namespace Azure { namespace Storage { namespace Blobs {
    class BlobClient;
}}}

class BloomFilter {
public:
    BloomFilter(size_t expectedItems, double falsePositiveRate);
    BloomFilter();

    // Delete copy constructor and copy assignment
    BloomFilter(const BloomFilter&) = delete;
    BloomFilter& operator=(const BloomFilter&) = delete;

    // Optionally, allow move
    BloomFilter(BloomFilter&&) = default;
    BloomFilter& operator=(BloomFilter&&) = default;

    void Add(const std::string& item);
    bool PossiblyContains(const std::string& item) const;
    std::vector<uint8_t> Serialize() const;
    void Deserialize(const std::vector<uint8_t>& buffer);

    void LoadFromBlob(const std::shared_ptr<Azure::Storage::Blobs::BlobClient>& blobClient);
    void SaveToBlob(const std::shared_ptr<Azure::Storage::Blobs::BlobClient>& blobClient);
    void StartPolling(const std::shared_ptr<Azure::Storage::Blobs::BlobClient>& blobClient, std::chrono::milliseconds interval);
    void StopPolling();

    ~BloomFilter();

private:
    size_t bitCount_;
    size_t hashCount_;
    std::vector<uint8_t> bits_;
    Azure::ETag lastETag_;
    std::atomic<bool> stopPolling_{false};
    std::thread pollingThread_;
    
    static size_t OptimalBitCount(size_t n, double p);
    static size_t OptimalHashCount(size_t n, size_t m);
    static size_t Hash(const std::string& item, size_t i);
};