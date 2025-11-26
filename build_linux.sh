#!/bin/bash
# Build script for KVStoreV2 on Linux
# Builds KVClient library and KVPlayground application

set -e

echo "===== Building KVStoreV2 for Linux ====="
echo ""

# Check for vcpkg
if [ -z "$VCPKG_ROOT" ]; then
    echo "Warning: VCPKG_ROOT not set. Assuming vcpkg is in ~/vcpkg"
    VCPKG_ROOT="$HOME/vcpkg"
fi

VCPKG_CMAKE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

if [ ! -f "$VCPKG_CMAKE" ]; then
    echo "Error: vcpkg toolchain file not found at $VCPKG_CMAKE"
    echo "Please install vcpkg or set VCPKG_ROOT environment variable"
    exit 1
fi

echo "Using vcpkg: $VCPKG_ROOT"
echo ""

# Create build directory
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    echo "Cleaning existing build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Configuring CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_CMAKE"

if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

echo ""
echo "Building..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo ""
echo "===== Build Successful ====="
echo ""
echo "Binaries created:"
echo "  KVClient library: $BUILD_DIR/KVClient/libKVClient.a"
echo "  KVPlayground: $BUILD_DIR/KVPlayground/KVPlayground"
echo ""
echo "To run KVPlayground:"
echo "  export KVSTORE_GRPC_SERVER=\"your-windows-server:50051\""
echo "  ./build/KVPlayground/KVPlayground conversation_tokens.json 10 2"
echo ""
