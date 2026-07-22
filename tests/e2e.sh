#!/usr/bin/env bash
# Automated end-to-end suite for webserv (drives the whole server over real
# HTTP). Serves as the regression gate: re-run it on every change to confirm
# nothing that already worked has broken.
#
# Non-interactive: starts the server with config/example.conf, fires requests,
# asserts on the actual responses, and exits 0 only if every check passes (so it
# works as a pre-commit / CI gate). Complements the interactive run_tests.sh.
#
# Each case maps to a feature we care about not regressing: standard HTTP,
# uploads, redirects, CGI (GET/POST/env/failure/timeout), and the crash/hang/
# leak fixes (location-less server, non-CGI file in a CGI dir, client drops,
# zombie reaping, overall liveness).
#
# Usage: tests/e2e.sh        # builds if needed, runs everything
#   Header changes: run `make re` first (the Makefile has no header deps).

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$ROOT/webserv"
CONFIG="$ROOT/config/example.conf"
HOST="127.0.0.1"
SERVER_LOG="$SCRIPT_DIR/.server.log"
UPLOAD_ARTIFACT="$ROOT/www/upload/regtest.txt"

VERBOSE=0
for arg in "$@"; do
	case "$arg" in
		-v | --verbose) VERBOSE=1 ;;
		-h | --help)
			echo "usage: tests/e2e.sh [-v|--verbose]"
			exit 0
			;;
		*)
			echo "unknown option: $arg (try -h)" >&2
			exit 2
			;;
	esac
done

BODY_FILE="$(mktemp)"

PASS=0
FAIL=0
pass() { printf '  \033[32m[PASS]\033[0m %s\n' "$1"; PASS=$((PASS + 1)); }
fail() { printf '  \033[31m[FAIL]\033[0m %s\n' "$1"; FAIL=$((FAIL + 1)); }

# With -v, dump the request, status, and (indented) response body for a check.
# BODY_FILE holds the last response body; $1 is its status, $2.. the curl args.
verbose_dump() {
	[ "$VERBOSE" -eq 1 ] || return 0
	local code="$1"
	shift
	printf '       \033[36m[REQUEST]\033[0m %s\n' "$*"
	printf '       \033[36m[STATUS]\033[0m  %s\n' "$code"
	if [ -s "$BODY_FILE" ]; then
		printf '       \033[36m[BODY]\033[0m\n'
		sed 's/^/         | /' "$BODY_FILE"
		# BODY_FILE may not end in a newline; make sure the blank-line
		# separator below always lands on its own line.
		[ -z "$(tail -c1 "$BODY_FILE")" ] || echo
	fi
	echo
}

# assert_status "desc" EXPECTED_CODE curl-args...
assert_status() {
	local desc="$1" expect="$2"
	shift 2
	local got
	got="$(curl -s -o "$BODY_FILE" -w '%{http_code}' -m 8 "$@")"
	if [ "$got" = "$expect" ]; then pass "$desc ($got)"; else fail "$desc (expected $expect, got $got)"; fi
	verbose_dump "$got" "$@"
}

# assert_body "desc" NEEDLE curl-args...
assert_body() {
	local desc="$1" needle="$2"
	shift 2
	local got
	got="$(curl -s -o "$BODY_FILE" -w '%{http_code}' -m 8 "$@")"
	if grep -q -- "$needle" "$BODY_FILE"; then pass "$desc"; else fail "$desc (missing '$needle')"; fi
	verbose_dump "$got" "$@"
}

# conn_probe HOST PORT close|keep  ->  prints "closed" if the server closed the
# socket after responding, "open" if it kept it alive (recv blocked to timeout).
conn_probe() {
	python3 - "$1" "$2" "$3" <<'PY'
import socket, sys
host, port, mode = sys.argv[1], int(sys.argv[2]), sys.argv[3]
req = b"GET / HTTP/1.1\r\nHost: x\r\n"
if mode == "close":
    req += b"Connection: close\r\n"
req += b"\r\n"
s = socket.create_connection((host, port), timeout=3)
s.sendall(req)
s.settimeout(3)
try:
    while True:
        if not s.recv(4096):
            print("closed"); break
except socket.timeout:
    print("open")
PY
}

# --- build ---
if ! make -C "$ROOT" >/dev/null 2>&1; then
	echo "build failed" >&2
	exit 1
fi

# --- start server (from ROOT so relative config paths resolve) ---
(cd "$ROOT" && exec "$BIN" "$CONFIG") >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

cleanup() {
	if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
		kill "$SERVER_PID" 2>/dev/null
		wait "$SERVER_PID" 2>/dev/null
	fi
	rm -f "$UPLOAD_ARTIFACT" "$BODY_FILE"
}
trap cleanup EXIT INT TERM

sleep 1
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
	echo "server failed to start, see $SERVER_LOG" >&2
	exit 1
fi

echo "== standard HTTP =="
assert_status "GET / -> 200" 200 "http://$HOST:8080/"
assert_status "GET /missing -> 404" 404 "http://$HOST:8080/missing"
assert_status "DELETE / (GET-only) -> 405" 405 -X DELETE "http://$HOST:8080/"
assert_status "GET /old -> 301 redirect" 301 "http://$HOST:8080/old"

echo "== virtual hosts (same port 8080, routed by Host header) =="
# example.com and vhost2.local both listen on 8080; the Host header decides.
assert_status "Host example.com -> default vhost 200" 200 -H "Host: example.com" "http://$HOST:8080/"
assert_status "Host vhost2.local -> second vhost 301" 301 -H "Host: vhost2.local" "http://$HOST:8080/"
assert_body   "vhost2.local served its own config" "routed-to-vhost2" -H "Host: vhost2.local" "http://$HOST:8080/"
assert_status "Host with :port suffix still routes" 301 -H "Host: vhost2.local:8080" "http://$HOST:8080/"
assert_status "unknown Host -> falls back to default" 200 -H "Host: nope.invalid" "http://$HOST:8080/"
# Per-vhost body limit: example.com allows 1000000, vhost2.local only 1000. A
# ~2000-byte POST must be judged by the *resolved* vhost's limit, not the port
# default (this is the regression the max_body recompute fixes).
VHOST_BODY="$(python3 -c "print('z' * 2000, end='')")"
assert_status "body limit follows Host: vhost2.local -> 413" 413 -H "Host: vhost2.local" -X POST --data-binary "$VHOST_BODY" "http://$HOST:8080/"
assert_status "same body under default's limit -> not 413" 201 -H "Host: example.com" -X POST --data-binary "$VHOST_BODY" "http://$HOST:8080/upload/vhtest.txt"
curl -s -X DELETE -H "Host: example.com" "http://$HOST:8080/upload/vhtest.txt" >/dev/null 2>&1

echo "== upload lifecycle =="
assert_status "POST upload -> 201" 201 -X POST --data-binary "regression payload" "http://$HOST:8080/upload/regtest.txt"
assert_status "GET uploaded file -> 200" 200 "http://$HOST:8080/upload/regtest.txt"
assert_status "GET /upload/ autoindex -> 200" 200 "http://$HOST:8080/upload/"
assert_body   "autoindex lists entries" "Index of" "http://$HOST:8080/upload/"
assert_status "DELETE uploaded file -> 204" 204 -X DELETE "http://$HOST:8080/upload/regtest.txt"
assert_status "GET deleted file -> 404" 404 "http://$HOST:8080/upload/regtest.txt"

echo "== CGI =="
assert_status "CGI GET -> 200" 200 "http://$HOST:8080/cgi-bin/echo.py"
assert_body   "CGI env: REQUEST_METHOD=GET" "method=GET" "http://$HOST:8080/cgi-bin/echo.py"
assert_body   "CGI POST body -> stdin" "body=\[hello cgi\]" -X POST --data-binary "hello cgi" "http://$HOST:8080/cgi-bin/echo.py"
assert_body   "CGI POST CONTENT_LENGTH" "len=9" -X POST --data-binary "hello cgi" "http://$HOST:8080/cgi-bin/echo.py"
assert_status "CGI non-zero exit -> 502" 502 "http://$HOST:8080/cgi-bin/fail.py"
assert_status "CGI runaway -> 504 (cgi_timeout)" 504 "http://$HOST:8080/cgi-bin/slow.py"
assert_body   "CGI runs in its own dir (relative file read)" "relative-read-ok" "http://$HOST:8080/cgi-bin/readfile.py"

echo "== client_max_body_size =="
# The 8082 server caps bodies at 1000 bytes.
BIG_BODY="$(python3 -c "print('x' * 2000, end='')")"
SMALL_BODY="$(python3 -c "print('y' * 100, end='')")"
assert_status "over-limit POST -> 413" 413 -X POST --data-binary "$BIG_BODY" "http://$HOST:8082/anything"
assert_status "under-limit POST -> not 413" 404 -X POST --data-binary "$SMALL_BODY" "http://$HOST:8082/anything"

echo "== connection handling =="
if [ "$(conn_probe "$HOST" 8080 close)" = "closed" ]; then pass "Connection: close closes the socket"; else fail "Connection: close closes the socket"; fi
if [ "$(conn_probe "$HOST" 8080 keep)" = "open" ]; then pass "HTTP/1.1 keep-alive keeps the socket open"; else fail "HTTP/1.1 keep-alive keeps the socket open"; fi

echo "== robustness (crash/hang/leak regressions) =="
assert_status "location-less server (8082) -> 404, no crash" 404 "http://$HOST:8082/anything"
assert_status "non-CGI file in CGI dir -> 404, no hang" 404 "http://$HOST:8080/cgi-bin/nope.txt"

# 40 clients that send a request then drop without reading — must not crash us.
for _ in $(seq 1 40); do
	(exec 3<>"/dev/tcp/$HOST/8080"; printf 'GET / HTTP/1.1\r\nHost: x\r\n\r\n' >&3; exec 3>&-) 2>/dev/null
done
assert_status "alive after 40 abrupt disconnects" 200 "http://$HOST:8080/"

# CGI ran above; no child should be left as a zombie.
zombies="$(ps --ppid "$SERVER_PID" -o stat= 2>/dev/null | grep -c 'Z')"
if [ "$zombies" -eq 0 ]; then pass "no zombie CGI children"; else fail "zombie children: $zombies"; fi

assert_status "server still serving at end" 200 "http://$HOST:8080/"

echo
echo "== summary: $PASS passed, $FAIL failed =="
[ "$FAIL" -eq 0 ]
