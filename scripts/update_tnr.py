"""Updates third_party/track-n-race from upstream (https://github.com/NoGoat/Track-N-Race, GPL-3.0, by NoGoat):
downloads the track maps and the licence of a commit (default: the newest on the default branch), records the
commit in third_party/track-n-race/SOURCE, then regenerates apps/dash-app/tracks/tnr_data.inc.

    python scripts/update_tnr.py            # newest commit
    python scripts/update_tnr.py <commit>   # a given commit

Then rebuild (cmake --build build) and run the tests.
"""
import json
import os
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, 'third_party', 'track-n-race')
REPO = 'NoGoat/Track-N-Race'
MAPS = 'electron-frontend/src/renderer/src/assets/maps'


def get(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'f1-race-update'})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def main():
    sha = sys.argv[1] if len(sys.argv) > 1 else json.loads(get('https://api.github.com/repos/%s/commits/HEAD' % REPO))['sha']
    tree = json.loads(get('https://api.github.com/repos/%s/git/trees/%s?recursive=1' % (REPO, sha)))['tree']
    files = [t['path'] for t in tree if t['type'] == 'blob' and t['path'].startswith(MAPS + '/track_') and t['path'].endswith('.json')]
    if not files:
        sys.exit('no maps found at %s in %s' % (MAPS, sha))
    os.makedirs(os.path.join(DEST, 'maps'), exist_ok=True)
    for old in os.listdir(os.path.join(DEST, 'maps')):
        os.remove(os.path.join(DEST, 'maps', old))
    raw = 'https://raw.githubusercontent.com/%s/%s/' % (REPO, sha)
    for path in files:
        with open(os.path.join(DEST, 'maps', os.path.basename(path)), 'wb') as f:
            f.write(get(raw + path))
    with open(os.path.join(DEST, 'LICENSE'), 'wb') as f:
        f.write(get(raw + 'LICENSE'))
    with open(os.path.join(DEST, 'SOURCE'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('https://github.com/%s\ncommit %s\nfiles %s/track_*.json (%d)\n' % (REPO, sha, MAPS, len(files)))
    print('%d maps from %s' % (len(files), sha))
    subprocess.check_call([sys.executable, os.path.join(ROOT, 'scripts', 'gen_tnr_tracks.py')])


if __name__ == '__main__':
    main()
