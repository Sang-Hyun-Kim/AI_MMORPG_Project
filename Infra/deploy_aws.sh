#!/bin/bash
# ============================================================================
# AWS EC2 배포 자동화 스크립트 (Ubuntu/Linux 기준)
# ============================================================================
set -e

echo "Starting Deployment..."

# 1. Update and install dependencies
sudo apt-get update -y
sudo apt-get install -y cmake build-essential gdb docker.io docker-compose

# 2. Start Docker service
sudo systemctl enable docker
sudo systemctl start docker

# 3. Spin up infrastructure (MySQL, Redis)
echo "Starting Database Infrastructure via Docker Compose..."
cd Infra
sudo docker-compose up -d
cd ..

echo "Waiting for MySQL to initialize..."
sleep 15

# 4. Build C++ Server (Release)
echo "Building C++ GameServer..."
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# 5. Run Server
echo "Starting GameServer..."
nohup ./Server/GameServer/GameServer > server.log 2>&1 &

echo "Deployment complete! Server is running in the background."
