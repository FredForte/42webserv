#!/usr/bin/env python3
# Regression fixture for CGI path variables: prints the resolved on-disk
# location variables so a test can assert they follow the location's alias root
# (e.g. /scripts/pathinfo.py -> cgi-bin/pathinfo.py) rather than the request URL.
import os
import sys

sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("SCRIPT_NAME=" + os.environ.get("SCRIPT_NAME", "?") + "\n")
sys.stdout.write("PATH_INFO=" + os.environ.get("PATH_INFO", "?") + "\n")
sys.stdout.write("SCRIPT_FILENAME=" + os.environ.get("SCRIPT_FILENAME", "?") + "\n")
sys.stdout.write("REQUEST_URI=" + os.environ.get("REQUEST_URI", "?") + "\n")
