#!/usr/bin/env python3
"""Serve the Web Bluetooth config page for EINK-N51802 on localhost.

Web Bluetooth only works in a secure context: https, or http on localhost.
Serving on 127.0.0.1 is therefore enough - no certificates needed.

    python tools/serve_web.py            # http://127.0.0.1:8080
    python tools/serve_web.py 9000       # custom port
    python tools/serve_web.py --no-open  # do not launch a browser
"""
import functools
import http.server
import os
import sys
import threading
import webbrowser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # the page is edited often while debugging; never cache it
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write("[web] " + (fmt % args) + "\n")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    port = int(args[0]) if args else 8080
    open_browser = "--no-open" not in sys.argv

    if not (WEB / "index.html").is_file():
        sys.exit(f"missing {WEB / 'index.html'}")

    handler = functools.partial(Handler, directory=str(WEB))
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)

    url = f"http://127.0.0.1:{port}/index.html"
    print(f"EINK-N51802 config page: {url}")
    print("Ctrl+C to stop. Use Chrome/Edge; the tag must be advertising.")

    if open_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
