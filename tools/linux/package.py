#!/usr/bin/env python3
"""Package a clean source checkout and an ELF binary; never an assembled game tree."""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import tarfile

ROOT_FILES = ('.gitignore', 'License.txt', 'README.md', 'OpenNeoUA.ini',
              'OpenNeoUA_Parameter_Guide.ini')
ROOT_DIRS = ('.github', 'src', 'tests', 'tools', 'docs', 'Env', 'Filters', 'Fonts',
             'Interface', 'Levels', 'Locale', 'Models', 'Res', 'Scripts', 'Sounds',
             'svg', 'Wireless')
LAUNCHER = b'''#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
exec ./OpenNeoUA "$@"
'''

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def command(args):
    return subprocess.check_output(args, text=True, timeout=30).strip()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--epoch', required=True, type=int)
    args = parser.parse_args()
    source, binary, output = (p.resolve() for p in (args.source, args.binary, args.output))
    if args.epoch < 0 or args.epoch > 0xffffffff:
        parser.error('epoch must fit the gzip timestamp field')
    if output.is_relative_to(source) or binary.is_relative_to(source):
        parser.error('binary and package must stay outside the source tree')
    if output.exists():
        parser.error('output already exists; choose a new package path')
    with binary.open('rb') as stream:
        if stream.read(4) != b'\x7fELF':
            parser.error('binary is not ELF')
    paths = []
    for name in ROOT_FILES:
        path = source / name
        if path.is_file():
            paths.append(path)
    for name in ROOT_DIRS:
        root = source / name
        if root.exists():
            paths.extend(p for p in root.rglob('*') if p.is_file())
    selected = []
    for path in paths:
        relative = path.relative_to(source)
        if '__pycache__' in relative.parts or path.suffix in ('.pyc', '.pyo'):
            continue
        if path.is_symlink() or not path.resolve().is_relative_to(source):
            parser.error('source contains a symlink or escaping path: ' + str(relative))
        if relative.as_posix().lower() == 'scripts/startup.scr':
            parser.error('retail scripts found: package a clean checkout, not merged game data')
        selected.append((relative.as_posix(), path))
    selected.sort()
    dynamic = command(['readelf', '-d', str(binary)])
    versions = command(['readelf', '--version-info', str(binary)])
    info = {
        'format': 1, 'source_date_epoch': args.epoch,
        'source_revision': os.environ.get('GITHUB_SHA', os.environ.get('SOURCE_REVISION', 'uncommitted-candidate')),
        'architecture': platform.machine(),
        'compiler': command(['c++', '--version']).splitlines()[0],
        'os_release': Path('/etc/os-release').read_text(),
        'binary_sha256': digest(binary),
        'needed_libraries': re.findall(r'Shared library: \[(.*?)\]', dynamic),
        'glibc_symbol_versions': sorted(set(re.findall(r'GLIBC_[0-9.]+', versions)),
                                       key=lambda v: tuple(map(int, v[6:].split('.')))),
        'files': {name: digest(path) for name, path in selected},
        'retail_data_included': False,
        'validation_limit': 'Build, tests and dependency evidence are separate from gameplay validation.'
    }
    metadata = (json.dumps(info, indent=2, sort_keys=True) + '\n').encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    prefix = 'OpenNeoUA-linux'

    def add(archive, name, size, stream, executable=False):
        item = tarfile.TarInfo(prefix + '/' + name)
        item.size, item.mtime = size, args.epoch
        item.uid = item.gid = 0
        item.uname = item.gname = ''
        item.mode = 0o755 if executable else 0o644
        archive.addfile(item, stream)

    with output.open('xb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=args.epoch) as zipped:
        with tarfile.open(fileobj=zipped, mode='w|', format=tarfile.PAX_FORMAT) as archive:
            with binary.open('rb') as stream:
                add(archive, 'OpenNeoUA', binary.stat().st_size, stream, True)
            add(archive, 'run-openneoua.sh', len(LAUNCHER), io.BytesIO(LAUNCHER), True)
            add(archive, 'BUILD-INFO.json', len(metadata), io.BytesIO(metadata))
            for name, path in selected:
                with path.open('rb') as stream:
                    add(archive, 'Data/' + name, path.stat().st_size, stream, path.suffix == '.sh')
    receipt = {'archive': output.name, 'bytes': output.stat().st_size,
               'sha256': digest(output), 'binary_sha256': info['binary_sha256'],
               'source_date_epoch': args.epoch, 'file_count': len(selected) + 3}
    output.with_suffix(output.suffix + '.json').write_text(json.dumps(receipt, indent=2) + '\n')
    output.with_suffix(output.suffix + '.sha256').write_text(receipt['sha256'] + '  ' + output.name + '\n')
    (output.parent / 'BUILD-INFO.json').write_bytes(metadata)
    print(json.dumps(receipt, indent=2))

if __name__ == '__main__':
    main()
