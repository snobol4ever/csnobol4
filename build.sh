#!/bin/bash
# build.sh — build CSNOBOL4 from source
# Requires: gcc, make, m4
# Usage: bash build.sh
set -e

apt-get install -y m4 -qq 2>/dev/null || true

cd "$(dirname "$0")"
./configure
make snobol4
echo "Built: $(pwd)/snobol4"
