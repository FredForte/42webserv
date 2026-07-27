#!/usr/bin/env python3
# Regression fixture for the "EOF before exit" race: writes a complete response,
# closes stdout, then stays alive for a moment before exiting.
#
# The server sees the pipe hit EOF while the child is still running, so it can't
# reap it yet. A pipe whose writer is gone stays readable forever, so if the
# server leaves it armed in epoll it gets handed straight back on every wakeup
# and spins at full CPU until the child finally exits (or cgi_timeout fires).
# The response must still come back normally, and cheaply.
import os
import sys
import time

sys.stdout.write("Content-Type: text/plain\r\n\r\nstdout-closed-early")
sys.stdout.flush()
os.close(1)
time.sleep(1.5)
