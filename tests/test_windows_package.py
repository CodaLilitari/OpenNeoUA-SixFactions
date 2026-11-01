import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('windows_package', Path(__file__).resolve().parents[1] / 'tools/windows/package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class WindowsPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='openneoua-pe-test-')
        self.root = Path(self.temp.name)
        self.dlls, self.system = self.root / 'dlls', self.root / 'system'
        self.dlls.mkdir()
        self.system.mkdir()
        self.binary = self.root / 'OpenNeoUA.exe'
        self.binary.touch()

    def tearDown(self):
        self.temp.cleanup()

    def test_transitive_cycle_is_closed_and_system_is_excluded(self):
        for name in ('first.dll', 'second.dll'):
            (self.dlls / name).touch()
        (self.system / 'KERNEL32.dll').touch()
        imports = {'OpenNeoUA.exe': ' DLL Name: first.dll\n DLL Name: KERNEL32.dll\n',
                   'first.dll': ' DLL Name: second.dll\n',
                   'second.dll': ' DLL Name: first.dll\n DLL Name: api-ms-win-test.dll\n'}
        files, graph = package.closure(self.binary, self.dlls, self.system, lambda p: imports[p.name])
        self.assertEqual(set(files), {'openneoua.exe', 'first.dll', 'second.dll'})
        self.assertEqual(graph['OpenNeoUA.exe'][1]['kind'], 'Windows system dependency')

    def test_unresolved_dependency_fails_before_packaging(self):
        with self.assertRaisesRegex(ValueError, 'unresolved import missing.dll'):
            package.closure(self.binary, self.dlls, self.system, lambda p: 'DLL Name: missing.dll\n')

    def test_import_names_cannot_escape_directory(self):
        for name in ('../outside.dll', 'C:\\outside.dll', 'folder/outside.dll'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                package.imported_names(' DLL Name: ' + name + '\n')

    def test_unrelated_dll_is_not_bundled(self):
        (self.dlls / 'unused.dll').touch()
        files, graph = package.closure(self.binary, self.dlls, self.system, lambda p: '')
        self.assertEqual(list(files), ['openneoua.exe'])

    def test_duplicate_imports_are_collapsed(self):
        self.assertEqual(package.imported_names(' DLL Name: same.dll\n DLL Name: same.dll\n'), ['same.dll'])


if __name__ == '__main__':
    unittest.main()
