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
pass() { printf '  \033[32mPASS\033[0m %s\n' "$1"; PASS=$((PASS + 1)); }
fail() { printf '  \033[31mFAIL\033[0m %s\n' "$1"; FAIL=$((FAIL + 1)); }

# With -v, dump the request, status, and (indented) response body for a check.
# BODY_FILE holds the last response body; $1 is its status, $2.. the curl args.
verbose_dump() {
	[ "$VERBOSE" -eq 1 ] || return 0
	local code="$1"
	shift
	printf '       request: %s\n' "$*"
	printf '       status : %s\n' "$code"
	if [ -s "$BODY_FILE" ]; then
		printf '       body   :\n'
		sed 's/^/         | /' "$BODY_FILE"
	fi
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
