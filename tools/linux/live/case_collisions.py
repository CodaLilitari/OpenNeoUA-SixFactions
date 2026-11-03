"""Report case-insensitive path collisions between retail data and the repo Data overlay (read-only)."""
import os, sys
from collections import defaultdict
retail, repo = sys.argv[1], sys.argv[2]
ROOT_DIRS = ('Env','Filters','Fonts','Interface','Levels','Locale','Models','Res','Scripts','Sounds','Wireless','svg')
ROOT_FILES = ('OpenNeoUA.ini','OpenNeoUA_Parameter_Guide.ini','License.txt')
entries = defaultdict(list)
def add(src_root, rel_dst_prefix, origin):
    for cur, dirs, files in os.walk(src_root):
        rel = os.path.relpath(cur, src_root)
        for f in files:
            dst = os.path.normpath(os.path.join(rel_dst_prefix, rel, f))
            entries[dst.lower()].append((origin, dst))
# retail DATA/* -> Data/*
add(os.path.join(retail, 'DATA'), 'Data', 'retail')
for d in ('LEVELS','LOCALE','ENV','HELP'):
    add(os.path.join(retail, d), os.path.join('Data', d), 'retail')
entries['data/nucleus.ini'].append(('retail', 'Data/NUCLEUS.INI'))
for d in ROOT_DIRS:
    add(os.path.join(repo, d), os.path.join('Data', d), 'repo')
for f in ROOT_FILES:
    entries[os.path.join('data', f).lower()].append(('repo', os.path.join('Data', f)))
coll = {k: v for k, v in entries.items() if len(v) > 1}
dirs = defaultdict(set)
for k, v in entries.items():
    for origin, dst in v:
        parts = dst.split('/')
        for i in range(1, len(parts)):
            dirs['/'.join(parts[:i]).lower()].add('/'.join(parts[:i]))
dircase = {k: sorted(v) for k, v in dirs.items() if len(v) > 1}
print('files:', len(entries), 'file collisions (same path ignoring case):', len(coll))
for k, v in sorted(coll.items())[:40]:
    print('  ', v)
print('directories with case variants:', len(dircase))
for k, v in sorted(dircase.items()):
    print('  ', v)
