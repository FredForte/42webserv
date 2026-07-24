#!/usr/bin/env python3
# Request reflector for the browser test panel. A page can't read the headers
# the browser actually puts on the wire (Host, User-Agent, Accept, ...), so we
# reconstruct the request from the CGI environment webserv handed us and echo
# it straight back. This is the request *as webserv parsed it*.
import os
import sys

length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
body = sys.stdin.read(length) if length > 0 else ""

method = os.environ.get("REQUEST_METHOD", "?")
path = os.environ.get("SCRIPT_NAME", "/cgi-bin/reflect.py")
query = os.environ.get("QUERY_STRING", "")
target = path + ("?" + query if query else "")
protocol = os.environ.get("SERVER_PROTOCOL", "HTTP/1.1")

# Rebuild the header block. webserv exposes request headers as HTTP_<NAME>
# (dashes -> underscores, uppercased); Content-Type/Length come without the
# HTTP_ prefix. Turn them back into conventional "Title-Case" header names.
def unmangle(name):
    return "-".join(part.capitalize() for part in name.split("_"))

headers = []
for key, value in os.environ.items():
    if key.startswith("HTTP_"):
        headers.append((unmangle(key[5:]), value))
if "CONTENT_TYPE" in os.environ:
    headers.append(("Content-Type", os.environ["CONTENT_TYPE"]))
if length > 0:
    headers.append(("Content-Length", str(length)))
headers.sort()

lines = [method + " " + target + " " + protocol]
for name, value in headers:
    lines.append(name + ": " + value)
lines.append("")            # blank line separating headers from body
lines.append(body)

sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("\n".join(lines))
