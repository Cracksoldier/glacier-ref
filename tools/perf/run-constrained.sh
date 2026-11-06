#!/usr/bin/env bash
# Runs a command under the constrained performance profile (spec §9): four
# P-cores (8 hardware threads) and 16 GiB memory without swap, approximating a
# contemporary 4-core / 16 GiB machine on the reference workstation.
#
# Usage: tools/perf/run-constrained.sh <command> [args...]
# Example:
#   tools/perf/run-constrained.sh build/dev-release/tools/bench/glacier-bench \
#       --dataset /data/glacier-fixtures/standard --screen HDMI-A-1 --json m1-standard.json
#
# CPUs 0-7 are P-cores 0-3 with Hyper-Threading on the i9-13900K (check with
# `lscpu -e=CPU,CORE,MAXMHZ`). Override with GLACIER_PERF_CPUS.

set -euo pipefail

if [[ $# -eq 0 ]]; then
    sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
for tool in taskset systemd-run; do
    command -v "$tool" >/dev/null 2>&1 || { echo "run-constrained: '$tool' is required" >&2; exit 2; }
done

cpus="${GLACIER_PERF_CPUS:-0-7}"
echo "run-constrained: CPUs $cpus, MemoryMax=16G, MemorySwapMax=0" >&2
exec systemd-run --user --scope --quiet -p MemoryMax=16G -p MemorySwapMax=0 -- taskset -c "$cpus" "$@"
