#!/usr/bin/env python3
"""Package clean source and a MinGW PE binary with its recursive DLL closure."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('linux_package', ROOT / 'tools/linux/package.py')
source_package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source_package)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def imported_names(output):
    names = re.findall(r'^\s*DLL Name:\s*(\S+)\s*$', output, re.M)
    for name in names:
        if not re.fullmatch(r'[A-Za-z0-9_.+-]+\.dll', name, re.I):
            raise ValueError('Invalid imported DLL name: ' + name)
    return sorted(set(names), key=str.lower)


def closure(binary, dll_dir, system_dir, inspect):
    available = {}
    for path in dll_dir.iterdir():
        if path.is_file() and path.suffix.lower() == '.dll':
            key = path.name.lower()
            if key in available:
                raise ValueError('Case-colliding DLL files: ' + path.name)
            if path.is_symlink():
                raise ValueError('DLL symlink is not supported: ' + path.name)
            available[key] = path
    queue, found, graph = [binary], {}, {}
    while queue:
        path = queue.pop()
        if path.name.lower() in found:
            continue
        found[path.name.lower()] = path
        dependencies = []
        for name in imported_names(inspect(path)):
            dependency = available.get(name.lower())
            if dependency is not None:
                queue.append(dependency)
                kind = 'bundled'
            elif name.lower().startswith(('api-ms-', 'ext-ms-')) or (system_dir / name).is_file():
                kind = 'Windows system dependency'
            else:
                raise ValueError(path.name + ': unresolved import ' + name)
            dependencies.append({'name': name, 'kind': kind})
        graph[path.name] = dependencies
    return found, graph


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, required=True, help='Official MSYS2 mingw64 prefix')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, binary, prefix, output = (p.resolve() for p in (args.source, args.binary, args.prefix, args.output))
    if output.is_relative_to(source) or binary.is_relative_to(source) or output.exists():
        parser.error('Use a new external output and an external build binary')
    if os.name != 'nt':
        parser.error('Package on Windows so system DLL resolution is checked against the target OS')
    with binary.open('rb') as stream:
        if stream.read(2) != b'MZ':
            parser.error('Expected a Windows PE executable')
    objdump = prefix / 'bin/objdump.exe'
    def inspect(path):
        return subprocess.check_output([str(objdump), '-p', str(path)], text=True, timeout=30)
    files, graph = closure(binary, prefix / 'bin', Path(os.environ['SystemRoot']) / 'System32', inspect)
    selected = {'OpenNeoUA-windows/' + p.name: p for p in files.values()}
    for name in source_package.ROOT_FILES:
        if (source / name).is_file():
            selected['OpenNeoUA-windows/Data/' + name] = source / name
    for name in source_package.ROOT_DIRS:
        root = source / name
        if not root.exists():
            continue
        for path in root.rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts and path.suffix not in ('.pyc', '.pyo'):
                if path.is_symlink() or not path.resolve().is_relative_to(source):
                    parser.error('Source symlink/escape: ' + str(path))
                relative = path.relative_to(source).as_posix()
                if relative.lower() == 'scripts/startup.scr':
                    parser.error('Retail data is not permitted in this source/binary package')
                selected['OpenNeoUA-windows/Data/' + relative] = path
    # Preserve the official toolchain's license corpus rather than guessing
    # license ownership from DLL names. It contains no runtime/account data.
    licenses = prefix / 'share/licenses'
    if not licenses.is_dir():
        parser.error('Official toolchain license corpus is missing')
    for path in licenses.rglob('*'):
        if path.is_file():
            if path.is_symlink():
                parser.error('License symlink is not supported')
            selected['OpenNeoUA-windows/ThirdPartyLicenses/' + path.relative_to(licenses).as_posix()] = path
    names = [name.lower() for name in selected]
    if len(names) != len(set(names)):
        parser.error('Package contains case-colliding paths')
    hashes = {name: digest(path) for name, path in sorted(selected.items())}
    info = {'format': 1, 'platform': 'windows-x86_64',
            'source_revision': os.environ.get('GITHUB_SHA', os.environ.get('SOURCE_REVISION', 'uncommitted-candidate')),
            'binary_sha256': digest(binary), 'files': hashes, 'dll_import_graph': graph,
            'retail_data_included': False, 'runtime_tested_by_packager': False,
            'license_scope': 'Official MSYS2 prefix license corpus'}
    output.parent.mkdir(parents=True, exist_ok=True)
    def add(archive, name, data):
        entry = zipfile.ZipInfo(name, (2000, 1, 1, 0, 0, 0))
        entry.compress_type = zipfile.ZIP_DEFLATED
        entry.external_attr = 0o100644 << 16
        archive.writestr(entry, data)
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, path in sorted(selected.items()):
            add(archive, name, path.read_bytes())
        add(archive, 'OpenNeoUA-windows/BUILD-INFO.json', json.dumps(info, indent=2).encode())
        add(archive, 'OpenNeoUA-windows/run-openneoua.cmd', b'@echo off\r\ncd /d "%~dp0"\r\nOpenNeoUA.exe %*\r\n')
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError('Package CRC verification failed')
        for name, sha in hashes.items():
            if hashlib.sha256(archive.read(name)).hexdigest() != sha:
                raise ValueError('Packaged payload changed: ' + name)
    receipt = {'archive': output.name, 'bytes': output.stat().st_size, 'sha256': digest(output),
               'binary_sha256': info['binary_sha256'], 'binary_and_dll_count': len(files), 'files': len(selected) + 2}
    output.with_suffix('.zip.json').write_text(json.dumps(receipt, indent=2) + '\n')
    output.with_suffix('.zip.sha256').write_text(receipt['sha256'] + '  ' + output.name + '\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
