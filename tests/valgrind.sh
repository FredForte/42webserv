#!/usr/bin/env bash
# Memory-leak / memory-error gate for webserv, driven through valgrind.
#
# Unlike e2e.sh (which asserts on HTTP behaviour), this suite asks a different
# question: after the server has handled a representative mix of traffic and
# then shut down cleanly, did it hand back everything it allocated?
#
# How it works:
#   1. Rebuild with -g so valgrind can attribute leaks to real line numbers.
#   2. Launch the server *under* valgrind (leak-check=full, all leak kinds).
#   3. Fire the same kinds of requests e2e.sh does -- standard HTTP, uploads,
#      CGI (incl. failure + timeout), body-size rejection, virtual hosts, and
#      40 abrupt disconnects -- so every alloc/free path runs at least once.
#   4. SIGTERM the server. Its signal handler breaks the event loop and runs the
#      cleanup (frees the read buffer, closes every fd), so valgrind sees an
#      orderly exit and prints a trustworthy report instead of shutdown noise.
#   5. Parse valgrind's summary and FAIL if any of these are non-zero:
#        definitely lost, indirectly lost, still reachable, or errors.
#
# Exit code 0 only if the report is clean. Meant as a CI / pre-commit gate.
#
# Usage: tests/valgrind.sh [-v|--verbose]   (-v also echoes the full VG log)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$ROOT/webserv"
CONFIG="$ROOT/config/example.conf"
HOST="127.0.0.1"
VG_LOG="$SCRIPT_DIR/.valgrind.log"
UPLOAD_ARTIFACT="$ROOT/www/upload/regtest.txt"

VERBOSE=0
for arg in "$@"; do
	case "$arg" in
		-v | --verbose) VERBOSE=1 ;;
		-h | --help) echo "usage: tests/valgrind.sh [-v|--verbose]"; exit 0 ;;
		*) echo "unknown option: $arg (try -h)" >&2; exit 2 ;;
	esac
done

# curl is slow-tolerant here: valgrind makes the server several times slower, so
# every request gets a generous timeout.
CURL="curl -s -o /dev/null -m 30"

# --- rebuild with debug symbols (fresh, so line numbers are accurate) ---
echo "== building with -g (make re DEBUG=1) =="
if ! make -C "$ROOT" re DEBUG=1 >/dev/null 2>&1; then
	echo "build failed" >&2
	exit 1
fi

# --- launch the server under valgrind, from ROOT so relative paths resolve ---
echo "== starting server under valgrind =="
(
	cd "$ROOT" && exec valgrind \
		--leak-check=full \
		--show-leak-kinds=all \
		--errors-for-leak-kinds=all \
		--track-origins=yes \
		--num-callers=30 \
		--log-file="$VG_LOG" \
		"$BIN" "$CONFIG"
) >/dev/null 2>&1 &
SERVER_PID=$!

cleanup() {
	if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
		kill -9 "$SERVER_PID" 2>/dev/null
		wait "$SERVER_PID" 2>/dev/null
	fi
	rm -f "$UPLOAD_ARTIFACT"
}
trap cleanup EXIT INT TERM

# --- wait until the server (slow under valgrind) is accepting connections ---
ready=0
for _ in $(seq 1 60); do
	if ! kill -0 "$SERVER_PID" 2>/dev/null; then
		echo "server died during startup, see $VG_LOG" >&2
		cat "$VG_LOG" >&2 2>/dev/null
		exit 1
	fi
	if (exec 3<>"/dev/tcp/$HOST/8080") 2>/dev/null; then ready=1; exec 3>&- 2>/dev/null; break; fi
	sleep 0.5
done
if [ "$ready" -ne 1 ]; then
	echo "server never became ready, see $VG_LOG" >&2
	exit 1
fi

# --- drive representative traffic so every alloc/free path executes ---
echo "== driving traffic =="

# standard HTTP
$CURL "http://$HOST:8080/"
$CURL "http://$HOST:8080/missing"
$CURL -X DELETE "http://$HOST:8080/"
$CURL "http://$HOST:8080/old"

# virtual hosts (routing + per-vhost body limit recompute)
$CURL -H "Host: vhost2.local" "http://$HOST:8080/"
$CURL -H "Host: nope.invalid" "http://$HOST:8080/"

# upload lifecycle (POST -> GET -> autoindex -> DELETE)
$CURL -X POST --data-binary "regression payload" "http://$HOST:8080/upload/regtest.txt"
$CURL "http://$HOST:8080/upload/regtest.txt"
$CURL "http://$HOST:8080/upload/"
$CURL -X DELETE "http://$HOST:8080/upload/regtest.txt"

# CGI: success (GET + POST-with-body), failure (502), timeout (504)
$CURL "http://$HOST:8080/cgi-bin/echo.py"
$CURL -X POST --data-binary "hello cgi" "http://$HOST:8080/cgi-bin/echo.py"
$CURL "http://$HOST:8080/cgi-bin/fail.py"
$CURL "http://$HOST:8080/cgi-bin/slow.py"

# client_max_body_size rejection (413) and an under-limit request
BIG_BODY="$(python3 -c "print('x' * 2000, end='')")"
SMALL_BODY="$(python3 -c "print('y' * 100, end='')")"
$CURL -X POST --data-binary "$BIG_BODY" "http://$HOST:8082/anything"
$CURL -X POST --data-binary "$SMALL_BODY" "http://$HOST:8082/anything"

# 40 abrupt disconnects: request sent, socket dropped without reading. Exercises
# the recv()<=0 teardown path (the crash fix) under the leak checker.
for _ in $(seq 1 40); do
	(exec 3<>"/dev/tcp/$HOST/8080"; printf 'GET / HTTP/1.1\r\nHost: x\r\n\r\n' >&3; exec 3>&-) 2>/dev/null
done

# one more live request to prove we're still serving after all of the above
$CURL "http://$HOST:8080/"

# --- shut the server down cleanly so valgrind writes its report ---
echo "== stopping server (SIGTERM -> graceful shutdown) =="
kill -TERM "$SERVER_PID" 2>/dev/null

# wait for the process (and thus valgrind's report flush) to finish
for _ in $(seq 1 60); do
	kill -0 "$SERVER_PID" 2>/dev/null || break
	sleep 0.5
done
if kill -0 "$SERVER_PID" 2>/dev/null; then
	echo "server did not exit after SIGTERM; killing" >&2
	kill -9 "$SERVER_PID" 2>/dev/null
fi
wait "$SERVER_PID" 2>/dev/null
SERVER_PID=""

[ "$VERBOSE" -eq 1 ] && { echo "== full valgrind log =="; cat "$VG_LOG"; echo; }

# --- parse the report ---
# Pull the byte count for a given leak category ("definitely lost" etc.). Lines
# look like:  ==NN==    definitely lost: 1,024 bytes in 2 blocks
# Missing category (valgrind omits it when the run is perfectly clean) -> 0.
leak_bytes() {
	local label="$1" line
	line="$(grep -F "$label:" "$VG_LOG" | tail -n1)"
	[ -z "$line" ] && { echo 0; return; }
	# take the field after the label, strip thousands separators, keep digits
	echo "$line" | sed -E "s/.*$label: *([0-9,]+) bytes.*/\1/" | tr -d ','
}

echo "== valgrind summary =="
if [ ! -s "$VG_LOG" ]; then
	echo "no valgrind log produced at $VG_LOG" >&2
	exit 1
fi

definite="$(leak_bytes 'definitely lost')"
indirect="$(leak_bytes 'indirectly lost')"
reachable="$(leak_bytes 'still reachable')"
possible="$(leak_bytes 'possibly lost')"
errors="$(grep -E 'ERROR SUMMARY:' "$VG_LOG" | tail -n1 | sed -E 's/.*ERROR SUMMARY: *([0-9,]+) errors.*/\1/' | tr -d ',')"
[ -z "$errors" ] && errors=0

printf '  definitely lost: %s bytes\n' "${definite:-0}"
printf '  indirectly lost: %s bytes\n' "${indirect:-0}"
printf '  still reachable: %s bytes\n' "${reachable:-0}"
printf '  possibly lost:   %s bytes (reported, not gated)\n' "${possible:-0}"
printf '  errors:          %s\n' "${errors:-0}"

fail=0
for pair in "definitely lost:$definite" "indirectly lost:$indirect" "still reachable:$reachable" "errors:$errors"; do
	name="${pair%%:*}"; val="${pair##*:}"
	if [ "${val:-0}" -ne 0 ]; then
		printf '  \033[31m[FAIL]\033[0m %s = %s (expected 0)\n' "$name" "$val"
		fail=1
	fi
done

echo
if [ "$fail" -eq 0 ]; then
	printf '\033[32m== CLEAN: no leaks, no errors ==\033[0m\n'
	echo "(full log: $VG_LOG)"
	exit 0
else
	printf '\033[31m== LEAKS/ERRORS DETECTED -- see %s ==\033[0m\n' "$VG_LOG"
	echo
	echo "---- relevant valgrind excerpts ----"
	grep -E 'lost|reachable|ERROR SUMMARY|at 0x|by 0x' "$VG_LOG" | head -60
	exit 1
fi
