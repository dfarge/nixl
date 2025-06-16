#!/bin/bash

# 3FS Complete Build Script
# This script automates the entire 3FS build process including all necessary fixes
# Based on: https://github.com/deepseek-ai/3FS/issues/78

set -e  # Exit on any error

echo "=== 3FS Complete Build Script ==="
echo "This script will build 3FS with all dependencies"
echo

# Check if running in container
if [ ! -f /.dockerenv ]; then
    echo "ERROR: This script should be run inside a Docker container"
    echo "Please run the playground.sh script first to create the container"
    exit 1
fi

# Set build directory
export BUILD_DIR=${BUILD_DIR:-/tmp/build-3fs}
mkdir -p $BUILD_DIR

echo "=== Step 1: Installing system dependencies ==="
apt update
apt install -y cmake libuv1-dev liblz4-dev liblzma-dev libdouble-conversion-dev libdwarf-dev libunwind-dev \
    libaio-dev libgflags-dev libgoogle-glog-dev libgtest-dev libgmock-dev clang-format-14 clang-14 clang-tidy-14 lld-14 \
    libgoogle-perftools-dev google-perftools libssl-dev gcc-12 g++-12 libboost-all-dev cargo git g++ wget meson curl

echo "=== Step 2: Installing FoundationDB ==="
cd /tmp
wget -q https://github.com/apple/foundationdb/releases/download/7.1.67/foundationdb-server_7.1.67-1_amd64.deb \
     https://github.com/apple/foundationdb/releases/download/7.1.67/foundationdb-clients_7.1.67-1_amd64.deb

dpkg -i foundationdb-server_7.1.67-1_amd64.deb foundationdb-clients_7.1.67-1_amd64.deb

echo "=== Step 3: Building and installing FUSE 3.16.2 ==="
cd /tmp
wget -q https://github.com/libfuse/libfuse/releases/download/fuse-3.16.2/fuse-3.16.2.tar.gz
tar -zxf fuse-3.16.2.tar.gz
cd fuse-3.16.2
mkdir build
cd build
meson setup ..
ninja
ninja install  # Ignore any errors as mentioned in guide

echo "=== Step 4: Updating Rust to latest version ==="
# Install rustup and latest Rust (needed for Cargo.lock version 4)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source "$HOME/.cargo/env"

# Verify Rust version
echo "Rust version: $(rustc --version)"
echo "Cargo version: $(cargo --version)"

echo "=== Step 5: Cloning and building 3FS ==="
cd /tmp
git clone https://github.com/deepseek-ai/3fs
cd 3fs

# Initialize submodules
git submodule update --init --recursive

# Apply patches
./patches/apply.sh

# Configure build
cmake -S . -B build \
    -DCMAKE_CXX_COMPILER=clang++-14 \
    -DCMAKE_C_COMPILER=clang-14 \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build with all available cores
echo "=== Building 3FS (this may take a while) ==="
cmake --build build -j $(nproc)

echo "=== Build completed successfully! ==="
echo "Built binaries are located in: /tmp/3fs/build/bin"
echo
echo "Available binaries:"
ls -lh /tmp/3fs/build/bin/ | awk '{print $9, $5}' | column -t

echo
echo "=== Build Summary ==="
echo "✅ System dependencies installed"
echo "✅ FoundationDB 7.1.67 installed"
echo "✅ FUSE 3.16.2 built and installed"
echo "✅ Rust $(rustc --version | cut -d' ' -f2) installed"
echo "✅ 3FS built successfully"
echo
echo "You can now use the 3FS binaries for setting up your distributed filesystem!" 