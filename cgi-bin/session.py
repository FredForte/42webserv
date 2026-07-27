#!/usr/bin/env python3
# Cookie / session demo. The server owns none of this: it hands us the client's
# Cookie header as HTTP_COOKIE and sends whatever Set-Cookie we emit back out,
# so the whole session lives here.
#
# First visit  -> no cookie, so mint a session id and Set-Cookie it, count = 1
# Later visits -> cookie comes back, we recognise it and the count goes up
#
# The count is kept in a file per session because every CGI run is a brand new
# process; nothing survives in memory between requests.
#
# Only ONE Set-Cookie is emitted on purpose. The server keeps extra CGI headers
# in a map keyed by name, so a second Set-Cookie would overwrite the first.
import os
import re
import secrets
import sys

STORE = "/tmp/webserv_sessions"

# A session id read back from a cookie is attacker-controlled and is about to
# become part of a filename, so accept only what we ourselves mint. Without this
# a "Cookie: sid=../../etc/passwd" would walk straight out of STORE.
VALID_ID = re.compile(r"^[0-9a-f]{32}$")


def read_cookie(name):
    raw = os.environ.get("HTTP_COOKIE", "")
    for pair in raw.split(";"):
        key, _, value = pair.strip().partition("=")
        if key == name:
            return value
    return None


def bump(session_id):
    path = os.path.join(STORE, session_id)
    count = 0
    try:
        with open(path) as f:
            count = int(f.read().strip() or 0)
    except (IOError, ValueError):
        count = 0
    count += 1
    with open(path, "w") as f:
        f.write(str(count))
    return count


os.makedirs(STORE, exist_ok=True)

session_id = read_cookie("session_id")
is_new = session_id is None or not VALID_ID.match(session_id)
if is_new:
    session_id = secrets.token_hex(16)

count = bump(session_id)

sys.stdout.write("Content-Type: text/html; charset=UTF-8\r\n")
if is_new:
    # Only sent on the first visit; afterwards the client already holds it.
    sys.stdout.write("Set-Cookie: session_id=%s; Path=/; HttpOnly\r\n" % session_id)
sys.stdout.write("\r\n")

sys.stdout.write("<html><body>\n")
if is_new:
    sys.stdout.write("<h1>Welcome, new visitor</h1>\n")
else:
    sys.stdout.write("<h1>Welcome back</h1>\n")
sys.stdout.write("<p>session_id: %s</p>\n" % session_id)
sys.stdout.write("<p>visits: %d</p>\n" % count)
sys.stdout.write("</body></html>\n")
