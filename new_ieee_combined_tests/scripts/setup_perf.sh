# #!/usr/bin/env bash

set -euo pipefail

if ! sudo -n true 2>/dev/null; then
    echo "Run sudo -v first"
    exit 0
fi

echo "Enabling perf access..."
sudo sysctl -w kernel.perf_event_paranoid=-1
sudo sysctl -w kernel.kptr_restrict=0

echo "Done"