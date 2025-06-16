#!/bin/bash

# Build NIXL with 3FS Docker Image and Convert to Singularity
# This script builds the Docker image and converts it for use with srun/Slurm

set -e

echo "=== Building NIXL with 3FS Docker Image ==="

# Build the Docker image
echo "Building Docker image from Dockerfile-with-3fs..."
docker build -f contrib/Dockerfile-with-3fs -t nixl-with-3fs:latest .

echo "Docker image built successfully!"

# Convert to Singularity format
echo "=== Converting to Singularity format ==="

# Create output directory if it doesn't exist
OUTPUT_DIR="/lustre/fsw/coreai_comparch_trtllm/$(whoami)"
mkdir -p "$OUTPUT_DIR"

# Convert Docker image to Singularity .sif format
SINGULARITY_IMAGE="$OUTPUT_DIR/nixl-with-3fs.sif"
echo "Converting to Singularity format: $SINGULARITY_IMAGE"

singularity build "$SINGULARITY_IMAGE" docker-daemon://nixl-with-3fs:latest

echo "=== Conversion completed successfully! ==="
echo "Singularity image location: $SINGULARITY_IMAGE"

# Optional: Create .sqsh format if preferred (compressed)
echo "=== Creating compressed .sqsh format ==="
SQSH_IMAGE="$OUTPUT_DIR/nixl-with-3fs.sqsh"
singularity build "$SQSH_IMAGE" docker-daemon://nixl-with-3fs:latest

echo "=== All formats created successfully! ==="
echo "Available images:"
echo "  SIF format:  $SINGULARITY_IMAGE"
echo "  SQSH format: $SQSH_IMAGE"
echo ""
echo "Use either format with your srun command." 