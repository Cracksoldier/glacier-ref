#!/usr/bin/env bash
# Offline audit (spec A10, milestone M0.09).
#
# Runs a command in a network namespace without connectivity and traces its
# socket activity. Any Internet-family socket (AF_INET/AF_INET6), whether it
# was created, connected, or sent to, by the application's own processes fails
# the audit. Local IPC (AF_UNIX: Wayland, D-Bus, portals) and AF_NETLINK are
# expected and only listed.
#
# The desktop's URL openers (xdg-open, kde-open, gio, ...) and everything they
# start (the browser) after Help > Project Website, a user-initiated link
# (spec A10), are outside the application's own network activity: their
# sockets are listed separately and don't fail the audit. Any other program
# the application starts counts as the application's own.
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
    strace -f -qq -e trace=socket,connect,bind,sendto,sendmsg,execve,clone,clone3,fork,vfork -o "$log" -- "$@"
status=$?
set -e

# Attribute every Internet socket to a process: own (the command and its
# threads/forks) or external (a process that exec'd another program, and its
# descendants). strace -f prefixes each line with the PID.
classified="$(awk '
    { pid = $1 }
    NR == 1 { root = pid }
    /(clone3?|fork|vfork)\(/ || /<\.\.\. (clone3?|fork|vfork) resumed>/ {
        n = split($0, parts, "= ")
        child = parts[n]
        sub(/[^0-9].*$/, "", child)
        if (child != "")
            parent[child] = pid
    }
    / execve\(/ && pid != root {
        program = $0
        sub(/^[^"]*"/, "", program)
        sub(/".*$/, "", program)
        programs[program] = 1
        name = program
        sub(/^.*\//, "", name)
        if (name ~ /^(xdg-open|kde-open[0-9]*|kioclient[0-9]*|gio|gnome-open|exo-open|gvfs-open)$/)
            external[pid] = 1
    }
    /(AF|PF)_INET6?[^_A-Z0-9]/ {
        line[++count] = $0
        owner[count] = pid
    }
    END {
        for (i = 1; i <= count; i++) {
            q = owner[i]
            ext = 0
            while (q != "") {
                if (q in external) { ext = 1; break }
                q = (q in parent) ? parent[q] : ""
            }
            print (ext ? "EXTERNAL " : "OWN ") line[i]
        }
        for (p in programs)
            print "PROGRAM " p
    }' "$log")"
inet="$(grep '^OWN ' <<<"$classified" | cut -c5- || true)"
external_inet="$(grep -c '^EXTERNAL ' <<<"$classified" || true)"
programs="$(grep '^PROGRAM ' <<<"$classified" | cut -c9- | sort | tr '\n' ' ' || true)"
unix_count="$(grep -cE '(AF|PF)_UNIX' "$log" || true)"
netlink_count="$(grep -cE '(AF|PF)_NETLINK' "$log" || true)"

echo "offline-audit: command exit status: $status"
echo "offline-audit: AF_UNIX calls (local IPC, expected): ${unix_count:-0}"
echo "offline-audit: AF_NETLINK calls (expected): ${netlink_count:-0}"
if [[ -n "${programs// /}" ]]; then
    echo "offline-audit: programs started by the application: $programs"
    echo "offline-audit: Internet socket calls by URL openers and the browser (not counted): ${external_inet:-0}"
fi
[[ $keep_log -eq 1 ]] && echo "offline-audit: trace log kept at $log"

if [[ -n "$inet" ]]; then
    echo "offline-audit: FAIL - Internet socket activity detected:" >&2
    echo "$inet" >&2
    exit 1
fi
echo "offline-audit: PASS - no AF_INET/AF_INET6 socket activity"
