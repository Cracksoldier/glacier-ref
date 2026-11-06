#!/usr/bin/env bash
# Offline audit (spec A10, milestone M0.09).
#
# Runs a command in a network namespace without connectivity and traces its
# socket activity. Any Internet-family socket (AF_INET/AF_INET6), whether it
# was created, connected, or sent to, fails the audit. Local IPC (AF_UNIX:
# Wayland, D-Bus, portals) and AF_NETLINK are expected and only listed.
#
# Usage: tools/offline-audit.sh [--keep-log] <command> [args...]
# Example: tools/offline-audit.sh build/dev-release/app/glacier-ref --platform-info
#
# Requires: strace, unshare (util-linux >= 2.38 for --map-current-user).
# Exit status: 0 = no Internet socket activity, 1 = activity found, 2 = usage/setup error.

set -euo pipefail

keep_log=0
if [[ "${1:-}" == "--keep-log" ]]; then
    keep_log=1
    shift
fi
if [[ $# -eq 0 ]]; then
    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
for tool in strace unshare; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "offline-audit: '$tool' is required (e.g. sudo pacman -S $tool)" >&2
        exit 2
    fi
done

log="$(mktemp -t glacier-offline-audit.XXXXXX)"
cleanup() { [[ $keep_log -eq 1 ]] || rm -f "$log"; }
trap cleanup EXIT

set +e
unshare --map-current-user --net -- \
    strace -f -qq -e trace=socket,connect,bind,sendto,sendmsg -o "$log" -- "$@"
status=$?
set -e

inet="$(grep -E '(AF|PF)_INET6?\b' "$log" || true)"
unix_count="$(grep -cE '(AF|PF)_UNIX' "$log" || true)"
netlink_count="$(grep -cE '(AF|PF)_NETLINK' "$log" || true)"

echo "offline-audit: command exit status: $status"
echo "offline-audit: AF_UNIX calls (local IPC, expected): ${unix_count:-0}"
echo "offline-audit: AF_NETLINK calls (expected): ${netlink_count:-0}"
[[ $keep_log -eq 1 ]] && echo "offline-audit: trace log kept at $log"

if [[ -n "$inet" ]]; then
    echo "offline-audit: FAIL - Internet socket activity detected:" >&2
    echo "$inet" >&2
    exit 1
fi
echo "offline-audit: PASS - no AF_INET/AF_INET6 socket activity"
