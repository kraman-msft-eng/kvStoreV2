#!/bin/bash
# Run KVPlayground from WSL to Azure Windows Server

# Uncomment for verbose gRPC debugging:
# export GRPC_VERBOSITY=DEBUG
# export GRPC_TRACE=all

echo "===== Running KVPlayground from WSL to Azure Server ====="
echo "Server: 172.179.242.186:8085"
echo ""

export KVSTORE_GRPC_SERVER="172.179.242.186:8085"

cd build/KVPlayground
./KVPlayground conversation_tokens.json 1 1 -s https://aoaikv.blob.core.windows.net -c gpt51-promptcache
