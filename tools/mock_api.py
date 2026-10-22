#!/usr/bin/env python3
"""A stand-in for the GitHub API, used to test pagination and the cache.

Serves 100 items per page and a `Link: rel="next"` header on page 1, exactly
like the real API, so a client that honours the header should see 200 items
across two pages -- and so should a client whose second run is served from the
cache.

Usage: python3 tools/mock_api.py <port> [pages]
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

PAGES = 2
PER_PAGE = 100
D = "2026-10-20T00:00:00Z"
STATE = {"requests": 0}


def items(kind, page):
    n = PER_PAGE if page <= PAGES else 0
    out = []
    for i in range(n):
        idx = (page - 1) * PER_PAGE + i
        if kind == "pulls":
            out.append({"number": idx, "title": f"PR {idx}", "state": "merged",
                        "created_at": D, "updated_at": D, "merged_at": D,
                        "html_url": f"https://example.test/p/{idx}",
                        "user": {"login": f"u{idx % 20}"}, "labels": [{"name": "bug"}]})
        elif kind == "issues":
            out.append({"number": idx, "title": f"Issue {idx}", "state": "closed",
                        "created_at": D, "updated_at": D, "closed_at": D,
                        "html_url": f"https://example.test/i/{idx}",
                        "user": {"login": f"u{idx % 20}"}, "labels": []})
        elif kind == "commits":
            out.append({"sha": f"{idx:040x}", "html_url": f"https://example.test/c/{idx}",
                        "commit": {"message": f"commit {idx}",
                                   "author": {"name": f"u{idx % 20}", "date": D}}})
        elif kind == "releases":
            out.append({"tag_name": f"v{idx}", "name": f"R{idx}", "published_at": D,
                        "html_url": f"https://example.test/r/{idx}",
                        "author": {"login": "alice"}, "body": "notes", "draft": False})
        else:
            out.append({"login": f"u{idx}", "contributions": 100 - idx})
    return out


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        url = urlparse(self.path)
        query = parse_qs(url.query)
        page = int(query.get("page", ["1"])[0])
        path = url.path

        # How many requests this mock has served, so a test can tell a run
        # that went to the network from one the cache answered.
        if path == "/__stats":
            body = json.dumps(STATE).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        STATE["requests"] += 1

        kind = None
        for candidate in ("pulls", "issues", "commits", "releases", "contributors"):
            if path.endswith("/" + candidate):
                kind = candidate
                break

        if kind is None:
            body = json.dumps({"stargazers_count": 1, "default_branch": "main"}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        body = json.dumps(items(kind, page)).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        if page < PAGES:
            nxt = f"http://127.0.0.1:{self.server.server_port}{path}?per_page=100&page={page + 1}"
            self.send_header("Link", f'<{nxt}>; rel="next"')
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        self.do_GET()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 18999
    pages = int(sys.argv[2]) if len(sys.argv) > 2 else PAGES
    PAGES = pages
    HTTPServer(("127.0.0.1", port), Handler).serve_forever()
