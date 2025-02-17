#!/usr/bin/env bash

# Create and start docker image
echo "Creating and starting docker image..."
docker compose build mutable
docker compose up mutable --detach

# Execute benchmark script
echo "Executing benchmark script..."
docker compose exec -it mutable /mutable/run_benchmarks.sh

# Execute paper script
echo "Executing paper script..."
docker compose exec -it mutable /mutable/build_paper.sh

# Copy final paper to host
echo "Copying final paper to host..."
docker compose cp mutable:mutable/paper/main.pdf main.pdf

# Stop docker image
echo "Stopping docker image..."
docker compose down
