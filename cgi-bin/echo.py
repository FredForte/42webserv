#!/usr/bin/env python3
# Regression fixture: echoes request method, body length, and body so tests can
# verify POST-to-stdin and environment delivery.
import os
import sys

length = int(os.environ.get("CONTENT_LENGTH", "0"))
body = sys.stdin.read(length)

sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("method=" + os.environ.get("REQUEST_METHOD", "?") + "\n")
sys.stdout.write("len=" + str(length) + "\n")
sys.stdout.write("body=[" + body + "]\n")
