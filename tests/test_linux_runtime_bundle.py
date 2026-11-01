"""Runtime-bundle contracts; Linux-only cases use installed tools, never a build."""
import importlib.util
import inspect
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

MODULE_PATH = Path(__file__).resolve().parents[1] / 'tools/linux/bundle-runtime.py'
bundle = None
if MODULE_PATH.exists():
    spec = importlib.util.spec_from_file_location('linux_runtime_bundle', MODULE_PATH)
    bundle = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = bundle
    spec.loader.exec_module(bundle)


class RuntimeBundleTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(bundle, 'runtime bundler is not implemented')
        self.temp = tempfile.TemporaryDirectory(prefix='openneoua-bundle-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_ldd_resolves_transitive_paths_without_virtual_object(self):
        result = bundle.parse_ldd('''
 linux-vdso.so.1 (0x00007ffe)
 libSDL2-2.0.so.0 => /usr/lib/libSDL2-2.0.so.0 (0x00007fff)
 libfoo.so.2 => /opt/library space/libfoo.so.2 (0x00008fff)
 /lib64/ld-linux-x86-64.so.2 (0x00000001)
''')
        self.assertEqual(result, {
            'libSDL2-2.0.so.0': Path('/usr/lib/libSDL2-2.0.so.0'),
            'libfoo.so.2': Path('/opt/library space/libfoo.so.2'),
            'ld-linux-x86-64.so.2': Path('/lib64/ld-linux-x86-64.so.2')})

    def test_unresolved_or_unrecognized_ldd_output_fails_closed(self):
        for text in ['libSDL2.so => not found', 'not a dynamic executable',
                     'libbad.so => relative/libbad.so (0x1)', 'unexpected diagnostic']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                bundle.parse_ldd(text)

    def test_host_glibc_is_excluded_but_mozilla_nss_is_bundleable(self):
        for name in ['libc.so.6', 'libm.so.6', 'libpthread.so.0', 'libdl.so.2',
                     'libresolv.so.2', 'libnss_files.so.2', 'ld-linux-x86-64.so.2',
                     'libmvec.so.1', 'libthread_db.so.1']:
            self.assertTrue(bundle.is_host_runtime(name), name)
        for name in ['libnss3.so', 'libSDL2-2.0.so.0', 'libstdc++.so.6', 'libLLVM-15.so.1']:
            self.assertFalse(bundle.is_host_runtime(name), name)

    def test_glibc_package_ownership_excludes_unlisted_runtime_component(self):
        self.assertIn('package', inspect.signature(bundle.is_host_runtime).parameters)
        self.assertTrue(bundle.is_host_runtime('libfuture-glibc.so.1', package='libc6:amd64'))
        self.assertFalse(bundle.is_host_runtime('libnss3.so', package='libnss3:amd64'))

    def test_elf_metadata_rejects_bound_search_path_or_nonstandard_loader(self):
        info = bundle.parse_readelf('''
  [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]
 0x0000000000000001 (NEEDED) Shared library: [libSDL2-2.0.so.0]
 0x000000000000000f (RPATH) Library rpath: [/home/builder/private-libs]
 0x000000000000000e (SONAME) Library soname: [sample.so]
  Name: GLIBC_2.34  Flags: none
''')
        self.assertEqual(info['needed'], ['libSDL2-2.0.so.0'])
        self.assertEqual(info['glibc_versions'], ['GLIBC_2.34'])
        with self.assertRaises(ValueError):
            bundle.validate_metadata(info, executable=True)
        info['search_paths'] = []
        info['rpath'] = []
        info['interpreter'] = '/tmp/untrusted-loader'
        with self.assertRaises(ValueError):
            bundle.validate_metadata(info, executable=True)

    def test_runpath_can_follow_bundled_ld_library_path_while_rpath_cannot(self):
        for path in ['$ORIGIN/../lib', '/usr/lib/x86_64-linux-gnu/pulseaudio']:
            info = bundle.parse_readelf('0x1 (RUNPATH) Library runpath: [' + path + ']')
            bundle.validate_metadata(info)
            strict = bundle.parse_readelf('0x1 (RPATH) Library rpath: [' + path + ']')
            with self.assertRaises(ValueError):
                bundle.validate_metadata(strict)

    def test_external_locations_reject_output_in_git_or_source(self):
        source = self.root / 'source'
        source.mkdir()
        binary = self.root / 'build/OpenNeoUA'
        binary.parent.mkdir()
        binary.write_bytes(b'fixture')
        bundle.validate_locations(source, binary, self.root / 'output.tar.gz')
        with self.assertRaises(ValueError):
            bundle.validate_locations(source, binary, source / 'output.tar.gz')
        repo = self.root / 'other-repo'
        (repo / '.git').mkdir(parents=True)
        with self.assertRaises(ValueError):
            bundle.validate_locations(source, binary, repo / 'dist/output.tar.gz')

    def test_library_symlink_cannot_escape_allowed_root(self):
        allowed = self.root / 'allowed'
        allowed.mkdir()
        outside = self.root / 'outside.so'
        outside.write_bytes(b'outside')
        link = allowed / 'libescape.so'
        try:
            link.symlink_to(outside)
        except OSError as error:
            self.skipTest(f'symlinks unavailable: {error}')
        with self.assertRaises(ValueError):
            bundle.resolve_library(link, [allowed])
        real = allowed / 'real.so'
        real.write_bytes(b'inside')
        safe = allowed / 'libsafe.so'
        safe.symlink_to(real)
        self.assertEqual(bundle.resolve_library(safe, [allowed]), real.resolve())

    def test_same_archive_name_with_different_library_bytes_is_rejected(self):
        one, two = self.root / 'one', self.root / 'two'
        one.write_bytes(b'first')
        two.write_bytes(b'second')
        entries = {}
        bundle.add_file(entries, 'lib/libsame.so', one)
        with self.assertRaises(ValueError):
            bundle.add_file(entries, 'lib/libsame.so', two)

    def test_merged_usr_ownership_queries_include_only_equivalent_system_alias(self):
        self.assertTrue(hasattr(bundle, 'ownership_candidates'), 'merged-/usr ownership lookup is missing')
        self.assertEqual(bundle.ownership_candidates(Path('/usr/lib/x86_64-linux-gnu/libz.so.1')),
                         [Path('/usr/lib/x86_64-linux-gnu/libz.so.1'), Path('/lib/x86_64-linux-gnu/libz.so.1')])
        self.assertEqual(bundle.ownership_candidates(Path('/lib64/ld-linux-x86-64.so.2')),
                         [Path('/lib64/ld-linux-x86-64.so.2'), Path('/usr/lib64/ld-linux-x86-64.so.2')])
        self.assertEqual(bundle.ownership_candidates(Path('/opt/vendor/libcustom.so')),
                         [Path('/opt/vendor/libcustom.so')])

    def test_source_rejects_retail_startup_script(self):
        source = self.root / 'source'
        (source / 'Scripts').mkdir(parents=True)
        (source / 'README.md').write_text('source')
        (source / 'Scripts/STARTUP.SCR').write_text('retail')
        with self.assertRaises(ValueError):
            bundle.select_source(source)

    def test_source_rejects_generated_dependency_tree_before_descending(self):
        source = self.root / 'source'
        (source / 'tools/node_modules').mkdir(parents=True)
        (source / 'tools/node_modules/generated.js').write_text('generated')
        with self.assertRaises(ValueError):
            bundle.select_source(source)

    def test_wrong_architecture_is_rejected_before_external_inspection(self):
        binary = self.root / 'foreign-elf'
        binary.write_bytes(b'\x7fELF\x02\x01' + bytes(12) + b'\xb7\x00')
        with self.assertRaises(ValueError):
            bundle.inspect_elf(binary, executable=True)

    def test_source_directory_symlink_is_rejected(self):
        source = self.root / 'source'
        source.mkdir()
        outside = self.root / 'outside'
        outside.mkdir()
        try:
            (source / 'Scripts').symlink_to(outside, target_is_directory=True)
        except OSError as error:
            self.skipTest(f'symlinks unavailable: {error}')
        with self.assertRaises(ValueError):
            bundle.select_source(source)

    def test_archive_is_repeatable_and_carries_normalized_metadata(self):
        payload = self.root / 'payload'
        payload.write_bytes(b'known binary bytes')
        entries = {}
        bundle.add_file(entries, 'OpenNeoUA', payload, executable=True)
        bundle.add_bytes(entries, 'RUNTIME-INFO.json', b'{"fixture": true}\n')
        one, two = self.root / 'one.tar.gz', self.root / 'two.tar.gz'
        bundle.write_archive(entries, one, 1234567890)
        os.utime(payload, (1700000000, 1700000000))
        bundle.write_archive(entries, two, 1234567890)
        self.assertEqual(one.read_bytes(), two.read_bytes())
        with tarfile.open(one) as archive:
            members = archive.getmembers()
            self.assertEqual([x.name for x in members], [
                'OpenNeoUA-linux-runtime/OpenNeoUA', 'OpenNeoUA-linux-runtime/RUNTIME-INFO.json'])
            self.assertEqual(archive.extractfile(members[0]).read(), b'known binary bytes')
            self.assertTrue(all(x.uid == x.gid == 0 and x.mtime == 1234567890 for x in members))
            self.assertEqual(members[0].mode, 0o755)
            self.assertTrue(all(x.isfile() for x in members))
        with self.assertRaises(FileExistsError):
            bundle.write_archive(entries, one, 1234567890)

    @unittest.skipUnless(os.name == 'posix' and shutil.which('sh'), 'requires POSIX shell')
    def test_launcher_uses_its_own_directory_and_forwards_arguments(self):
        folder = self.root / 'runtime with spaces'
        folder.mkdir()
        (folder / 'run-openneoua.sh').write_bytes(bundle.launcher(software=True))
        executable = folder / 'OpenNeoUA'
        executable.write_text('#!/bin/sh\nprintf "%s\\n" "$PWD" "$LD_LIBRARY_PATH" "$LIBGL_DRIVERS_PATH" "$LP_NUM_THREADS" "$1"\n')
        executable.chmod(0o755)
        env = os.environ.copy()
        env.pop('LP_NUM_THREADS', None)
        env['LD_LIBRARY_PATH'] = '/existing/path'
        result = subprocess.run(['sh', str(folder / 'run-openneoua.sh'), 'argument with spaces'],
                                cwd=self.root, env=env, capture_output=True, text=True, timeout=10, check=True)
        self.assertEqual(result.stdout.splitlines(), [str(folder), str(folder / 'lib') + ':/existing/path',
            str(folder / 'lib/dri'), '2', 'argument with spaces'])

    @unittest.skipUnless(sys.platform == 'linux' and shutil.which('readelf') and shutil.which('ldd'),
                         'requires Linux ELF tools')
    def test_real_system_elf_leaves_host_runtime_unbundled(self):
        result = bundle.collect_dependencies(Path('/bin/true').resolve(),
            [Path('/usr/lib'), Path('/lib'), Path('/lib64')])
        self.assertTrue(any(x['name'] == 'libc.so.6' for x in result['host_libraries']))
        self.assertFalse(any(bundle.is_host_runtime(x['name']) for x in result['libraries']))
        self.assertTrue(result['binary_metadata']['glibc_versions'])


if __name__ == '__main__':
    unittest.main()
