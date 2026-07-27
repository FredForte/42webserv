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
# uploads, redirects, CGI (GET/POST/env/failure/timeout), alias-style path
# resolution (prefix stripped then joined onto root, for both static files and
# CGI scripts), and the crash/hang/leak fixes (location-less server, non-CGI
# file in a CGI dir, client drops, zombie reaping, overall liveness).
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

# drip_request HOST PORT SPLIT_BYTES  ->  sends the request read from stdin in
# SPLIT_BYTES-sized writes with a pause between them, then prints the response.
# Framing is decided incrementally as bytes arrive, so a request that is correct
# when it lands in one read can still be mis-framed when it dribbles in; this is
# the only way to exercise that path from the outside.
# NB: the script is passed with -c, not a heredoc - a heredoc would BE stdin and
# the request piped in would never reach the script.
drip_request() {
	python3 -c '
import socket, sys, time
host, port, split = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
data = sys.stdin.buffer.read()
s = socket.create_connection((host, port), timeout=10)
for i in range(0, len(data), split):
    s.sendall(data[i:i + split])
    time.sleep(0.005)
s.settimeout(5)
out = b""
try:
    while True:
        b = s.recv(65536)
        if not b:
            break
        out += b
except socket.timeout:
    pass
sys.stdout.buffer.write(out)
' "$1" "$2" "$3"
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

# A CGI that closes stdout before exiting makes the pipe report EOF while the
# child is still alive, so it can't be reaped yet. The pipe stays readable
# forever from then on, so the run has to come off epoll at EOF and be finished
# by the reaper - otherwise the loop spins at full CPU and the request ends up
# answered 504 by its own timeout instead of served.
assert_status "CGI that closes stdout early -> served, not timed out" 200 "http://$HOST:8080/cgi-bin/closes_stdout.py"
assert_body   "CGI that closes stdout early returns its output" "stdout-closed-early" "http://$HOST:8080/cgi-bin/closes_stdout.py"

# cgi_instance is owned by the connection, so a keep-alive client reuses it for
# every request. The collected CGI output must be reset per run: without that,
# the second response carries the first run's body plus the new run's raw CGI
# header block. --next on one curl invocation reuses the socket, so the second
# body must be exactly "BBB" and must not mention the CGI's own Status: line.
CGI_REUSE="$(curl -s -m 20 \
	-X POST --data-binary "aaa" "http://$HOST:8080/directory/youpi.bla" \
	--next -X POST --data-binary "bbb" "http://$HOST:8080/directory/youpi.bla")"
if [ "$CGI_REUSE" = "AAABBB" ]; then
	pass "keep-alive CGI reuse: no stale output between runs"
else
	fail "keep-alive CGI reuse: no stale output between runs (got $(printf '%s' "$CGI_REUSE" | head -c 120 | tr '\n' '~'))"
fi

echo "== alias path resolution (prefix stripped, joined onto root) =="
# /static is rooted at www/, so the /static prefix must be stripped before
# joining. A 200 here (not 404) proves alias-style resolution: /static/index.html
# -> www/index.html, not www/static/index.html.
assert_status "alias GET /static/index.html -> 200" 200 "http://$HOST:8080/static/index.html"
assert_body   "alias serves the aliased file (www/index.html)" "<title>Test</title>" "http://$HOST:8080/static/index.html"
assert_status "alias GET /static/ (index resolved under root) -> 200" 200 "http://$HOST:8080/static/"
assert_status "alias GET /static/missing -> 404" 404 "http://$HOST:8080/static/missing.html"

echo "== CGI on an alias location =="
# /scripts is rooted at cgi-bin/, so /scripts/echo.py must resolve to
# cgi-bin/echo.py. This is the regression the alias CGI fix guards: the script
# path has to come from the (prefix-stripped) alias root, not the request path.
assert_status "alias CGI GET -> 200" 200 "http://$HOST:8080/scripts/echo.py"
assert_body   "alias CGI env: REQUEST_METHOD=GET" "method=GET" "http://$HOST:8080/scripts/echo.py"
assert_body   "alias CGI POST body -> stdin" "body=\[hello alias\]" -X POST --data-binary "hello alias" "http://$HOST:8080/scripts/echo.py"
assert_body   "alias CGI POST CONTENT_LENGTH" "len=11" -X POST --data-binary "hello alias" "http://$HOST:8080/scripts/echo.py"

# CGI path variables split in two: the on-disk pair follows the location's alias
# root (/scripts is rooted at cgi-bin/, so PATH_TRANSLATED / SCRIPT_FILENAME must
# resolve to cgi-bin/pathinfo.py), while PATH_INFO / SCRIPT_NAME / REQUEST_URI
# stay in URL space. The 42 cgi_tester enforces PATH_INFO == the requested URL
# path, so it must never carry the on-disk location.
assert_body   "alias CGI PATH_INFO = request URL path" "^PATH_INFO=/scripts/pathinfo.py$" "http://$HOST:8080/scripts/pathinfo.py"
assert_body   "alias CGI PATH_TRANSLATED = resolved on-disk path" "^PATH_TRANSLATED=cgi-bin/pathinfo.py$" "http://$HOST:8080/scripts/pathinfo.py"
assert_body   "alias CGI SCRIPT_FILENAME = resolved on-disk path" "^SCRIPT_FILENAME=cgi-bin/pathinfo.py$" "http://$HOST:8080/scripts/pathinfo.py"
assert_body   "alias CGI SCRIPT_NAME keeps the request URL" "^SCRIPT_NAME=/scripts/pathinfo.py$" "http://$HOST:8080/scripts/pathinfo.py"
assert_body   "alias CGI REQUEST_URI carries path + query" "^REQUEST_URI=/scripts/pathinfo.py?k=v$" "http://$HOST:8080/scripts/pathinfo.py?k=v"

echo "== client_max_body_size =="
# The 8082 server caps bodies at 1000 bytes.
BIG_BODY="$(python3 -c "print('x' * 2000, end='')")"
SMALL_BODY="$(python3 -c "print('y' * 100, end='')")"
assert_status "over-limit POST -> 413" 413 -X POST --data-binary "$BIG_BODY" "http://$HOST:8082/anything"
assert_status "under-limit POST -> not 413" 404 -X POST --data-binary "$SMALL_BODY" "http://$HOST:8082/anything"

# /post_body declares its own client_max_body_size 100, which must override the
# 8080 server's (0 = unlimited). Boundary matters: 100 passes, 101 is over.
AT_LIMIT="$(python3 -c "print('z' * 100, end='')")"
OVER_LIMIT="$(python3 -c "print('z' * 101, end='')")"
assert_status "location limit: POST at the limit (100) -> 200" 200 -X POST --data-binary "$AT_LIMIT" "http://$HOST:8080/post_body"
assert_status "location limit: POST one over (101) -> 413" 413 -X POST --data-binary "$OVER_LIMIT" "http://$HOST:8080/post_body"
# A 413 is sent mid-body and the socket is closed right after, so the response
# must advertise the close. Saying keep-alive here and hanging up anyway leaves a
# pooling client (Go's http.Client, for one) reusing a dead socket and getting an
# RST in place of its next response. -i so the headers land in the checked output.
assert_body   "413 advertises Connection: close" "Connection: close" -i -X POST --data-binary "$OVER_LIMIT" "http://$HOST:8080/post_body"
# The location's limit must not leak onto sibling locations that don't set one:
# /upload inherits the 8080 server's 0 (unlimited), so 2000 bytes is fine there.
assert_status "location limit stays scoped (sibling location unaffected)" 201 -X POST --data-binary "$BIG_BODY" "http://$HOST:8080/upload/limitscope.txt"
curl -s -o /dev/null -X DELETE -m 8 "http://$HOST:8080/upload/limitscope.txt"

# A POST aimed at the location prefix itself has no filename to store under, and
# a body is optional for POST. That is a valid request, not a 400.
assert_status "POST with no body -> accepted" 200 -X POST --data-binary "" "http://$HOST:8080/post_body"
assert_status "POST with no filename in target -> accepted" 200 -X POST --data-binary "hello" "http://$HOST:8080/post_body"

echo "== request framing (incremental, arrives in pieces) =="
# Framing state is now carried across recv calls instead of being recomputed from
# byte 0 each time, so the cases that matter are the ones where a request is
# split at an awkward point: mid-header, across the blank line, mid-chunk-size,
# mid-chunk-data. Each split size below lands the boundary somewhere different.
# NB: emitted by a function rather than held in a variable - $(...) strips
# trailing newlines, which would eat the request's final LF and leave every one
# of these hanging forever.
chunked_req() {
	printf 'POST /directory/youpi.bla HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n7\r\nWebserv\r\n4\r\nTest\r\n7\r\nChunked\r\n0\r\n\r\n'
}
for split in 1 3 7 40 4096; do
	OUT="$(chunked_req | drip_request "$HOST" 8080 "$split")"
	# the CGI upper-cases the body it receives, so this proves every chunk was
	# reassembled, in order, exactly once
	if printf '%s' "$OUT" | grep -q "WEBSERVTESTCHUNKED"; then
		pass "chunked request reassembled when split every $split byte(s)"
	else
		fail "chunked request reassembled when split every $split byte(s) (got $(printf '%s' "$OUT" | tr -d '\r' | tail -1))"
	fi
done

# Same for a Content-Length body: the header block and the body must be measured
# correctly even when the blank line itself is split across two writes.
CL_BODY="hello framing world"
cl_req() {
	printf 'POST /directory/youpi.bla HTTP/1.1\r\nHost: localhost\r\nContent-Length: %s\r\n\r\n%s' \
		"${#CL_BODY}" "$CL_BODY"
}
for split in 1 5 37; do
	OUT="$(cl_req | drip_request "$HOST" 8080 "$split")"
	if printf '%s' "$OUT" | grep -q "HELLO FRAMING WORLD"; then
		pass "content-length request framed when split every $split byte(s)"
	else
		fail "content-length request framed when split every $split byte(s)"
	fi
done

# Two requests down one connection: the framing state must be reset between them,
# or the second gets measured with the first one's header length / body size.
OUT="$(python3 - "$HOST" 8080 <<'PY'
import socket, sys
host, port = sys.argv[1], int(sys.argv[2])
# First a Content-Length request, then a chunked one down the same socket,
# each read to completion before the next is sent (keep-alive, not pipelining).
# The second must be framed from scratch: if the first request's header length
# or body size leaked through, it is measured wrong and the reply is garbage.
reqs = [
    b"POST /directory/youpi.bla HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\n\r\naaa",
    b"POST /directory/youpi.bla HTTP/1.1\r\nHost: localhost\r\n"
    b"Transfer-Encoding: chunked\r\n\r\n3\r\nbbb\r\n0\r\n\r\n",
]
s = socket.create_connection((host, port), timeout=5)
bodies = []
for r in reqs:
    s.sendall(r)
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += s.recv(65536)
    head, _, rest = buf.partition(b"\r\n\r\n")
    n = 0
    for line in head.split(b"\r\n"):
        if line.lower().startswith(b"content-length:"):
            n = int(line.split(b":")[1])
    while len(rest) < n:
        rest += s.recv(65536)
    bodies.append(rest[:n].decode())
print(",".join(bodies))
PY
)"
if [ "$OUT" = "AAA,BBB" ]; then
	pass "framing state resets between requests on one connection"
else
	fail "framing state resets between requests on one connection (got '$OUT')"
fi

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

# A client that walks away mid-CGI used to strand everything behind it: the child
# kept running to completion, its pipe stayed open and registered, and its
# cgi_fd_map entry pointed at a client that no longer existed - which the reaper
# skips, so nothing ever collected it. Dropping the connection must take the run
# with it. slow.py sleeps 30s, far longer than this test waits.
FDS_BEFORE="$(ls "/proc/$SERVER_PID/fd" 2>/dev/null | wc -l)"
DROP_PIDS=""
for _ in $(seq 1 5); do
	curl -s -m 1 -o /dev/null "http://$HOST:8080/cgi-bin/slow.py" 2>/dev/null &
	DROP_PIDS="$DROP_PIDS $!"
done
# only these, never a bare `wait` - the server itself is a background job of this
# shell, so waiting on everything would block until the suite times out
for p in $DROP_PIDS; do wait "$p" 2>/dev/null; done
sleep 2
ORPHANS="$(pgrep -P "$SERVER_PID" -x python3 2>/dev/null | wc -l)"
FDS_AFTER="$(ls "/proc/$SERVER_PID/fd" 2>/dev/null | wc -l)"
if [ "$ORPHANS" -eq 0 ]; then pass "client disconnect kills its in-flight CGI"; else fail "client disconnect kills its in-flight CGI ($ORPHANS still running)"; fi
if [ "$FDS_AFTER" -le "$FDS_BEFORE" ]; then pass "disconnect mid-CGI leaks no fds ($FDS_AFTER <= $FDS_BEFORE)"; else fail "disconnect mid-CGI leaks fds ($FDS_BEFORE -> $FDS_AFTER)"; fi

echo "== config validation (rejected at boot, not at request time) =="
# A location advertising POST with nowhere to put the body can only ever answer
# 403, so it must not be allowed to start. Uses a port nothing else binds.
BAD_CONF="$(mktemp)"
printf 'server {\n listen 8099;\n location /nowhere {\n  root www/;\n  methods GET POST;\n }\n}\n' > "$BAD_CONF"
# output captured first, not piped: this script runs with `set -o pipefail`, and
# webserv rightly exits non-zero here, which would fail the pipeline even though
# grep matched.
BAD_OUT="$("$BIN" "$BAD_CONF" 2>&1 || true)"
if printf '%s' "$BAD_OUT" | grep -q 'allows POST but has no'; then
	pass "POST location without upload_store or cgi is rejected at boot"
else
	fail "POST location without upload_store or cgi is rejected at boot"
fi
# A "return" location answers every method with the redirect and never reads a
# body, so it is exempt from that rule.
REDIR_CONF="$(mktemp)"
printf 'server {\n listen 8099;\n location /old {\n  methods GET POST;\n  return 301 /new;\n }\n}\n' > "$REDIR_CONF"
REDIR_OUT="$(timeout 2 "$BIN" "$REDIR_CONF" 2>&1 || true)"
if printf '%s' "$REDIR_OUT" | grep -q 'config:'; then
	fail "POST allowed on a redirect location (should not need upload_store)"
else
	pass "POST allowed on a redirect location (exempt from the rule)"
fi
rm -f "$BAD_CONF" "$REDIR_CONF"

assert_status "server still serving at end" 200 "http://$HOST:8080/"

echo
echo "== summary: $PASS passed, $FAIL failed =="
[ "$FAIL" -eq 0 ]
