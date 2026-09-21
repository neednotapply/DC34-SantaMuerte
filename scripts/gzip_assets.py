"""Pre-build: keep a gzip twin (foo.css -> foo.css.gz) next to every text asset
in data/, so the web server can serve Content-Encoding: gzip.

Why this exists: the badge has ~46 KB of free heap and lwip's listen backlog is
compiled out, so it accepts all of a browser's parallel first-load requests at
once. Sending those pages and scripts uncompressed collapses the heap and wedges
the WiFi stack. The gzip twins are ~4x smaller, so the same burst fits.

Runs before every build (harmless for a firmware-only build; the twins only ship
with `uploadfs`). Regenerates a twin only when its source is newer, so it is
cheap on repeat builds. The twins are git-ignored -- they are build artifacts.
"""
Import("env")  # noqa: F821  (injected by PlatformIO)

import glob
import gzip
import os

TEXT_EXTENSIONS = (".html", ".css", ".js", ".json", ".svg", ".txt")

data_dir = os.path.join(env["PROJECT_DIR"], "data")  # noqa: F821
if os.path.isdir(data_dir):
    made = 0
    for source in glob.glob(os.path.join(data_dir, "**", "*"), recursive=True):
        if source.endswith(".gz") or not source.endswith(TEXT_EXTENSIONS):
            continue
        twin = source + ".gz"
        if os.path.exists(twin) and os.path.getmtime(twin) >= os.path.getmtime(source):
            continue
        with open(source, "rb") as raw:
            payload = raw.read()
        # mtime=0 keeps the output byte-identical when the input has not changed,
        # so an unchanged asset does not churn its twin (and its ETag) every build.
        with gzip.GzipFile(twin, "wb", compresslevel=9, mtime=0) as out:
            out.write(payload)
        made += 1
        print("gzip: data/%s" % os.path.relpath(twin, data_dir))
    if made:
        print("gzip_assets: refreshed %d asset(s)" % made)
