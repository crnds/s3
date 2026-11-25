"""Shared setup for the server tests (stdlib unittest, Python 3.9-safe).

`python3 -m unittest discover tests/server` puts this directory on sys.path,
so test files just `import helpers`. Importing usage_server starts no threads.
"""
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "server"))

import usage_server as u  # noqa: E402,F401  (re-exported for the tests)
