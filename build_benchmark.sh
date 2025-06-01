#!/bin/bash

# Compile the io_uring benchmark
echo "Compiling io_uring overhead benchmark..."
g++ -o uring_overhead_benchmark uring_overhead_benchmark.cpp -luring -std=c++17 -O2

if [ $? -eq 0 ]; then
    echo "Build successful. Run with: ./uring_overhead_benchmark"
else
    echo "Build failed."
fi 