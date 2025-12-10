#include "KVStoreServiceImpl.h"
#include "StorageDatabaseResolver.h"
#include "MetricsHelper.h"
#include "ServiceConfig.h"
#include "FileConfigProvider.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <iostream>
#include <memory>
#include <string>
#include <csignal>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

std::unique_ptr<grpc::Server> g_server;

// Get total processor count across all NUMA nodes/processor groups
int GetTotalProcessorCount() {
#ifdef _WIN32
    WORD groupCount = GetActiveProcessorGroupCount();
    if (groupCount > 1) {
        DWORD totalProcessors = 0;
        for (WORD i = 0; i < groupCount; i++) {
            totalProcessors += GetActiveProcessorCount(i);
        }
        return static_cast<int>(totalProcessors);
    }
#endif
    int count = std::thread::hardware_concurrency();
    return count > 0 ? count : 96;  // Fallback for FX96 VMs
}

// Set process to use all NUMA nodes for better CPU utilization
void EnableAllNumaNodes() {
#ifdef _WIN32
    // Get number of processor groups (NUMA nodes on Windows)
    WORD groupCount = GetActiveProcessorGroupCount();
    std::cout << "Detected " << groupCount << " processor group(s)" << std::endl;
    
    if (groupCount > 1) {
        std::cout << "Multi-NUMA system detected. Enabling all processor groups." << std::endl;
        
        // Get total processor count across all groups
        DWORD totalProcessors = 0;
        for (WORD i = 0; i < groupCount; i++) {
            DWORD processorsInGroup = GetActiveProcessorCount(i);
            std::cout << "  Group " << i << ": " << processorsInGroup << " processors" << std::endl;
            totalProcessors += processorsInGroup;
        }
        std::cout << "Total processors: " << totalProcessors << std::endl;
        
        // Set process to use all processor groups (Windows 11 / Server 2022+)
        // This allows threads to be scheduled across all NUMA nodes
        HANDLE hProcess = GetCurrentProcess();
        
        // Try to set affinity to span all groups
        // Build a GROUP_AFFINITY for each group and set default thread affinity
        for (WORD group = 0; group < groupCount; group++) {
            // Create a thread on each NUMA node to "warm up" the scheduler
            // This encourages Windows to distribute subsequent threads
            std::thread([group]() {
                GROUP_AFFINITY affinity = {};
                affinity.Group = group;
                affinity.Mask = ~0ULL;  // All processors in this group
                SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr);
                // Brief work to establish presence on this NUMA node
                volatile int x = 0;
                for (int i = 0; i < 1000; i++) x += i;
            }).detach();
        }
        
        std::cout << "Threads will be distributed across " << groupCount << " NUMA nodes" << std::endl;
    }
#endif
}

void SignalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down gracefully..." << std::endl;
    if (g_server) {
        g_server->Shutdown();
    }
}

void RunServer(const std::string& serverAddress,
               const kvstore::ServiceConfig& serviceConfig,
               LogLevel logLevel = LogLevel::Information,
               HttpTransportProtocol transport = HttpTransportProtocol::WinHTTP,
               bool enableSdkLogging = true,
               bool enableMultiNic = false,
               bool enableMetricsLogging = true,
               int numThreads = 0) {
    
    // Report NUMA topology
    EnableAllNumaNodes();
    
    // Determine thread count - use all processors across all NUMA nodes
    if (numThreads <= 0) {
        numThreads = GetTotalProcessorCount();
    }
    std::cout << "Using " << numThreads << " server threads" << std::endl;
    
    // Log service configuration
    std::cout << "Service Configuration:" << std::endl;
    std::cout << "  Current Location: " << serviceConfig.currentLocation << std::endl;
    std::cout << "  Configuration Store: " << serviceConfig.configurationStore << std::endl;
    std::cout << "  Configuration Container: " << serviceConfig.configurationContainer << std::endl;
    std::cout << "  Domain Suffix: " << serviceConfig.domainSuffix << std::endl;
    std::cout << "  Configuration Store URL: " << serviceConfig.GetConfigurationStoreUrl() << std::endl;
    
    // Enable gRPC verbose logging if environment variable is set
    const char* grpcVerbose = std::getenv("GRPC_VERBOSITY");
    if (grpcVerbose) {
        std::cout << "gRPC verbosity level: " << grpcVerbose << std::endl;
    }
    const char* grpcTrace = std::getenv("GRPC_TRACE");
    if (grpcTrace) {
        std::cout << "gRPC trace enabled: " << grpcTrace << std::endl;
    }
    
    // Create StorageDatabaseResolver with configuration
    kvstore::StorageDatabaseResolverConfig resolverConfig;
    resolverConfig.serviceConfig = serviceConfig;
    resolverConfig.httpTransport = transport;
    resolverConfig.enableSdkLogging = enableSdkLogging;
    resolverConfig.enableMultiNic = enableMultiNic;
    resolverConfig.logLevel = logLevel;
    
    auto accountResolver = std::make_shared<kvstore::StorageDatabaseResolver>(resolverConfig);
    
    // Set up logging callback for the resolver
    accountResolver->SetLogCallback([logLevel](LogLevel level, const std::string& message) {
        if (level == LogLevel::Error) {
            std::cerr << "[ERROR] " << message << std::endl;
        } else if (level <= logLevel) {
            std::cout << "[INFO] " << message << std::endl;
        }
    });
    
    // Initialize the resolver (connects to config store)
    if (!accountResolver->Initialize()) {
        std::cerr << "Failed to initialize account resolver: " << accountResolver->GetLastError() << std::endl;
        return;
    }
    std::cout << "Account resolver initialized successfully" << std::endl;
    
    // Create service with account resolver
    kvstore::KVStoreServiceImpl service(accountResolver);
    service.SetLogLevel(logLevel);
    service.EnableMetricsLogging(enableMetricsLogging);

    grpc::EnableDefaultHealthCheckService(true);
    // Note: Proto reflection plugin not available in static builds
    
    grpc::ServerBuilder builder;
    
    // Configure server options for performance
    builder.SetMaxReceiveMessageSize(100 * 1024 * 1024);  // 100MB max message size
    builder.SetMaxSendMessageSize(100 * 1024 * 1024);
    
    // NUMA optimization: Configure gRPC to use more threads across NUMA nodes
    // For async callback API, we use resource quota and completion queue count
    grpc::ResourceQuota quota("server_quota");
    quota.SetMaxThreads(numThreads);
    builder.SetResourceQuota(quota);
    
    // Use multiple completion queues to spread work across NUMA nodes
    // Each CQ can be serviced by threads on different NUMA nodes
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::NUM_CQS, 
                                 std::max(2, numThreads / 24));  // ~4 CQs for 96 threads
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MIN_POLLERS, numThreads / 2);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS, numThreads);
    
    // TCP optimization: Reduce latency for small messages
    builder.AddChannelArgument("grpc.tcp_user_timeout_ms", 20000);  // 20 second TCP timeout
    builder.AddChannelArgument("grpc.tcp_nodelay", 1);  // Disable Nagle's algorithm
    
    // Server-side keepalive configuration
    // Allow client keepalive pings and send server keepalive pings
    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS, 10000);  // Ping every 10s
    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000);  // 5s timeout
    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
    builder.AddChannelArgument(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 5000);
    builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);  // Unlimited
    
    // Increase concurrent stream limit to handle high client concurrency
    builder.AddChannelArgument(GRPC_ARG_MAX_CONCURRENT_STREAMS, 200);
    
    // HTTP/2 flow control optimizations
    builder.AddChannelArgument(GRPC_ARG_HTTP2_BDP_PROBE, 1);  // Enable BDP probing
    
    // HTTP/2 frame size optimization for large payloads (1.2MB KV cache buffers)
    builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_FRAME_SIZE, 16 * 1024 * 1024);  // 16MB max frame
    builder.AddChannelArgument(GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES, 64 * 1024 * 1024);  // 64MB stream window
    builder.AddChannelArgument(GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE, 8 * 1024 * 1024);  // 8MB write buffer
    
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
    std::cout << "Account Resolver: StorageDatabaseResolver" << std::endl;
    std::cout << "Log Level: " << (logLevel == LogLevel::Error ? "Error" : 
                                   logLevel == LogLevel::Information ? "Information" : "Verbose") << std::endl;
    std::cout << "HTTP Transport: " << (transport == HttpTransportProtocol::WinHTTP ? "WinHTTP" : "LibCurl") << std::endl;
    std::cout << "SDK Logging: " << (enableSdkLogging ? "Enabled" : "Disabled") << std::endl;
    std::cout << "Multi-NIC: " << (enableMultiNic ? "Enabled" : "Disabled") << std::endl;
    std::cout << "Metrics Logging: " << (enableMetricsLogging ? "Enabled" : "Disabled") << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;
    
    // Wait for server shutdown
    g_server->Wait();
    
    std::cout << "Server stopped" << std::endl;
}

void PrintUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --config FILE                 Path to service configuration JSON file (default: service-config.json)" << std::endl;
    std::cout << "  --port PORT                   Port to listen on (default: 50051)" << std::endl;
    std::cout << "  --host HOST                   Host to bind to (default: 0.0.0.0)" << std::endl;
    std::cout << "  --threads NUM                 Number of server threads (default: auto-detect CPU count)" << std::endl;
    std::cout << "  --log-level LEVEL             Log level: error, info, verbose (default: info)" << std::endl;
    std::cout << "  --transport TRANSPORT         HTTP transport: winhttp, libcurl (default: libcurl)" << std::endl;
    std::cout << "  --enable-sdk-logging          Enable Azure SDK logging (default: disabled)" << std::endl;
    std::cout << "  --disable-multi-nic           Disable multi-NIC support (default: enabled)" << std::endl;
    std::cout << "  --disable-metrics             Disable JSON metrics logging to console (default: enabled)" << std::endl;
    std::cout << "  --metrics-endpoint ENDPOINT   Azure Monitor OTLP endpoint (optional)" << std::endl;
    std::cout << "  --instrumentation-key KEY     Application Insights instrumentation key (optional)" << std::endl;
    std::cout << "  --help                        Show this help message" << std::endl;
}

int main(int argc, char** argv) {
    // Set up signal handlers
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    
    // Parse command line arguments
    std::string host = "0.0.0.0";
    int port = 50051;
    int numThreads = 0;  // 0 = auto-detect
    LogLevel logLevel = LogLevel::Information;
    HttpTransportProtocol transport = HttpTransportProtocol::LibCurl;
    bool enableSdkLogging = false;  // Default: disabled for cleaner output
    bool enableMultiNic = true;
    bool enableMetricsLogging = true;  // Default: enabled
    std::string metricsEndpoint;
    std::string instrumentationKey;
    std::string configFilePath = "service-config.json";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }
        else if (arg == "--config" && i + 1 < argc) {
            configFilePath = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
        else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        }
        else if (arg == "--threads" && i + 1 < argc) {
            numThreads = std::stoi(argv[++i]);
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
        else if (arg == "--enable-sdk-logging") {
            enableSdkLogging = true;
        }
        else if (arg == "--disable-multi-nic") {
            enableMultiNic = false;
        }
        else if (arg == "--disable-metrics") {
            enableMetricsLogging = false;
        }
        else if (arg == "--metrics-endpoint" && i + 1 < argc) {
            metricsEndpoint = argv[++i];
        }
        else if (arg == "--instrumentation-key" && i + 1 < argc) {
            instrumentationKey = argv[++i];
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }
    }
    
    // Load service configuration
    std::cout << "Loading configuration from: " << configFilePath << std::endl;
    kvstore::FileConfigProvider configProvider(configFilePath);
    if (!configProvider.Load()) {
        std::cerr << "Failed to load configuration: " << configProvider.GetLastError() << std::endl;
        return 1;
    }
    
    const kvstore::ServiceConfig& serviceConfig = configProvider.GetConfig();
    std::cout << "Configuration loaded successfully" << std::endl;
    
    std::string serverAddress = host + ":" + std::to_string(port);
    
    // Initialize Azure Monitor metrics if endpoint provided
    if (!metricsEndpoint.empty() && !instrumentationKey.empty()) {
        std::cout << "Initializing Azure Monitor metrics..." << std::endl;
        kvstore::MetricsHelper::GetInstance().Initialize(metricsEndpoint, instrumentationKey);
    } else if (!metricsEndpoint.empty() || !instrumentationKey.empty()) {
        std::cerr << "Warning: Both --metrics-endpoint and --instrumentation-key are required for Azure Monitor" << std::endl;
    }
    
    try {
        RunServer(serverAddress, serviceConfig, logLevel, transport, enableSdkLogging, enableMultiNic, enableMetricsLogging, numThreads);
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
