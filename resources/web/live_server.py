#!/usr/bin/env python3
"""CrucibleCode live preview server (Python standard library only).

  * serves a folder over http://127.0.0.1:<port>/
  * injects a tiny client script into every HTML page
  * watches the folder; when a file is saved the open browser tabs reload
    (CSS-only changes are applied without a page reload)

Started by CrucibleCode:   python live_server.py --root <folder> --port 5500
"""
import argparse
import functools
import os
import queue
import re
import sys
import threading
import time
import urllib.parse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

CLIENT_JS = b"""(function () {
  if (window.__crucibleLive) return;
  window.__crucibleLive = true;
  var wasDown = false;
  function refreshCss() {
    var links = document.querySelectorAll('link[rel~="stylesheet"]');
    for (var i = 0; i < links.length; i++) {
      (function (l) {
        var u;
        try { u = new URL(l.href, location.href); } catch (e) { return; }
        if (u.origin !== location.origin) return;
        u.searchParams.set('_crucible', Date.now());
        var n = l.cloneNode();
        n.href = u.href;
        n.onload = function () { if (l.parentNode) l.parentNode.removeChild(l); };
        l.parentNode.insertBefore(n, l.nextSibling);
      })(links[i]);
    }
  }
  function connect() {
    var es = new EventSource('/__crucible/events');
    es.onopen = function () { if (wasDown) location.reload(); wasDown = false; };
    es.onmessage = function (e) {
      if (e.data === 'reload') location.reload();
      else if (e.data === 'css') refreshCss();
    };
    es.onerror = function () { wasDown = true; es.close(); setTimeout(connect, 1000); };
  }
  connect();
})();
"""

SNIPPET = b'<script src="/__crucible/client.js"></script>'
BODY_END = re.compile(rb"</body\s*>", re.I)

WATCH_EXT = {".html", ".htm", ".css", ".js", ".mjs", ".cjs", ".json", ".svg", ".png", ".jpg", ".jpeg",
             ".gif", ".webp", ".ico", ".woff", ".woff2", ".ttf", ".otf", ".xml", ".txt", ".wasm"}
SKIP_DIRS = {".git", ".svn", ".hg", "node_modules", "__pycache__", ".idea", ".vs", ".vscode"}


class Hub:
    """Fan-out of reload events to every connected browser tab."""

    def __init__(self):
        self.lock = threading.Lock()
        self.clients = []

    def add(self):
        q = queue.Queue()
        with self.lock:
            self.clients.append(q)
        return q

    def remove(self, q):
        with self.lock:
            if q in self.clients:
                self.clients.remove(q)

    def broadcast(self, kind):
        with self.lock:
            for q in self.clients:
                q.put(kind)


HUB = Hub()


class Handler(SimpleHTTPRequestHandler):
    server_version = "CrucibleLive/1.0"

    def log_message(self, fmt, *args):
        if "/__crucible/" in (self.path or ""):
            return
        sys.stdout.write("%s %s\n" % (self.command, urllib.parse.unquote(self.path)))
        sys.stdout.flush()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        if path == "/__crucible/events":
            return self.serve_events()
        if path == "/__crucible/client.js":
            self.send_response(200)
            self.send_header("Content-Type", "application/javascript")
            self.send_header("Content-Length", str(len(CLIENT_JS)))
            self.end_headers()
            self.wfile.write(CLIENT_JS)
            return

        fs = self.translate_path(self.path)
        if os.path.isdir(fs) and path.endswith("/"):
            for name in ("index.html", "index.htm"):
                cand = os.path.join(fs, name)
                if os.path.isfile(cand):
                    return self.serve_html(cand)
        elif os.path.isfile(fs) and fs.lower().endswith((".html", ".htm")):
            return self.serve_html(fs)
        return super().do_GET()

    def serve_html(self, fs):
        try:
            with open(fs, "rb") as f:
                data = f.read()
        except OSError:
            self.send_error(404, "File not found")
            return
        matches = list(BODY_END.finditer(data))
        if matches:
            i = matches[-1].start()
            data = data[:i] + SNIPPET + data[i:]
        else:
            data += SNIPPET
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def serve_events(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        q = HUB.add()
        try:
            self.wfile.write(b": connected\n\n")
            self.wfile.flush()
            while True:
                try:
                    kind = q.get(timeout=15)
                    self.wfile.write(("data: %s\n\n" % kind).encode("ascii"))
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            pass
        finally:
            HUB.remove(q)


def snapshot(root):
    result = {}
    count = 0
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            if os.path.splitext(name)[1].lower() not in WATCH_EXT:
                continue
            full = os.path.join(dirpath, name)
            try:
                result[full] = os.stat(full).st_mtime_ns
            except OSError:
                continue
            count += 1
            if count > 20000:
                return result
    return result


def watch(root, interval=0.3):
    previous = snapshot(root)
    while True:
        time.sleep(interval)
        current = snapshot(root)
        changed = [p for p in current if previous.get(p) != current[p]]
        changed += [p for p in previous if p not in current]
        previous = current
        if not changed:
            continue
        css_only = all(p.lower().endswith(".css") for p in changed)
        HUB.broadcast("css" if css_only else "reload")
        for p in changed[:3]:
            print("changed: %s" % os.path.relpath(p, root))
        sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--port", type=int, default=5500)
    ap.add_argument("--host", default="127.0.0.1")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print("ERROR folder not found: %s" % root)
        return 2

    handler = functools.partial(Handler, directory=root)
    ThreadingHTTPServer.daemon_threads = True
    try:
        server = ThreadingHTTPServer((args.host, args.port), handler)
    except OSError as e:
        print("ERROR cannot listen on %s:%d (%s)" % (args.host, args.port, e))
        return 3

    threading.Thread(target=watch, args=(root,), daemon=True).start()
    print("READY http://%s:%d/" % (args.host, args.port))
    sys.stdout.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
