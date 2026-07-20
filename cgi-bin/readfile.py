#!/usr/bin/env python3
# Regression fixture for the CGI working directory: opens a sibling file by a
# RELATIVE path, which only resolves if the CGI was run from its own directory.
import sys
with open("cgi_data.txt") as f:
    data = f.read()
sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write(data)
