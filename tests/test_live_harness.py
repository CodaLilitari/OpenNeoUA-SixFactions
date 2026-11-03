"""Linux multi-instance harness (tools/linux/live): argument building, runtime
assembly rules and the adapter plumbing. No engine, display or retail data."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
LIVE = Path(__file__).resolve().parents[1] / 'tools/linux/live'


def load(name):
    spec = importlib.util.spec_from_file_location('live_' + name, LIVE / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


drive = load('drive')


class EngineArguments(unittest.TestCase):
    def test_host_client_and_bot_use_the_lobby_flags(self):
        host = drive.Spec('host', 'HOST1:resistance')
        self.assertEqual(host.engine_args(132, 3), ['-net-host', '132', '-net-name', 'HOST1',
                                                    '-net-start-when', '3', '-net-faction', '1'])
        client = drive.Spec('client', 'C1:Black-Sect')
        self.assertEqual(client.engine_args(132, 3), ['-net-join', '127.0.0.1', '-net-name', 'C1',
                                                      '-net-ready', '-net-faction', '5'])
        plain = drive.Spec('client', 'C2')
        self.assertEqual(plain.engine_args(132, 3), ['-net-join', '127.0.0.1', '-net-name', 'C2', '-net-ready'])
        # The bot's -bot faction is also its lobby faction; the spec uses the
        # names ParseBotSpec knows.
        bot = drive.Spec('bot', 'BOT1:mykonier:Veteran')
        self.assertEqual(bot.engine_args(132, 3), ['-bot', 'mykonian:veteran', '-net-join', '127.0.0.1',
                                                   '-net-name', 'BOT1', '-net-ready'])
        self.assertEqual(drive.Spec('bot', 'BOT2:6').engine_args(1, 2)[:2], ['-bot', 'ghorkov:normal'])

    def test_bad_specs_are_refused_before_anything_starts(self):
        for role, text in (('host', 'BOT Ghorkov'), ('client', 'x' * 21), ('client', 'a*b'),
                           ('client', 'C1:elves'), ('bot', 'BOT1'), ('bot', 'BOT1:ghorkov:hard'),
                           ('client', 'C1:ghorkov:veteran'), ('host', '')):
            with self.subTest(role=role, text=text), self.assertRaises(SystemExit):
                drive.Spec(role, text)
        self.assertTrue(drive.CALLSIGN.fullmatch('x' * 20))
        self.assertTrue(drive.CALLSIGN.fullmatch('Ghor.1-[A]'))

    def test_factions_are_owner_ids(self):
        self.assertEqual([drive.faction_owner(n) for n in
                          ('Resistance', 'sulgogars', 'Mykonian', 'TAERKASTEN', 'black sect', 'ghorkov', '4')],
                         [1, 2, 3, 4, 5, 6, 4])

    def test_status_line_summarises_the_net_state(self):
        line = drive.describe('C1', {'screen': 'menu', 'net': {
            'role': 'client', 'transport': 'enet', 'level_id': 132, 'local_owner': 0, 'started': False,
            'auto': {'step': 'wait_start'},
            'lobby': [{'name': 'HOST1', 'faction': 1, 'ready': True}, {'name': 'C1', 'faction': 4, 'ready': False}]}})
        for part in ('C1', 'step=wait_start', 'role=client', 'enet', 'level=132', 'lobby=[HOST1/1+ C1/4-]'):
            self.assertIn(part, line)

    def test_retail_check_ignores_directory_case(self):
        with tempfile.TemporaryDirectory(prefix='live-retail-') as d:
            root = Path(d)
            for part in ('Data/Set46', 'Data/fonts', 'Data/Scripts', 'Levels/Multi', 'Locale'):
                (root / part).mkdir(parents=True)
            with self.assertRaisesRegex(SystemExit, 'LOCALE'):
                drive.check_retail(root)
            (root / 'Locale/English.dll').write_text('x')
            drive.check_retail(root)
            self.assertEqual(drive.ci(root, 'DATA', 'SET46'), root / 'Data/Set46')
            self.assertIsNone(drive.ci(root, 'DATA', 'MC2RES'))

    def test_adapter_plumbing_against_a_fake_engine(self):
        self.assertEqual(drive.selftest(), 0)

    def test_shell_scripts_parse(self):
        for script in ('launch_instance.sh', 'ns_holder.sh', 'collect_logs.sh', 'sample.sh'):
            with self.subTest(script=script):
                subprocess.run(['bash', '-n', str(LIVE / script)], check=True, timeout=10)


class RuntimeAssembly(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='live-harness-test-')
        root = Path(self.temp.name)
        self.retail, self.repo, self.out = root / 'retail', root / 'repo', root / 'out'
        files = {
            self.retail / 'DATA/SCRIPTS/STARTUP.SCR': 'retail script',
            self.retail / 'DATA/MOV/intro.mpg': 'movie',
            self.retail / 'DATA/SET46/x.ilb': 'tiles',
            self.retail / 'LEVELS/MULTI/l132132.ldf': 'level',
            self.retail / 'NUCLEUS.INI': 'gfx.movie_player = yes\nmenu.windowed = off\n',
            self.repo / 'Scripts/net_bot.scr': 'repo script',
            self.repo / 'Levels/Multi/keep.txt': 'repo level dir',
            self.repo / 'OpenNeoUA.ini': 'game.new.debug = yes\n',
        }
        for path, text in files.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        self.binary = root / 'OpenNeoUA'
        self.binary.write_bytes(b'\x7fELF fake')

    def tearDown(self):
        self.temp.cleanup()

    def assemble(self, *extra):
        return subprocess.run([sys.executable, '-I', str(LIVE / 'assemble_runtime.py'), '--retail', str(self.retail),
                               '--repo', str(self.repo), '--binary', str(self.binary), '--out', str(self.out),
                               *extra], capture_output=True, text=True, timeout=60)

    def test_case_variants_merge_and_unattended_settings(self):
        result = self.assemble()
        self.assertEqual(result.returncode, 0, result.stderr)
        data = self.out / 'Data'
        # One directory per case-insensitive name (the engine's VFS takes the
        # first match), with the repo's casing.
        names = sorted(p.name for p in data.iterdir())
        self.assertIn('Scripts', names)
        self.assertNotIn('SCRIPTS', names)
        self.assertEqual(sorted(p.name for p in (data / 'Scripts').iterdir()), ['STARTUP.SCR', 'net_bot.scr'])
        self.assertTrue((data / 'Levels/Multi/l132132.ldf').is_file())
        # The intro movie stays out unless asked for.
        self.assertFalse(any(p.name.lower() == 'intro.mpg' for p in data.rglob('*')))
        self.assertIn('movies_left_out=1', result.stdout)
        nucleus = (data / 'NUCLEUS.INI').read_text()
        self.assertIn('gfx.movie_player = no', nucleus)
        self.assertIn('menu.windowed = on', nucleus)
        self.assertNotIn('= yes', nucleus)
        self.assertIn('game.new.debug = no', (data / 'OpenNeoUA.ini').read_text())
        self.assertFalse(any(p.is_symlink() for p in self.out.rglob('*')))
        self.assertTrue(os.access(self.out / 'run-openneoua.sh', os.X_OK))
        self.assertTrue(os.access(self.out / 'OpenNeoUA', os.X_OK))
        # Incomplete retail data is reported, not hidden.
        self.assertIn('WARNING', result.stdout)

    def test_movies_on_request_and_no_overwrite(self):
        self.assertEqual(self.assemble('--keep-movies').returncode, 0)
        self.assertTrue(any(p.name.lower() == 'intro.mpg' for p in (self.out / 'Data').rglob('*')))
        again = self.assemble()
        self.assertNotEqual(again.returncode, 0)
        self.assertIn('output exists', again.stderr)


if __name__ == '__main__':
    unittest.main()
