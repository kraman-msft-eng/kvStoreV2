#include "BloomFilter.h"
#include <azure/storage/blobs.hpp>
#include <azure/storage/blobs/blob_client.hpp>
#include <azure/storage/blobs/block_blob_client.hpp>
#include <azure/storage/blobs/blob_options.hpp>
#include <azure/storage/blobs/blob_responses.hpp>
#include <cassert>

BloomFilter::BloomFilter(size_t expectedItems, double falsePositiveRate)
    : bitCount_(OptimalBitCount(expectedItems, falsePositiveRate)),
      hashCount_(OptimalHashCount(expectedItems, bitCount_))
{
    size_t byteCount = (bitCount_ + 7) / 8;
    bits_.resize(byteCount, 0);
}

BloomFilter::BloomFilter() : bitCount_(0), hashCount_(0) {}

void BloomFilter::Add(const std::string& item) {
    for (size_t i = 0; i < hashCount_; ++i) {
        size_t hash = Hash(item, i);
        size_t idx = hash % bitCount_;
        bits_[idx / 8] |= (1 << (idx % 8));
    }
}

bool BloomFilter::PossiblyContains(const std::string& item) const {
    for (size_t i = 0; i < hashCount_; ++i) {
        size_t hash = Hash(item, i);
        size_t idx = hash % bitCount_;
        if (!(bits_[idx / 8] & (1 << (idx % 8))))
            return false;
    }
    return true;
}

std::vector<uint8_t> BloomFilter::Serialize() const {
    std::vector<uint8_t> buffer;
    buffer.resize(16);
    *reinterpret_cast<size_t*>(&buffer[0]) = bitCount_;
    *reinterpret_cast<size_t*>(&buffer[8]) = hashCount_;
    buffer.insert(buffer.end(), bits_.begin(), bits_.end());
    return buffer;
}

void BloomFilter::Deserialize(const std::vector<uint8_t>& buffer) {
    if (buffer.size() < 16) throw std::runtime_error("BloomFilter: buffer too small");
    bitCount_ = *reinterpret_cast<const size_t*>(&buffer[0]);
    hashCount_ = *reinterpret_cast<const size_t*>(&buffer[8]);
    size_t byteCount = (bitCount_ + 7) / 8;
    if (buffer.size() < 16 + byteCount) throw std::runtime_error("BloomFilter: buffer too small for bits");
    bits_.assign(buffer.begin() + 16, buffer.begin() + 16 + byteCount);
}

void BloomFilter::LoadFromBlob(const std::shared_ptr<Azure::Storage::Blobs::BlobClient>& blobClient) {
    try {
        auto downloadResponse = blobClient->Download();
        auto& bodyStream = downloadResponse.Value.BodyStream;
        std::vector<uint8_t> buffer;
        buffer.resize(static_cast<size_t>(downloadResponse.Value.BlobSize));
        bodyStream->Read(buffer.data(), buffer.size());
        Deserialize(buffer);
        lastETag_ = downloadResponse.Value.Details.ETag;
    } catch (const Azure::Storage::StorageException&) {
        lastETag_ = Azure::ETag(); // Null ETag
    }
}

void BloomFilter::SaveToBlob(const std::shared_ptr<Azure::Storage::Blobs::BlobClient>& blobClient) {
    auto buffer = Serialize();
    Azure::Storage::Blobs::UploadBlockBlobFromOptions options;
    blobClient->AsBlockBlobClient().UploadFrom(buffer.data(), buffer.size(), options);
}

void BloomFilter::StartPolling(const std::shared_ptr<Azure::Storage::Blobs::BlobClient>& blobClient, std::chrono::milliseconds interval) {
    stopPolling_ = false;
    pollingThread_ = std::thread([this, blobClient, interval]() {
        while (!stopPolling_) {
            try {
                auto properties = blobClient->GetProperties();
                auto currentETag = properties.Value.ETag;
                if (currentETag != lastETag_) {
                    LoadFromBlob(blobClient);
                }
            } catch (...) {
                // Ignore errors
            }
            std::this_thread::sleep_for(interval);
        }
    });
}

void BloomFilter::StopPolling() {
    stopPolling_ = true;
    if (pollingThread_.joinable()) {
        pollingThread_.join();
    }
}

BloomFilter::~BloomFilter() {
    StopPolling();
}

size_t BloomFilter::OptimalBitCount(size_t n, double p) {
    return static_cast<size_t>(-1.0 * n * std::log(p) / (std::log(2) * std::log(2)));
}

size_t BloomFilter::OptimalHashCount(size_t n, size_t m) {
    return std::max<size_t>(1, static_cast<size_t>(std::round((double)m / n * std::log(2))));
}

size_t BloomFilter::Hash(const std::string& item, size_t i) {
    static std::hash<std::string> hasher;
    size_t h1 = hasher(item);
    size_t h2 = std::hash<size_t>{}(h1 ^ 0x9e3779b9);
    return h1 + i * h2;
}