"""The Mac server's code, imported by server/usage_server.py and
server/control_server.py.

Both are run by absolute path (launchd plists, no WorkingDirectory), so
sys.path[0] is server/ and this package is found as a sibling. Use absolute
imports (`from s3srv import state`) everywhere: a relative import breaks in a
file run by path. Never name a module here after a stdlib module.
"""
