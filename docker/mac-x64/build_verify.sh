#!/bin/bash
# Build verification app for macOS
set -e
OUTPUT_DIR="../../build-output/mac-x64"
cd "$OUTPUT_DIR"
if [ ! -f verify_auth ]; then
    echo "Building verification app..."
    clang++ -std=c++11 -mmacosx-version-min=10.14 -stdlib=libc++ \
        -o verify_auth ../../verify_auth.cc
fi
echo "Running verification..."
./verify_auth
