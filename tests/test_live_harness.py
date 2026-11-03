"""Linux multi-instance harness (tools/linux/live): argument building, runtime
assembly rules and the adapter plumbing. No engine, display or retail data."""
import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
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

    def test_generated_levels_join_the_runtime_level_directory(self):
        with tempfile.TemporaryDirectory(prefix='live-levels-') as d:
            root = Path(d)
            rt = root / 'rt-H1'
            multi = rt / 'Data/Levels/MULTI'          # retail casing, as assembled
            multi.mkdir(parents=True)
            (multi / 'l132132.ldf').write_text('retail 132')
            (multi / 'L201201.LDF').write_text('older 201')
            maps = root / 'maps'                       # as make_six_faction_map.py writes it
            (maps / 'Levels/Multi').mkdir(parents=True)
            (maps / 'Levels/Multi/l200200.ldf').write_text('six-start 200')
            (maps / 'Levels/Multi/l201201.ldf').write_text('six-start 201')
            (maps / 'receipts').mkdir()
            (maps / 'receipts/level-200.json').write_text('{}')
            added = drive.add_level_files(maps, rt)
            self.assertEqual(sorted(p.name for p in added), ['l200200.ldf', 'l201201.ldf'])
            # One level directory (the engine takes the first case-insensitive
            # match), the other-case 201 replaced, receipts not copied.
            self.assertEqual([p.name for p in (rt / 'Data/Levels').iterdir()], ['MULTI'])
            self.assertEqual(sorted(p.name for p in multi.iterdir()), ['l132132.ldf', 'l200200.ldf', 'l201201.ldf'])
            self.assertEqual((multi / 'l201201.ldf').read_text(), 'six-start 201')
            self.assertFalse(any('receipt' in p.name for p in rt.rglob('*')))
            self.assertEqual(drive.add_level_files(maps, rt), [])
            self.assertEqual(drive.add_level_files(root / 'no-maps', rt), [])

    def test_shell_scripts_parse(self):
        for script in ('launch_instance.sh', 'ns_holder.sh', 'collect_logs.sh', 'sample.sh'):
            with self.subTest(script=script):
                subprocess.run(['bash', '-n', str(LIVE / script)], check=True, timeout=10)


class Launcher(unittest.TestCase):
    """launch_instance.sh with a fake Xvfb on PATH and a fake engine (no display, no game).
    A fresh Xvfb costs the launcher's 5 s socket wait, since the fake creates none."""

    def test_a_relaunch_into_a_kept_match_keeps_its_transport(self):
        with tempfile.TemporaryDirectory(prefix='live-launch-') as d:
            root = Path(d)
            bin_dir, rt, lab = root / 'bin', root / 'rt-C1', root / 'lab'
            bin_dir.mkdir()
            (rt / 'Data/Env').mkdir(parents=True)
            (bin_dir / 'Xvfb').write_text('#!/usr/bin/env bash\nexec sleep 120\n')
            (bin_dir / 'Xvfb').chmod(0o755)
            (rt / 'OpenNeoUA').write_text('#!/usr/bin/env bash\necho "${OPENNEOUA_TRANSPORT:-unset}" >>seen.txt\n')
            (rt / 'OpenNeoUA').chmod(0o755)
            env = dict(os.environ, PATH=f'{bin_dir}{os.pathsep}{os.environ["PATH"]}', NICE='0')
            env.pop('OPENNEOUA_TRANSPORT', None)
            seen = rt / 'seen.txt'

            def launch(transport=None):
                count = len(seen.read_text().split()) if seen.exists() else 0
                run_env = dict(env, OPENNEOUA_TRANSPORT=transport) if transport else env
                subprocess.run(['bash', str(LIVE / 'launch_instance.sh'), 'C1', '977', str(rt), str(lab),
                                '-net-join', '127.0.0.1'], env=run_env, check=True, capture_output=True, timeout=60)
                deadline = time.monotonic() + 10
                while (len(seen.read_text().split()) if seen.exists() else 0) == count:
                    self.assertLess(time.monotonic(), deadline, 'the fake engine did not run')
                    time.sleep(0.05)
                return (lab / 'pids/C1.transport').read_text().strip()

            def stop_match():
                # As drive.py stop: Xvfb terminated, pid files removed (the fake
                # engine has exited already, so its pid is not signalled).
                xvfb = lab / 'pids/C1.xvfb.pid'
                if xvfb.exists():
                    try:
                        os.kill(int(xvfb.read_text()), signal.SIGTERM)
                    except (OSError, ValueError):
                        pass
                for pid_file in (xvfb, lab / 'pids/C1.pid'):
                    pid_file.unlink(missing_ok=True)

            try:
                self.assertEqual(launch('enet'), 'enet')    # the match starts on ENet
                self.assertEqual(launch(), 'enet')          # crash drill: relaunch without the variable
                self.assertEqual(launch('zndnet'), 'zndnet')  # an explicit choice wins
                stop_match()
                self.assertEqual(launch(), 'zndnet')        # a new start: no stale transport
            finally:
                stop_match()
            self.assertEqual(seen.read_text().split(), ['enet', 'enet', 'zndnet', 'unset'])


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

    def test_maps_generated_after_the_runtimes_reach_them(self):
        # A lab first used with a retail level, then with a generated one (or
        # with 200, then 201): rt-base and the instance copies exist already.
        (self.retail / 'LOCALE').mkdir()
        (self.retail / 'LOCALE/ENGLISH.DLL').write_text('dll')
        (self.retail / 'DATA/FONTS').mkdir()
        (self.retail / 'DATA/FONTS/x.fnt').write_text('font')
        lab = Path(self.temp.name) / 'lab'
        specs = [drive.Spec('host', 'H1'), drive.Spec('client', 'C1')]
        args = argparse.Namespace(retail=str(self.retail), binary=str(self.binary), level=132, fresh=False)
        saved, drive.REPO = drive.REPO, self.repo      # the small overlay, not this checkout
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                drive.prepare(args, lab, specs)
                generated = lab / 'maps/Levels/Multi/l200200.ldf'   # what the generator writes
                generated.parent.mkdir(parents=True)
                generated.write_text('six-start 200')
                args.level = 200
                runtimes = drive.prepare(args, lab, specs)
        finally:
            drive.REPO = saved
        for rt in (lab / 'rt-base', runtimes['H1'], runtimes['C1']):
            with self.subTest(runtime=rt.name):
                found = [p for p in (rt / 'Data').rglob('*') if p.name.lower() == 'l200200.ldf']
                self.assertEqual([p.relative_to(rt).as_posix() for p in found], ['Data/Levels/Multi/l200200.ldf'])
                self.assertEqual(found[0].read_text(), 'six-start 200')

    def test_movies_on_request_and_no_overwrite(self):
        self.assertEqual(self.assemble('--keep-movies').returncode, 0)
        self.assertTrue(any(p.name.lower() == 'intro.mpg' for p in (self.out / 'Data').rglob('*')))
        again = self.assemble()
        self.assertNotEqual(again.returncode, 0)
        self.assertIn('output exists', again.stderr)


if __name__ == '__main__':
    unittest.main()
