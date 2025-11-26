#include "kvstore.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using kvstore::KVStoreService;
using kvstore::LookupRequest;
using kvstore::LookupResponse;
using kvstore::ReadRequest;
using kvstore::ReadResponse;
using kvstore::WriteRequest;
using kvstore::WriteResponse;

class KVStoreClient {
public:
    KVStoreClient(std::shared_ptr<Channel> channel)
        : stub_(KVStoreService::NewStub(channel)) {}

    // Perform a Lookup operation with optional precomputed hashes
    bool Lookup(const std::string& accountUrl,
                const std::string& containerName,
                const std::string& partitionKey,
                const std::string& completionId,
                const std::vector<int64_t>& tokens,
                LookupResponse* response,
                const std::vector<uint64_t>& precomputedHashes = {}) {
        
        LookupRequest request;
        request.set_account_url(accountUrl);
        request.set_container_name(containerName);
        request.set_partition_key(partitionKey);
        request.set_completion_id(completionId);
        
        for (auto token : tokens) {
            request.add_tokens(token);
        }
        
        // Add precomputed hashes if provided
        for (auto hash : precomputedHashes) {
            request.add_precomputed_hashes(hash);
        }

        ClientContext context;
        Status status = stub_->Lookup(&context, request, response);

        if (!status.ok()) {
            std::cerr << "Lookup RPC failed: " << status.error_code() << ": " 
                      << status.error_message() << std::endl;
            return false;
        }

        return response->success();
    }

    // Perform a Read operation
    bool Read(const std::string& accountUrl,
              const std::string& containerName,
              const std::string& location,
              const std::string& completionId,
              ReadResponse* response) {
        
        ReadRequest request;
        request.set_account_url(accountUrl);
        request.set_container_name(containerName);
        request.set_location(location);
        request.set_completion_id(completionId);

        ClientContext context;
        Status status = stub_->Read(&context, request, response);

        if (!status.ok()) {
            std::cerr << "Read RPC failed: " << status.error_code() << ": " 
                      << status.error_message() << std::endl;
            return false;
        }

        return response->success();
    }

    // Perform a Write operation
    bool Write(const std::string& accountUrl,
               const std::string& containerName,
               uint64_t hash,
               const std::string& partitionKey,
               uint64_t parentHash,
               const std::vector<uint8_t>& buffer,
               const std::vector<int64_t>& tokens,
               const std::string& completionId,
               WriteResponse* response) {
        
        WriteRequest request;
        request.set_account_url(accountUrl);
        request.set_container_name(containerName);
        
        auto* chunk = request.mutable_chunk();
        chunk->set_hash(hash);
        chunk->set_partition_key(partitionKey);
        chunk->set_parent_hash(parentHash);
        chunk->set_buffer(buffer.data(), buffer.size());
        chunk->set_completion_id(completionId);
        
        for (auto token : tokens) {
            chunk->add_tokens(token);
        }

        ClientContext context;
        Status status = stub_->Write(&context, request, response);

        if (!status.ok()) {
            std::cerr << "Write RPC failed: " << status.error_code() << ": " 
                      << status.error_message() << std::endl;
            return false;
        }

        return response->success();
    }

private:
    std::unique_ptr<KVStoreService::Stub> stub_;
};

void PrintUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --server ADDRESS         Server address (default: localhost:50051)" << std::endl;
    std::cout << "  --account URL            Azure Storage account URL" << std::endl;
    std::cout << "  --container NAME         Container name" << std::endl;
    std::cout << "  --help                   Show this help message" << std::endl;
}

int main(int argc, char** argv) {
    std::string serverAddress = "localhost:50051";
    std::string accountUrl;
    std::string containerName;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }
        else if (arg == "--server" && i + 1 < argc) {
            serverAddress = argv[++i];
        }
        else if (arg == "--account" && i + 1 < argc) {
            accountUrl = argv[++i];
        }
        else if (arg == "--container" && i + 1 < argc) {
            containerName = argv[++i];
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }
    }
    
    if (accountUrl.empty() || containerName.empty()) {
        std::cerr << "Error: --account and --container are required" << std::endl;
        PrintUsage(argv[0]);
        return 1;
    }
    
    // Create client
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    KVStoreClient client(channel);
    
    std::cout << "==================================================" << std::endl;
    std::cout << "KV Store gRPC Client - Test Application" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "Server: " << serverAddress << std::endl;
    std::cout << "Account: " << accountUrl << std::endl;
    std::cout << "Container: " << containerName << std::endl;
    std::cout << "==================================================" << std::endl;
    
    // Test 1: Write operation
    std::cout << "\n[Test 1] Writing a test chunk..." << std::endl;
    // Use 128 tokens (standard block size for KVStore)
    std::vector<int64_t> testTokens;
    std::vector<uint8_t> testBuffer;
    for (int i = 0; i < 128; i++) {
        testTokens.push_back(1000 + i);  // Tokens: 1000, 1001, 1002, ...
        testBuffer.push_back(static_cast<uint8_t>('A' + (i % 26)));  // Repeating A-Z
    }
    
    WriteResponse writeResponse;
    bool writeSuccess = client.Write(
        accountUrl,
        containerName,
        12345,              // hash
        "test-partition",   // partition key
        0,                  // parent hash
        testBuffer,
        testTokens,
        "test-client-001",  // completion ID
        &writeResponse
    );
    
    if (writeSuccess) {
        std::cout << "  ✓ Write successful" << std::endl;
    } else {
        std::cout << "  ✗ Write failed: " << writeResponse.error() << std::endl;
    }
    
    // Test 2: Lookup operation
    std::cout << "\n[Test 2] Looking up cached tokens..." << std::endl;
    LookupResponse lookupResponse;
    // Pass the same hash (12345) that was used during Write
    std::vector<uint64_t> precomputedHashes = {12345};
    bool lookupSuccess = client.Lookup(
        accountUrl,
        containerName,
        "test-partition",
        "test-client-001",
        testTokens,
        &lookupResponse,
        precomputedHashes  // Must match the hash used in Write
    );
    
    if (lookupSuccess) {
        std::cout << "  ✓ Lookup successful" << std::endl;
        std::cout << "    Cached blocks: " << lookupResponse.cached_blocks() << std::endl;
        std::cout << "    Last hash: " << lookupResponse.last_hash() << std::endl;
        std::cout << "    Locations: " << lookupResponse.locations().size() << std::endl;
        
        // Test 3: Read operation (if we have locations)
        if (lookupResponse.locations().size() > 0) {
            std::cout << "\n[Test 3] Reading first cached block..." << std::endl;
            const auto& location = lookupResponse.locations(0);
            
            ReadResponse readResponse;
            bool readSuccess = client.Read(
                accountUrl,
                containerName,
                location.location(),
                "test-client-001",
                &readResponse
            );
            
            if (readSuccess && readResponse.found()) {
                std::cout << "  ✓ Read successful" << std::endl;
                std::cout << "    Chunk hash: " << readResponse.chunk().hash() << std::endl;
                std::cout << "    Buffer size: " << readResponse.chunk().buffer().size() << std::endl;
                std::cout << "    Tokens: " << readResponse.chunk().tokens().size() << std::endl;
            } else {
                std::cout << "  ✗ Read failed or not found" << std::endl;
            }
        }
    } else {
        std::cout << "  ✗ Lookup failed: " << lookupResponse.error() << std::endl;
    }
    
    std::cout << "\n==================================================" << std::endl;
    std::cout << "Test completed" << std::endl;
    std::cout << "==================================================" << std::endl;
    
    return 0;
}
