#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Keep the actual launch-path behavior checks together after removing the
# investigation-only callback, watch, and provenance suites.
bash tools/test-dd-policy.sh
bash tools/test-dd-dynarec-boundary.sh
bash tools/test-dd-dma-transfer.sh
bash tools/test-dd-core-imem-dma.sh
echo "DD startup behavioral checks passed"