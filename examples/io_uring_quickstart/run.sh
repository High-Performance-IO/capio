#!/usr/bin/env bash
# Runs the io_uring quickstart under CAPIO: the consumer starts first, the
# producer writes five messages, and the consumer reads them while they are written.
#
#   ./run.sh                              uses an installed CAPIO (capio_server on PATH)
#   CAPIO_BUILD=/path/to/build ./run.sh   uses a CAPIO build directory instead
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
workflow="io_uring_quickstart"

# --- find CAPIO ---------------------------------------------------------------
if [ -n "${CAPIO_BUILD:-}" ]; then
    server="$CAPIO_BUILD/capio/server/capio_server"
    library="$CAPIO_BUILD/capio/posix/libcapio_posix.so"
    [ -x "$server" ] && [ -f "$library" ] || { echo "No CAPIO build in $CAPIO_BUILD"; exit 1; }
else
    server="$(command -v capio_server)" || { echo "capio_server not found: install CAPIO or set CAPIO_BUILD"; exit 1; }
    library="libcapio_posix.so"
fi

# --- build the two programs ---------------------------------------------------
cmake -S "$here" -B "$here/build" >/dev/null
cmake --build "$here/build" >/dev/null

# --- start the CAPIO server ---------------------------------------------------
dir="$(mktemp -d /tmp/capio_quickstart.XXXX)"
keep_dir=0
cleanup() {
    [ -n "${server_pid:-}" ] && kill "$server_pid" 2>/dev/null && wait "$server_pid" 2>/dev/null
    rm -f /dev/shm/"$workflow"* /dev/shm/sem."$workflow"* 2>/dev/null
    [ "$keep_dir" = 1 ] || rm -rf "$dir"
}
trap cleanup EXIT

# CAPIO sends written data to the server one cache line at a time (default 256 KiB).
# One line per 4 KiB message lets the consumer see each message at once. The server
# and the applications must use the same value.
export CAPIO_CACHE_LINE_SIZE=4096

# -r resolves the relative paths in capio_config.json against CAPIO_DIR.
CAPIO_DIR="$dir" "$server" -c "$here/capio_config.json" -r "$dir" >"$dir/server.log" 2>&1 &
server_pid=$!
for _ in $(seq 50); do
    grep -q "successfully started" "$dir/server.log" && break
    sleep 0.2
done
grep -q "successfully started" "$dir/server.log" || { echo "The server did not start:"; cat "$dir/server.log"; exit 1; }

# --- run the two steps --------------------------------------------------------
# INTERCEPT_ALL_OBJS=1 lets CAPIO see the io_uring calls that liburing makes.
with_capio() { # with_capio <app name> <command...>
    local app="$1"; shift
    CAPIO_DIR="$dir" CAPIO_WORKFLOW_NAME="$workflow" CAPIO_APP_NAME="$app" \
        INTERCEPT_ALL_OBJS=1 LD_PRELOAD="$library" "$@"
}

echo "CAPIO_DIR is $dir"
echo
with_capio consumer "$here/build/consumer" "$dir/messages.txt" | tee -a "$dir/output" &
consumer_pid=$!
sleep 1
with_capio producer "$here/build/producer" "$dir/messages.txt" 5 | tee -a "$dir/output"
wait "$consumer_pid"

# --- result -------------------------------------------------------------------
echo
read_count=$(grep -c '^consumer: read    message' "$dir/output" || true)
first_read=$(grep -n -m1 '^consumer: read' "$dir/output" | cut -d: -f1)
closed=$(grep -n -m1 '^producer: closed' "$dir/output" | cut -d: -f1)
if [ "$read_count" = 5 ] && grep -q '^consumer: end of file' "$dir/output"; then
    if [ -n "$first_read" ] && [ -n "$closed" ] && [ "$first_read" -lt "$closed" ]; then
        echo "Done: the consumer read the 5 messages through CAPIO while the producer wrote them."
    else
        echo "Done: the consumer read the 5 messages through CAPIO after the producer closed the file."
    fi
    [ -e "$dir/messages.txt" ] || echo "messages.txt is not in $dir: CAPIO kept it in memory."
else
    echo "The consumer did not read the 5 messages. Server log: $dir/server.log"
    keep_dir=1
    exit 1
fi
