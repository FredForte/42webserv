#!/usr/bin/env bash
# Interactive runner for the raw .http request files in this directory.
# Starts webserv, lets you pick a request to fire at it, and prints the
# raw request alongside whatever webserv sent back.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="$PROJECT_ROOT/webserv"
CONFIG="${1:-$PROJECT_ROOT/config/upload_test.conf}"

# main.cpp binds this port directly, regardless of what the config's
# "listen" directives say, so there is nothing to configure here.
HOST="127.0.0.1"
PORT="8080"

SERVER_LOG="$SCRIPT_DIR/.server.log"
# webserv currently never closes the socket after responding (it just goes
# back to EPOLLIN), so we cap how long we wait for a reply instead of
# waiting for the connection to close on its own.
REPLY_TIMEOUT=2

SERVER_PID=""

cleanup() {
	if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
		kill "$SERVER_PID" 2>/dev/null
		wait "$SERVER_PID" 2>/dev/null
	fi
}
trap cleanup EXIT INT TERM

if [ ! -x "$BINARY" ]; then
	echo "webserv binary not found, building it..."
	make -C "$PROJECT_ROOT" || exit 1
fi

if [ ! -f "$CONFIG" ]; then
	echo "Config file not found: $CONFIG" >&2
	exit 1
fi

echo "Starting webserv with $(basename "$CONFIG") ..."
# Config paths like "root www/" are relative to webserv's cwd, not to the
# config file's location, so we run from PROJECT_ROOT regardless of where
# this script was invoked from.
(cd "$PROJECT_ROOT" && exec "$BINARY" "$CONFIG") > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
	echo "webserv failed to start, see $SERVER_LOG" >&2
	exit 1
fi

echo "webserv running (pid $SERVER_PID) on $HOST:$PORT"
echo

mapfile -t TEST_FILES < <(find "$SCRIPT_DIR" -maxdepth 1 -type f -name "*.http" | sort)

if [ "${#TEST_FILES[@]}" -eq 0 ]; then
	echo "No test request files found in $SCRIPT_DIR" >&2
	exit 1
fi

run_test() {
	local file="$1"
	echo "=============================================="
	echo "Request: $(basename "$file")"
	echo "----------------------------------------------"
	cat "$file"
	echo
	echo "----------------------------------------------"
	echo "Response:"
	timeout "$REPLY_TIMEOUT" nc -q 1 "$HOST" "$PORT" < "$file"
	echo
	echo "=============================================="
	echo
}

while true; do
	echo "Pick a test to run:"
	for i in "${!TEST_FILES[@]}"; do
		printf "  %2d) %s\n" "$((i + 1))" "$(basename "${TEST_FILES[$i]}")"
	done
	echo "   a) run all"
	echo "   q) quit"
	read -rp "> " choice || break

	case "$choice" in
		q|Q) break ;;
		a|A)
			for f in "${TEST_FILES[@]}"; do
				run_test "$f"
				read -rp "Press enter for the next test..." _ || break
			done
			;;
		''|*[!0-9]*)
			echo "Please enter a number, 'a', or 'q'."
			;;
		*)
			idx=$((choice - 1))
			if [ "$idx" -ge 0 ] && [ "$idx" -lt "${#TEST_FILES[@]}" ]; then
				run_test "${TEST_FILES[$idx]}"
			else
				echo "No such test: $choice"
			fi
			;;
	esac
	echo
done

echo "Stopping webserv..."
