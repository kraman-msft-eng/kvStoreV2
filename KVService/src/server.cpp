#include "KVStoreServiceImpl.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <iostream>
#include <memory>
#include <string>
#include <csignal>

std::unique_ptr<grpc::Server> g_server;

void SignalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down gracefully..." << std::endl;
    if (g_server) {
        g_server->Shutdown();
    }
}

void RunServer(const std::string& serverAddress, 
               LogLevel logLevel = LogLevel::Information,
               HttpTransportProtocol transport = HttpTransportProtocol::WinHTTP,
               bool enableSdkLogging = true,
               bool enableMultiNic = false) {
    
    kvstore::KVStoreServiceImpl service;
    service.SetLogLevel(logLevel);
    service.SetHttpTransport(transport);
    service.EnableSdkLogging(enableSdkLogging);
    service.EnableMultiNic(enableMultiNic);

    grpc::EnableDefaultHealthCheckService(true);
    // Note: Proto reflection plugin not available in static builds
    
    grpc::ServerBuilder builder;
    
    // Configure server options for performance
    builder.SetMaxReceiveMessageSize(100 * 1024 * 1024);  // 100MB max message size
    builder.SetMaxSendMessageSize(100 * 1024 * 1024);
    
    // Add listening port
    builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    
    // Register service
    builder.RegisterService(&service);
    
    // Build and start server
    g_server = builder.BuildAndStart();
    
    if (!g_server) {
        std::cerr << "Failed to start server" << std::endl;
        return;
    }
    
    std::cout << "==================================================" << std::endl;
    std::cout << "KV Store gRPC Service" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "Server listening on: " << serverAddress << std::endl;
    std::cout << "Log Level: " << (logLevel == LogLevel::Error ? "Error" : 
                                   logLevel == LogLevel::Information ? "Information" : "Verbose") << std::endl;
    std::cout << "HTTP Transport: " << (transport == HttpTransportProtocol::WinHTTP ? "WinHTTP" : "LibCurl") << std::endl;
    std::cout << "SDK Logging: " << (enableSdkLogging ? "Enabled" : "Disabled") << std::endl;
    std::cout << "Multi-NIC: " << (enableMultiNic ? "Enabled" : "Disabled") << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;
    
    // Wait for server shutdown
    g_server->Wait();
    
    std::cout << "Server stopped" << std::endl;
}

void PrintUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --port PORT              Port to listen on (default: 50051)" << std::endl;
    std::cout << "  --host HOST              Host to bind to (default: 0.0.0.0)" << std::endl;
    std::cout << "  --log-level LEVEL        Log level: error, info, verbose (default: info)" << std::endl;
    std::cout << "  --transport TRANSPORT    HTTP transport: winhttp, libcurl (default: libcurl)" << std::endl;
    std::cout << "  --disable-sdk-logging    Disable Azure SDK logging" << std::endl;
    std::cout << "  --disable-multi-nic      Disable multi-NIC support (default: enabled)" << std::endl;
    std::cout << "  --help                   Show this help message" << std::endl;
}

int main(int argc, char** argv) {
    // Set up signal handlers
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    
    // Parse command line arguments
    std::string host = "0.0.0.0";
    int port = 50051;
    LogLevel logLevel = LogLevel::Information;
    HttpTransportProtocol transport = HttpTransportProtocol::LibCurl;
    bool enableSdkLogging = true;
    bool enableMultiNic = true;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }
        else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
        else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        }
        else if (arg == "--log-level" && i + 1 < argc) {
            std::string level = argv[++i];
            if (level == "error") {
                logLevel = LogLevel::Error;
            } else if (level == "info") {
                logLevel = LogLevel::Information;
            } else if (level == "verbose") {
                logLevel = LogLevel::Verbose;
            } else {
                std::cerr << "Invalid log level: " << level << std::endl;
                return 1;
            }
        }
        else if (arg == "--transport" && i + 1 < argc) {
            std::string transportStr = argv[++i];
            if (transportStr == "winhttp") {
                transport = HttpTransportProtocol::WinHTTP;
            } else if (transportStr == "libcurl") {
                transport = HttpTransportProtocol::LibCurl;
            } else {
                std::cerr << "Invalid transport: " << transportStr << std::endl;
                return 1;
            }
        }
        else if (arg == "--disable-sdk-logging") {
            enableSdkLogging = false;
        }
        else if (arg == "--disable-multi-nic") {
            enableMultiNic = false;
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }
    }
    
    std::string serverAddress = host + ":" + std::to_string(port);
    
    try {
        RunServer(serverAddress, logLevel, transport, enableSdkLogging, enableMultiNic);
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
