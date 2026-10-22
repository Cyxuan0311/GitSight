#!/usr/bin/env python3
"""Writes an offline fixture set for the benchmark.

GitSight serves canned responses from a directory when GS_FIXTURE_DIR is set,
which short-circuits the whole network layer. This writes one that holds
`count` items per resource kind, so a run can be timed at a known size.

Usage: python3 tools/make_fixture.py <dir> [count]
"""
import datetime
import json
import os
import sys


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    out = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
    os.makedirs(out, exist_ok=True)

    # One day old, so the items fall inside any window the benchmark uses.
    stamp = (datetime.datetime.utcnow() - datetime.timedelta(days=1)).strftime("%Y-%m-%dT%H:%M:%SZ")

    def write(name, data):
        with open(os.path.join(out, name), "w") as fp:
            json.dump(data, fp)

    write("prs.json", [
        {
            "number": i,
            "title": f"Fix the widget number {i} with a reasonably long title",
            "state": "merged",
            "created_at": stamp, "updated_at": stamp, "merged_at": stamp,
            "html_url": f"https://example.test/p/{i}",
            "user": {"login": f"user{i % 50}"},
            "labels": [{"name": "bug"}, {"name": "ui"}, {"name": "enhancement"}],
        } for i in range(count)
    ])
    write("commits.json", [
        {
            "sha": f"{i:040x}",
            "html_url": f"https://example.test/c/{i}",
            "commit": {
                "message": f"fix the widget\n\nlong body line for commit {i}",
                "author": {"name": f"user{i % 50}", "date": stamp},
            },
        } for i in range(count)
    ])
    write("issues.json", [
        {
            "number": i,
            "title": f"Something broke in module {i}",
            "state": "closed",
            "created_at": stamp, "updated_at": stamp, "closed_at": stamp,
            "html_url": f"https://example.test/i/{i}",
            "user": {"login": f"user{i % 50}"},
            "labels": [{"name": "bug"}],
        } for i in range(count)
    ])
    write("releases.json", [
        {
            "tag_name": f"v1.{i}.0", "name": f"Release 1.{i}.0",
            "published_at": stamp, "html_url": f"https://example.test/r/{i}",
            "author": {"login": "alice"}, "body": "release notes " * 20, "draft": False,
        } for i in range(count)
    ])
    write("contributors.json", [
        {"login": f"user{i}", "contributions": max(1, 100 - i)} for i in range(count)
    ])
    write("languages.json", {"C": 61000, "Go": 39000, "Python": 12000})
    write("meta.json", {
        "stargazers_count": 1234, "forks_count": 56, "description": "demo repo",
        "language": "C", "default_branch": "main", "license": {"spdx_id": "MIT"},
    })
    print(f"wrote {count} items per kind to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
