#!/bin/bash
# Re-post every offering captured in board-backup.json, oldest first, keeping
# each one's original timestamp and author id. Run after `pio run -t uploadfs`,
# which replaces the whole LittleFS partition and therefore erases /board.dat.
# Caveat: device name and MAC are learned from whoever sends the POST, so every
# restored offering is re-attributed to the machine running this script, not to
# whoever originally wrote it. Those labels are RAM-only and reset on reboot.
HOST="${1:-SantaMuerte.local}"
python3 - "$HOST" <<'PY'
import json, subprocess, sys
host = sys.argv[1]
posts = json.load(open('board-backup.json'))['posts']
for p in sorted(posts, key=lambda x: x['id']):
    if p.get('hasImage'):
        print(f"  #{p['id']}: SKIPPED (image bytes were not backed up)"); continue
    if p['authorId'] and p['authorId'] < 1000:
        # 1 = NFC, 2 = USB. /api/board/post only accepts the browser range, so a
        # badge-made offering cannot be re-attributed from here and would come
        # back as plain "Anonymous". Re-post it from the TUI to restore the tag.
        print(f"  #{p['id']}: transport tag ({'NFC' if p['authorId']==1 else 'USB'}) "
              f"cannot be restored over HTTP -- re-post from the TUI: {p['text'][:40]!r}")
        continue
    r = subprocess.run(['curl','-s','-m','15','-X','POST',
        f'http://{host}/api/board/post',
        '--data-urlencode', f"text={p['text']}",
        '--data-urlencode', f"ts={p['createdAt']}",
        '--data-urlencode', f"authorId={p['authorId']}"],
        capture_output=True, text=True)
    ok = '"ok":true' in r.stdout
    print(f"  #{p['id']} {'restored' if ok else 'FAILED'}: {p['text'][:50]!r}")
PY
