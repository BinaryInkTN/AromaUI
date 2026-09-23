#!/usr/bin/env python3
"""AromaUI package store server (standard library only).

Hosts a directory of .apak bundles for the car infotainment "Packages"
store app, which browses, downloads and installs them over HTTP.

Repository layout (the served directory):
    <repo>/
        com.example.calc-1.0.0.apak
        com.example.notes-2.1.0.apak
        ...

Endpoints:
    GET /api/packages          JSON index of all valid packages
    GET /api/download/<id>     the newest .apak bytes for a package id
    GET /                      tiny human-readable listing (debugging)

Usage:
    python3 tools/aroma_store_server.py --dir ./store_repo --port 8080
    # on the car (or same machine), point the Packages store at
    # http://<server-ip>:8080 -- e.g. default http://127.0.0.1:8080

    # seed a repo from freshly built first-party bundles:
    mkdir -p store_repo && cp examples/car_infotainment/build/dist/*.apak store_repo/
"""

import argparse
import hashlib
import json
import os
import re
import sys
import time
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, unquote

MANIFEST_NAME = "manifest.json"


def read_manifest(apak_path):
    """Return (manifest_dict, error_str)."""
    try:
        with zipfile.ZipFile(apak_path, "r") as z:
            try:
                raw = z.read(MANIFEST_NAME)
            except KeyError:
                return None, "no manifest.json inside"
            try:
                m = json.loads(raw.decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError) as e:
                return None, "bad manifest.json: %s" % e
            if not isinstance(m, dict) or not m.get("id") or not m.get("name"):
                return None, "manifest missing id/name"
            if not re.match(r"^[a-z0-9_]+(\.[a-z0-9_]+)+$", m["id"]):
                return None, "manifest id is not reverse-dns"
            return m, None
    except zipfile.BadZipFile as e:
        return None, "not a zip: %s" % e
    except OSError as e:
        return None, "unreadable: %s" % e


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def build_index(repo_dir):
    """Scan *.apak files. Newest version_code wins per id."""
    entries = {}
    try:
        names = sorted(os.listdir(repo_dir))
    except OSError as e:
        return {}, "cannot list %s: %s" % (repo_dir, e)
    for fn in names:
        if not fn.endswith(".apak"):
            continue
        path = os.path.join(repo_dir, fn)
        try:
            st = os.stat(path)
        except OSError:
            continue
        manifest, err = read_manifest(path)
        if err:
            print("store: skipping %s (%s)" % (fn, err), flush=True)
            continue
        pid = manifest["id"]
        code = manifest.get("version_code", 0)
        try:
            code = int(code)
        except (TypeError, ValueError):
            code = 0
        prev = entries.get(pid)
        if prev is not None and prev["_code"] >= code:
            continue
        # Real data only: what the manifest declares. Missing rating /
        # counts stay zero ("New" / hidden in the client) - never
        # synthesized.
        try:
            rating = float(manifest.get("rating", 0.0))
        except (TypeError, ValueError):
            rating = 0.0
        rating = max(0.0, min(5.0, rating))
        try:
            rating_count = int(manifest.get("rating_count", 0))
        except (TypeError, ValueError):
            rating_count = 0
        try:
            downloads = int(manifest.get("downloads", 0))
        except (TypeError, ValueError):
            downloads = 0
        category = str(manifest.get("category", "") or "").strip() or "Apps"
        featured = bool(manifest.get("featured", False))
        entries[pid] = {
            "id": pid,
            "name": manifest.get("name", pid),
            "version": str(manifest.get("version", "?")),
            "version_code": code,
            "author": manifest.get("author", ""),
            "description": manifest.get("description", ""),
            "icon": manifest.get("icon", "AROMA_ICON_WIDGETS"),
            "category": category,
            "rating": rating,
            "rating_count": max(0, rating_count),
            "downloads": max(0, downloads),
            "featured": featured,
            "size": st.st_size,
            "sha256": sha256_of(path),
            "file": fn,
            "_code": code,
        }
    return entries, None


def filter_entries(entries, category=None, query=None, featured_only=False,
                   sort=None):
    """Apply Play-style filtering/sorting to an index dict. Returns list."""
    out = []
    q = (query or "").strip().lower()
    for pid in sorted(entries):
        e = dict(entries[pid])
        e.pop("_code", None)
        e.pop("file", None)
        e["url"] = "/api/download/%s" % pid
        if featured_only and not e.get("featured"):
            continue
        if category and category.lower() not in ("all", "") and \
                e.get("category", "").lower() != category.lower():
            continue
        if q:
            hay = " ".join(str(e.get(k, "")) for k in
                           ("name", "id", "author", "description",
                            "category")).lower()
            if q not in hay:
                continue
        out.append(e)
    if sort == "downloads":
        out.sort(key=lambda e: e.get("downloads", 0), reverse=True)
    elif sort == "rating":
        out.sort(key=lambda e: (e.get("rating", 0), e.get("rating_count", 0)),
                 reverse=True)
    elif sort == "name":
        out.sort(key=lambda e: e.get("name", "").lower())
    return out


class StoreHandler(BaseHTTPRequestHandler):
    server_version = "AromaStore/1.0"

    def log_message(self, fmt, *args):
        sys.stdout.write("[store %s] %s\n" % (
            self.client_address[0], fmt % args))
        sys.stdout.flush()

    def _send_json(self, obj, code=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        repo = self.server.repo_dir
        parsed = urlparse(self.path)
        path = unquote(parsed.path)
        from urllib.parse import parse_qs
        qs = parse_qs(parsed.query)
        def qparam(name, default=""):
            v = qs.get(name, [default])
            return v[0] if v else default
        if path in ("/api/packages", "/api/search"):
            entries, err = build_index(repo)
            if err:
                self._send_json({"error": err}, 500)
                return
            pkgs = filter_entries(
                entries,
                category=qparam("category") or None,
                query=qparam("q") or qparam("query") or None,
                featured_only=qparam("featured") == "1",
                sort=qparam("sort") or None)
            self._send_json({"packages": pkgs})
        elif path == "/api/categories":
            entries, err = build_index(repo)
            if err:
                self._send_json({"error": err}, 500)
                return
            cats = sorted({e.get("category", "Apps") for e in entries.values()})
            counts = {c: sum(1 for e in entries.values()
                             if e.get("category") == c) for c in cats}
            self._send_json({"categories": cats, "counts": counts,
                             "total": len(entries)})
        elif path == "/api/featured":
            entries, err = build_index(repo)
            if err:
                self._send_json({"error": err}, 500)
                return
            pkgs = filter_entries(entries, featured_only=True)
            if not pkgs:
                # Fall back to top-rated so the carousel is never empty.
                pkgs = filter_entries(entries, sort="rating")[:3]
            self._send_json({"packages": pkgs})
        elif path.startswith("/api/package/"):
            pid = unquote(path[len("/api/package/"):])
            entries, err = build_index(repo)
            if err:
                self._send_json({"error": err}, 500)
                return
            e = entries.get(pid)
            if not e:
                self._send_json({"error": "unknown package"}, 404)
                return
            d = dict(e)
            d.pop("_code", None)
            d.pop("file", None)
            d["url"] = "/api/download/%s" % pid
            self._send_json({"package": d})
        elif path.startswith("/api/download/"):
            pid = unquote(path[len("/api/download/"):])
            if not pid or "/" in pid or pid in (".", ".."):
                self._send_json({"error": "bad id"}, 400)
                return
            entries, err = build_index(repo)
            if err:
                self._send_json({"error": err}, 500)
                return
            e = entries.get(pid)
            if not e:
                self._send_json({"error": "unknown package"}, 404)
                return
            fpath = os.path.join(repo, e["file"])
            try:
                size = os.path.getsize(fpath)
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(size))
                self.send_header("Content-Disposition",
                                 'attachment; filename="%s"' % e["file"])
                self.end_headers()
                with open(fpath, "rb") as f:
                    while True:
                        chunk = f.read(65536)
                        if not chunk:
                            break
                        self.wfile.write(chunk)
            except (OSError, BrokenPipeError, ConnectionResetError) as ex:
                print("store: download of %s aborted: %s" % (pid, ex),
                      flush=True)
        elif path == "/":
            entries, _ = build_index(repo)
            cats = {}
            for e in entries.values():
                cats.setdefault(e.get("category", "Apps"), []).append(e)
            sections = ""
            for cat in sorted(cats):
                items = "".join(
                    "<li><b>%s</b> %s - %s (rating %.1f, %d downloads)</li>" % (
                        e["name"], e["version"], e["id"], e["rating"],
                        e["downloads"])
                    for e in sorted(cats[cat], key=lambda x: x["name"]))
                sections += "<h2>%s (%d)</h2><ul>%s</ul>" % (cat, len(cats[cat]), items)
            html = ("<html><body><h1>Aroma Store - %d packages</h1>"
                    "<p>API: /api/packages, /api/search?q=, /api/categories, "
                    "/api/featured, /api/package/&lt;id&gt;</p>%s"
                    "</body></html>" % (len(entries), sections))
            body = html.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self._send_json({"error": "not found"}, 404)


def main(argv=None):
    ap = argparse.ArgumentParser(description="AromaUI package store server")
    ap.add_argument("--dir", default="store_repo",
                    help="directory of .apak files (default: store_repo)")
    ap.add_argument("--host", default="0.0.0.0",
                    help="bind host (default: 0.0.0.0)")
    ap.add_argument("--port", type=int, default=8080,
                    help="bind port (default: 8080)")
    args = ap.parse_args(argv)
    if not os.path.isdir(args.dir):
        print("store: creating repo dir %s" % args.dir, flush=True)
        os.makedirs(args.dir, exist_ok=True)
    entries, err = build_index(args.dir)
    if err:
        print("store: %s" % err, flush=True)
        return 1
    print("store: serving %d package(s) from %s on %s:%d" % (
        len(entries), os.path.abspath(args.dir), args.host, args.port),
        flush=True)
    for pid in sorted(entries):
        e = entries[pid]
        print("store:   %s %s (%d bytes)" % (e["id"], e["version"], e["size"]),
              flush=True)
    server = ThreadingHTTPServer((args.host, args.port), StoreHandler)
    server.repo_dir = os.path.abspath(args.dir)
    server.daemon_threads = True
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
