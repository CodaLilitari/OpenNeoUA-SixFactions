#!/usr/bin/env python3
"""Bundle a trusted Linux x86_64 build and resolved libraries, excluding glibc."""
import argparse
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import stat
import struct
import subprocess
import tarfile
import tempfile

PREFIX = 'OpenNeoUA-linux-runtime'
SAFE_NAME = re.compile(r'^[A-Za-z0-9_+.-]+$')
HOST_NAMES = {'libc.so.6', 'libm.so.6', 'libpthread.so.0', 'libdl.so.2', 'librt.so.1',
              'libresolv.so.2', 'libutil.so.1', 'libanl.so.1', 'libBrokenLocale.so.1',
              'libmvec.so.1', 'libthread_db.so.1', 'libmemusage.so', 'libpcprofile.so',
              'libSegFault.so', 'libcidn.so.1',
              'ld-linux-x86-64.so.2', 'ld-linux.so.2'}


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def command(arguments, optional=False):
    env = {k: v for k, v in os.environ.items() if not k.startswith('LD_')}
    env['LC_ALL'] = 'C'
    try:
        result = subprocess.run(arguments, capture_output=True, text=True, env=env, timeout=30)
    except FileNotFoundError:
        if optional:
            return ''
        raise
    if result.returncode:
        if optional:
            return ''
        raise ValueError(f'{arguments[0]} failed for {arguments[-1]}: {result.stderr.strip()}')
    return result.stdout


def is_host_runtime(name, package=None):
    owner = (package or '').split(':')[0]
    return (name in HOST_NAMES or owner in ('libc6', 'libc6-dev', 'libc-bin', 'libc-dev-bin')
            or bool(re.fullmatch(r'libnss_(?:compat|dns|files|hesiod)\.so\.2', name)))


def parse_ldd(text):
    result = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or re.fullmatch(r'linux-vdso\.so\.\d+\s+\(0x[0-9a-fA-F]+\)', line):
            continue
        match = re.fullmatch(r'(\S+)\s+=>\s+(/.+?)\s+\(0x[0-9a-fA-F]+\)', line)
        if match:
            name, path = match.groups()
        else:
            match = re.fullmatch(r'(/.+?)\s+\(0x[0-9a-fA-F]+\)', line)
            if not match:
                raise ValueError('Unresolved or unrecognized ldd entry: ' + line)
            path = match.group(1)
            name = PurePosixPath(path).name
        if not SAFE_NAME.fullmatch(name):
            raise ValueError('Unsafe dependency name: ' + name)
        if name in result and result[name] != Path(path):
            raise ValueError('Conflicting ldd paths for ' + name)
        result[name] = Path(path)
    if not result:
        raise ValueError('ldd returned no dependencies')
    return result


def parse_readelf(text):
    interpreter = re.search(r'\[Requesting program interpreter: ([^\]]+)\]', text)
    soname = re.search(r'\(SONAME\).*?\[([^\]]+)\]', text)
    rpath = [item for paths in re.findall(r'\(RPATH\).*?\[([^\]]*)\]', text) for item in paths.split(':')]
    runpath = [item for paths in re.findall(r'\(RUNPATH\).*?\[([^\]]*)\]', text) for item in paths.split(':')]
    return {'interpreter': interpreter.group(1) if interpreter else None,
            'soname': soname.group(1) if soname else None,
            'needed': re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', text),
            'rpath': rpath, 'runpath': runpath, 'search_paths': rpath + runpath,
            'glibc_versions': sorted(set(re.findall(r'\bGLIBC_\d+(?:\.\d+)+\b', text)),
                                     key=lambda v: tuple(map(int, v[6:].split('.'))))}


def validate_metadata(info, executable=False):
    interpreter = info['interpreter']
    if executable and not interpreter:
        raise ValueError('A dynamically linked glibc executable is required')
    if interpreter and interpreter not in ('/lib64/ld-linux-x86-64.so.2', '/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2'):
        raise ValueError('Nonstandard ELF interpreter: ' + interpreter)
    # DT_RPATH can take precedence over the launcher's bundled LD_LIBRARY_PATH.
    # DT_RUNPATH follows it; retain RUNPATH as evidence while auditing all NEEDED names.
    if any(path not in ('$ORIGIN', '${ORIGIN}') for path in info['rpath']):
        raise ValueError('Host-bound or unsupported ELF RPATH')
    if any(not SAFE_NAME.fullmatch(name) for name in info['needed']):
        raise ValueError('ELF dependency contains a path or unsafe name')


def inspect_elf(path, executable=False):
    with path.open('rb') as stream:
        header = stream.read(20)
    if len(header) < 20 or header[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', header, 18)[0] != 62:
        raise ValueError('Only little-endian Linux x86_64 ELF is supported: ' + str(path))
    if path.stat().st_mode & (stat.S_ISUID | stat.S_ISGID):
        raise ValueError('Set-id ELF files cannot use this relative library launcher')
    info = parse_readelf(command(['readelf', '--wide', '--file-header', '--program-headers',
                                  '--dynamic', '--version-info', str(path)]))
    validate_metadata(info, executable)
    return info


def has_git_parent(path):
    return any((parent / '.git').exists() for parent in (path, *path.parents))


def validate_locations(source, binary, output):
    if source.is_symlink() or binary.is_symlink() or output.is_symlink():
        raise ValueError('Source, binary and output arguments must not be symlinks')
    source, binary, output = (p.resolve() for p in (source, binary, output))
    if not source.is_dir() or not binary.is_file():
        raise ValueError('Source directory and external binary must exist')
    if binary.is_relative_to(source) or output.is_relative_to(source):
        raise ValueError('Generated binary and output must be outside the source tree')
    if has_git_parent(binary.parent) or has_git_parent(output.parent):
        raise ValueError('Generated binary and output must be outside all Git checkouts')
    if output.exists():
        raise FileExistsError(output)
    return source, binary, output


def resolve_library(path, roots):
    real = path.resolve(strict=True)
    if not real.is_file() or not any(real.is_relative_to(root.resolve()) for root in roots):
        raise ValueError('Library or symlink escapes the allowed roots: ' + str(path))
    return real


def collect_dependencies(binary, library_roots, mesa_swrast=None, mesa_glx=None):
    binary_info = inspect_elf(binary, executable=True)
    seeds = [(binary, None)]
    if mesa_swrast:
        driver = resolve_library(mesa_swrast, library_roots)
        glx = resolve_library(mesa_glx or mesa_swrast.parent.parent / 'libGLX_mesa.so.0', library_roots)
        seeds += [(driver, 'lib/dri/swrast_dri.so'), (glx, 'lib/libGLX_mesa.so.0')]
    elif mesa_glx:
        raise ValueError('--mesa-glx requires --mesa-swrast')
    found, destinations, metadata = {}, {}, {binary: binary_info}

    def register(name, path, destination=None):
        if not SAFE_NAME.fullmatch(name):
            raise ValueError('Unsafe library name: ' + name)
        real = resolve_library(path, library_roots)
        if name in found and digest(found[name]) != digest(real):
            raise ValueError('Different libraries resolve to the same name: ' + name)
        found[name] = real
        destinations[name] = destination or 'lib/' + name

    for seed, destination in seeds:
        if destination:
            metadata[seed] = inspect_elf(seed)
            register(PurePosixPath(destination).name, seed, destination)
        for name, path in parse_ldd(command(['ldd', str(seed)])).items():
            register(name, path)
    if len(found) > 256:
        raise ValueError('Dependency closure exceeds the supported 256-library bound')
    libraries, host = [], []
    for name, real in sorted(found.items()):
        if real not in metadata:
            metadata[real] = inspect_elf(real)
        info = metadata[real]
        record = {'name': name, 'source': str(real), 'sha256': digest(real),
                  'destination': destinations[name], 'provenance': package_provenance(real), **info}
        package = record['provenance']['package']
        (host if is_host_runtime(name, package) or is_host_runtime(info['soname'] or '', package) else libraries).append(record)
    available = set(found)
    for real, info in metadata.items():
        missing = set(info['needed']) - available
        if missing:
            raise ValueError(f'Incomplete ldd closure for {real}: {sorted(missing)}')
    return {'binary_metadata': binary_info, 'libraries': libraries, 'host_libraries': host}


def select_source(source):
    spec = importlib.util.spec_from_file_location('openneoua_source_package', Path(__file__).with_name('package.py'))
    package = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package)
    selected = {}

    def add(path):
        relative = path.relative_to(source)
        if path.is_symlink() or not path.resolve().is_relative_to(source):
            raise ValueError('Unsafe source symlink: ' + str(relative))
        if relative.as_posix().lower() == 'scripts/startup.scr':
            raise ValueError('Original game data found; provide a clean source checkout')
        if path.is_file() and path.suffix not in ('.pyc', '.pyo'):
            selected[relative.as_posix()] = path

    if (source / 'Data').exists() or (source / 'DATA').exists():
        raise ValueError('Merged Data tree found; provide a clean source checkout')
    for name in package.ROOT_FILES:
        if (source / name).exists() or (source / name).is_symlink():
            add(source / name)
    for name in package.ROOT_DIRS:
        root = source / name
        if root.is_symlink():
            raise ValueError('Source directory symlink: ' + name)
        if not root.exists():
            continue
        for current, dirs, files in os.walk(root, followlinks=False):
            for directory in dirs:
                if (Path(current) / directory).is_symlink():
                    raise ValueError('Source directory contains a symlink')
                if directory in ('node_modules', '.venv', 'venv', 'CMakeFiles'):
                    raise ValueError('Generated dependency/build tree inside source: ' + directory)
            dirs[:] = [d for d in dirs if d not in ('__pycache__', '.git')]
            for filename in files:
                add(Path(current) / filename)
    return dict(sorted(selected.items()))


def add_file(entries, name, path, executable=False):
    _safe_member(name)
    entry = {'path': path, 'data': None, 'sha256': digest(path), 'executable': executable}
    if name in entries and entries[name]['sha256'] != entry['sha256']:
        raise ValueError('Archive path collision: ' + name)
    entries[name] = entry


def add_bytes(entries, name, data, executable=False):
    _safe_member(name)
    entry = {'path': None, 'data': data, 'sha256': hashlib.sha256(data).hexdigest(), 'executable': executable}
    if name in entries and entries[name]['sha256'] != entry['sha256']:
        raise ValueError('Archive path collision: ' + name)
    entries[name] = entry


def _safe_member(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name or not path.parts:
        raise ValueError('Unsafe archive member: ' + name)


def write_archive(entries, output, epoch):
    if not 0 <= epoch <= 0xffffffff:
        raise ValueError('epoch must fit the gzip timestamp field')
    if output.exists():
        raise FileExistsError(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.openneoua-runtime-', dir=output.parent) as temporary:
        partial = Path(temporary) / 'archive.tar.gz'
        with partial.open('xb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=epoch) as zipped:
            with tarfile.open(fileobj=zipped, mode='w|', format=tarfile.PAX_FORMAT) as archive:
                for name, entry in sorted(entries.items()):
                    _safe_member(name)
                    item = tarfile.TarInfo(PREFIX + '/' + name)
                    item.mtime, item.uid, item.gid = epoch, 0, 0
                    item.uname = item.gname = ''
                    item.mode = 0o755 if entry['executable'] else 0o644
                    path = entry['path']
                    if path is not None:
                        if digest(path) != entry['sha256']:
                            raise ValueError('Input changed during packaging: ' + str(path))
                        item.size = path.stat().st_size
                        with path.open('rb') as stream:
                            archive.addfile(item, stream)
                    else:
                        item.size = len(entry['data'])
                        archive.addfile(item, io.BytesIO(entry['data']))
        # Same-filesystem link publishes the completed file without overwriting a concurrent output.
        os.link(partial, output)


def launcher(software=False):
    text = '''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
'''
    if software:
        text += '''export LIBGL_DRIVERS_PATH="$root/lib/dri"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-true}"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LP_NUM_THREADS="${LP_NUM_THREADS:-2}"
'''
    return (text + 'cd -- "$root"\nexec ./OpenNeoUA "$@"\n').encode()


def ownership_candidates(path):
    text = path.as_posix()
    for prefix, alias in (('/usr/lib/', '/lib/'), ('/usr/lib64/', '/lib64/'),
                          ('/lib/', '/usr/lib/'), ('/lib64/', '/usr/lib64/')):
        if text.startswith(prefix):
            return [path, Path(alias + text[len(prefix):])]
    return [path]


def package_provenance(path):
    package, ownership_path = None, None
    for candidate in ownership_candidates(path):
        for line in command(['dpkg-query', '-S', str(candidate)], optional=True).splitlines():
            owner, separator, owned_path = line.rpartition(': ')
            if separator and owned_path == str(candidate) and re.fullmatch(r'[a-z0-9][a-z0-9+.-]*(?::[a-z0-9_-]+)?', owner):
                package, ownership_path = owner, str(candidate)
                break
        if package:
            break
    if not package:
        return {'package': None, 'version': None, 'copyright': None, 'ownership_path': None}
    version = command(['dpkg-query', '-W', '-f=${Version}', package], optional=True).strip()
    copyright_path = Path('/usr/share/doc') / package.split(':')[0] / 'copyright'
    if copyright_path.is_file() and copyright_path.resolve().is_relative_to(Path('/usr/share/doc')):
        notice = str(copyright_path.resolve())
    else:
        notice = None
    return {'package': package, 'version': version or None, 'copyright': notice, 'ownership_path': ownership_path}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--epoch', required=True, type=int)
    parser.add_argument('--library-root', action='append', default=[], type=Path,
                        help='Additional trusted library root; standard /lib and /usr/lib are always allowed')
    parser.add_argument('--mesa-swrast', type=Path, help='Optional explicit swrast_dri.so path')
    parser.add_argument('--mesa-glx', type=Path, help='Optional GLX vendor override; defaults beside dri/')
    args = parser.parse_args()
    if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'amd64'):
        parser.error('Build this runtime archive on its Linux x86_64/glibc host')
    source, binary, output = validate_locations(args.source, args.binary, args.output)
    for suffix in ('.json', '.sha256'):
        if Path(str(output) + suffix).exists():
            raise FileExistsError(str(output) + suffix)
    roots = [Path('/lib'), Path('/lib64'), Path('/usr/lib'), *args.library_root]
    closure = collect_dependencies(binary, roots, args.mesa_swrast, args.mesa_glx)
    entries = {}
    add_file(entries, 'OpenNeoUA', binary, executable=True)
    add_bytes(entries, 'run-openneoua.sh', launcher(bool(args.mesa_swrast)), executable=True)
    for name, path in select_source(source).items():
        add_file(entries, 'Data/' + name, path, executable=path.suffix == '.sh')
    for record in closure['libraries']:
        path = Path(record['source'])
        add_file(entries, record['destination'], path)
        provenance = record['provenance']
        if provenance['copyright']:
            add_file(entries, 'licenses/' + provenance['package'].replace(':', '_') + '/copyright',
                     Path(provenance['copyright']))
    info = {'format': 1, 'source_date_epoch': args.epoch, 'architecture': platform.machine(),
            'os_release': platform.freedesktop_os_release(),
            'source_revision': os.environ.get('GITHUB_SHA', os.environ.get('SOURCE_REVISION', 'uncommitted-candidate')),
            'binary_sha256': digest(binary), 'software_gl': bool(args.mesa_swrast), **closure,
            'files': {name: entry['sha256'] for name, entry in sorted(entries.items())},
            'retail_data_included': False,
            'compatibility': 'Host glibc/loader, kernel and X server remain required. DT_NEEDED closure plus explicit Mesa modules is not universal plugin discovery or gameplay validation.'}
    add_bytes(entries, 'RUNTIME-INFO.json', (json.dumps(info, indent=2, sort_keys=True) + '\n').encode())
    write_archive(entries, output, args.epoch)
    receipt = {'archive': output.name, 'bytes': output.stat().st_size, 'sha256': digest(output),
               'binary_sha256': info['binary_sha256'], 'source_date_epoch': args.epoch,
               'files': len(entries), 'bundled_libraries': len(closure['libraries']),
               'host_libraries_excluded': len(closure['host_libraries'])}
    with Path(str(output) + '.json').open('x') as stream:
        stream.write(json.dumps(receipt, indent=2) + '\n')
    with Path(str(output) + '.sha256').open('x') as stream:
        stream.write(receipt['sha256'] + '  ' + output.name + '\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
