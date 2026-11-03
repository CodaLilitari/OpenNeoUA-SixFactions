#!/usr/bin/env python3
"""Assemble one OpenNeoUA Linux runtime directory from retail data + the repo Data overlay.

Layout produced (matches tools/linux/package.py and the README "Data-first" rule):
  OUT/OpenNeoUA            copied engine binary
  OUT/run-openneoua.sh     launcher (cd to its own dir, exec ./OpenNeoUA "$@")
  OUT/Data/...             retail DATA contents + retail LEVELS/LOCALE/ENV/HELP/SAVE/MUSIC
                           + NUCLEUS.INI, then the repo overlay on top (overlay wins)

Linux specifics handled here:
  * The engine VFS (src/system/fsmgr.cpp _scanDir) only indexes DT_REG/DT_DIR entries,
    so symlinks are invisible: everything is copied as real files.
  * The VFS matches names case-insensitively but returns the FIRST match, so retail
    SCRIPTS/SOUNDS/LEVELS/LOCALE/ENV and repo Scripts/Sounds/Levels/Locale/Env must be
    merged into ONE directory each.  Directory casing of the repo overlay wins.
  * Unattended INI edits: gfx.movie_player=no, menu.windowed=on, game.new.debug=no
    (normal F5-F8 bindings), game.crash_diagnostics=yes.
  * Retail DATA/MOV is left out unless --keep-movies: the first load of the retail
    SDU7 profile resets the options and writes gfx.movie_player=yes back into
    NUCLEUS.INI, and with intro.mpg present the engine then sits in the intro for
    about a minute before the menu (and the agent bridge) appear.

Each running instance needs its own copy (Env, Save and the INI files are written):
assemble one base, then copy it per instance with real files (cp -a, no symlinks).
Inputs are only read.  OUT must not exist.
"""
import argparse
import os
from pathlib import Path
import shutil
import sys

REPO_DIRS = ('Env', 'Filters', 'Fonts', 'Interface', 'Levels', 'Locale', 'Models', 'Res',
             'Scripts', 'Sounds', 'Wireless', 'svg')
REPO_FILES = ('OpenNeoUA.ini', 'OpenNeoUA_Parameter_Guide.ini', 'License.txt')
RETAIL_SUPPORT = ('LEVELS', 'LOCALE', 'ENV', 'HELP', 'SAVE', 'MUSIC')
LAUNCHER = '#!/usr/bin/env bash\nset -euo pipefail\ncd -- "$(dirname -- "${BASH_SOURCE[0]}")"\nexec ./OpenNeoUA "$@"\n'


class Tree:
    """Canonical, case-insensitive destination tree under OUT/Data."""

    def __init__(self, root):
        self.root = root
        self.dirs = {'': ''}          # lower relative dir -> canonical relative dir
        self.files = {}               # lower relative file -> canonical relative file

    def register_dir(self, rel):
        parts = [p for p in rel.split('/') if p]
        canon = ''
        for part in parts:
            key = (canon + '/' + part).lower().lstrip('/')
            if key not in self.dirs:
                self.dirs[key] = (canon + '/' + part).lstrip('/')
            canon = self.dirs[key]
        return canon

    def put(self, src, rel):
        parent, name = os.path.split(rel)
        parent = self.register_dir(parent)
        dst_rel = (parent + '/' + name).lstrip('/')
        old = self.files.get(dst_rel.lower())
        if old is not None and old != dst_rel:
            (self.root / old).unlink()            # same name, other case: overlay wins
        dst = self.root / dst_rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.is_symlink():
            raise SystemExit('refusing symlink in output: ' + str(dst))
        shutil.copyfile(src, dst)
        self.files[dst_rel.lower()] = dst_rel


def walk_files(base):
    for cur, dirs, files in os.walk(base, followlinks=False):
        dirs.sort()
        for f in sorted(files):
            p = Path(cur) / f
            if p.is_symlink() or not p.is_file():
                continue
            yield p, p.relative_to(base).as_posix()


def set_ini(path, key, value):
    lines = path.read_text(encoding='latin-1').splitlines() if path.exists() else []
    out, done = [], False
    for line in lines:
        head = line.split(';', 1)[0].split('=', 1)[0].strip().lower()
        if head == key.lower():
            if not done:
                out.append(f'{key} = {value}')
                done = True
            continue
        out.append(line)
    if not done:
        out.append(f'{key} = {value}')
    path.write_text('\n'.join(out) + '\n', encoding='latin-1')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--retail', required=True, type=Path, help='retail package root (has DATA/, LEVELS/, NUCLEUS.INI)')
    ap.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[3],
                    help='clean OpenNeoUA checkout (overlay source); default: this checkout')
    ap.add_argument('--binary', required=True, type=Path, help='built engine binary')
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--extra-levels', type=Path, help='generated map package (has Levels/Multi/*.ldf)')
    ap.add_argument('--movie-player', default='no')
    ap.add_argument('--new-debug', default='no')
    ap.add_argument('--menu-windowed', default='on', help='windowed 640x480 shell keeps agent click coordinates 1:1')
    ap.add_argument('--crash-diagnostics', default='yes', help='Data/Env/Diagnostics/<session>/ hang+crash reports')
    ap.add_argument('--low-cpu', action='store_true', help='disable CPU-heavy post effects for llvmpipe')
    ap.add_argument('--keep-movies', action='store_true', help='also copy retail DATA/MOV (see the note above)')
    a = ap.parse_args()
    out = a.out.resolve()
    if out.exists():
        sys.exit('output exists: ' + str(out))
    data = out / 'Data'
    data.mkdir(parents=True)
    tree = Tree(data)
    for d in REPO_DIRS:                       # register repo casing first
        if (a.repo / d).is_dir():
            for cur, dirs, _ in os.walk(a.repo / d):
                tree.register_dir(Path(cur).relative_to(a.repo).as_posix())
    retail_data = next((a.retail / n for n in ('DATA', 'Data', 'data') if (a.retail / n).is_dir()), None)
    if retail_data is None:
        sys.exit('retail DATA directory not found')
    skipped = 0
    for p, rel in walk_files(retail_data):
        if not a.keep_movies and rel.split('/', 1)[0].lower() == 'mov':
            skipped += 1
            continue
        tree.put(p, rel)
    for name in RETAIL_SUPPORT:
        src = next((a.retail / n for n in os.listdir(a.retail) if n.lower() == name.lower()), None)
        if src is not None and src.is_dir():
            for p, rel in walk_files(src):
                tree.put(p, src.name + '/' + rel)
    nucleus = next((a.retail / n for n in os.listdir(a.retail) if n.lower() == 'nucleus.ini'), None)
    if nucleus is not None:
        tree.put(nucleus, 'NUCLEUS.INI')
    for d in REPO_DIRS:
        if (a.repo / d).is_dir():
            for p, rel in walk_files(a.repo / d):
                tree.put(p, d + '/' + rel)
    for f in REPO_FILES:
        if (a.repo / f).is_file():
            tree.put(a.repo / f, f)
    if a.extra_levels:
        for p, rel in walk_files(a.extra_levels / 'Levels'):
            tree.put(p, 'Levels/' + rel)
    tree.register_dir('Save')                 # profiles go to Data/Save when it exists
    (data / tree.dirs['save']).mkdir(parents=True, exist_ok=True)
    shutil.copyfile(a.binary, out / 'OpenNeoUA')
    os.chmod(out / 'OpenNeoUA', 0o755)
    (out / 'run-openneoua.sh').write_text(LAUNCHER)
    os.chmod(out / 'run-openneoua.sh', 0o755)
    nuc = data / tree.files.get('nucleus.ini', 'NUCLEUS.INI')
    set_ini(nuc, 'gfx.movie_player', a.movie_player)
    set_ini(nuc, 'menu.windowed', a.menu_windowed)
    ini = data / 'OpenNeoUA.ini'
    set_ini(ini, 'game.new.debug', a.new_debug)
    set_ini(ini, 'game.crash_diagnostics', a.crash_diagnostics)
    if a.low_cpu:
        for k, v in (('gfx.horizon_fog_enable', 'no'), ('gfx.horizon_dark_enable', 'no'),
                     ('gfx.atmosphere_strength', '0'), ('gfx.palette_filter_strength', '0'),
                     ('gfx.vhs_filter_strength', '0'), ('gfx.particles.limit', '1000'),
                     ('gfx.ground_decal_limit', '32'), ('gfx.ground_decal_smoke_limit', '32')):
            set_ini(ini, k, v)
    missing = [n for n in ('set46', 'mc2res', 'set1', 'fonts') if n not in tree.dirs]
    if not any(f.startswith('locale/') and f.endswith('.dll') for f in tree.files):
        missing.append('locale/*.dll')
    print(f'files={len(tree.files)} dirs={len(tree.dirs)} movies_left_out={skipped} out={out}')
    if missing:
        print('WARNING: the runtime lacks', ', '.join('Data/' + m.upper() for m in missing),
              '- the engine stops at init without them; supply the complete retail package.')


if __name__ == '__main__':
    main()
