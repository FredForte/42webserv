#!/usr/bin/env python3
# Regression fixture: never outputs and never exits, so only cgi_timeout can
# retire it (server should answer 504 Gateway Timeout).
import time
time.sleep(30)
