"""Protocol/file-transport checks. An emulated peer is not a gameplay test."""
import base64
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import zlib

sys.dont_write_bytecode = True
MODULE = Path(__file__).resolve().parents[1] / 'tools/agent/openneoua_mcp.py'
spec = importlib.util.spec_from_file_location('agent_mcp', MODULE)
mcp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mcp)
SESSION = '0123456789abcdef'


class AgentMCPTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='openneoua-agent-test-')
        self.directory = Path(self.temp.name)
        self.session()
        self.bridge = mcp.Bridge(self.directory, timeout=0.1)

    def tearDown(self):
        self.bridge.close()
        self.temp.cleanup()

    def session(self, value=SESSION, operations=None):
        if operations is None:
            operations = ['observe', 'status', 'key', 'move', 'click', 'text', 'release',
                          'fork_create', 'fork_step', 'fork_observe', 'fork_commit', 'fork_discard', 'fork_clone']
        (self.directory / 'session.json').write_text(json.dumps({'protocol': 1, 'session': value,
            'observation_schema': 2, 'operations': operations}))

    def response(self, ident, **extras):
        data = {'protocol': 1, 'session': SESSION, 'id': ident, 'ok': True, **extras}
        temp = self.directory / 'test-response.tmp'
        temp.write_text(json.dumps(data))
        temp.replace(self.directory / 'response.json')

    def peer(self, fn):
        def run():
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                request = self.directory / 'request.txt'
                if request.exists():
                    fn(request.read_text().split())
                    return
                time.sleep(0.005)
            raise AssertionError('Adapter wrote no request')
        thread = threading.Thread(target=run)
        thread.start()
        return thread

    def test_single_adapter_owns_directory(self):
        with self.assertRaisesRegex(mcp.AgentError, 'Another adapter'):
            mcp.Bridge(self.directory)
        self.bridge.close()
        other = mcp.Bridge(self.directory)
        other.close()

    def test_rejects_session_inside_git_checkout(self):
        (self.directory / '.git').mkdir()
        with self.assertRaisesRegex(mcp.AgentError, 'outside a Git'):
            mcp.Bridge(self.directory)

    def test_wrong_protocol_rejected(self):
        (self.directory / 'session.json').write_text('{"protocol":99,"session":"0123456789abcdef"}')
        with self.assertRaisesRegex(mcp.AgentError, 'Unsupported'):
            self.bridge.capabilities()

    def test_completed_request_id_is_not_reused(self):
        self.response(17)
        thread = self.peer(lambda r: self.response(int(r[2]), operation=r[4]))
        result = self.bridge.command('release')
        thread.join()
        self.assertEqual((result['id'], result['operation']), (18, 'release'))

    def test_pending_previous_process_request_is_not_replayed(self):
        (self.directory / 'request.txt').write_text(f'1 {SESSION} 7 9999999999999 key 4 50\n')
        with self.assertRaisesRegex(mcp.AgentError, 'Previous command'):
            self.bridge.command('observe')
        self.assertEqual((self.directory / 'request.txt').read_text(), f'1 {SESSION} 7 9999999999999 key 4 50\n')

    def test_timeout_does_not_repeat_mutation(self):
        with self.assertRaisesRegex(mcp.AgentError, 'not retried'):
            self.bridge.command('key', (4, 100))
        before = (self.directory / 'request.txt').read_bytes()
        with self.assertRaisesRegex(mcp.AgentError, 'Previous command'):
            self.bridge.command('key', (5, 100))
        self.assertEqual((self.directory / 'request.txt').read_bytes(), before)

    def test_wrong_session_response_does_not_acknowledge(self):
        thread = self.peer(lambda r: self.response(int(r[2]), session='deadbeef'))
        with self.assertRaisesRegex(mcp.AgentError, 'not retried'):
            self.bridge.command('observe')
        thread.join()

    def test_restart_is_not_replayed_automatically(self):
        thread = self.peer(lambda r: self.session('1234567890abcdef'))
        with self.assertRaisesRegex(mcp.AgentError, 'restarted'):
            self.bridge.command('key', (4, 50))
        thread.join()
        self.assertIn(SESSION, (self.directory / 'request.txt').read_text())

    def test_engine_error_is_visible(self):
        thread = self.peer(lambda r: self.response(int(r[2]), ok=False, error='outside current framebuffer'))
        with self.assertRaisesRegex(mcp.AgentError, 'outside current framebuffer'):
            self.bridge.command('click', (1, 3000, 3000, 50))
        thread.join()

    def test_argument_boundaries_precede_transport(self):
        cases = [
            ('game_key_press', {'key': 'a', 'duration_ms': 2001}),
            ('game_key_press', {'key': 'a', 'duration_ms': True}),
            ('game_mouse_move', {'x': -1, 'y': 0}),
            ('game_mouse_click', {'x': 1, 'y': 1, 'button': 'extra'}),
            ('game_type_text', {'text': 'hello\nquit'}),
            ('game_type_text', {'text': 'a' * 129}),
            ('game_type_text', {'text': '\u263a'}),
            ('game_observe', {'path': '../../private'}),
            ('game_fork_create', {'owner': 256}),
            ('game_fork_create', {'owner': True}),
            ('game_fork_step', {'fork': 1, 'action': 5, 'ticks': 1}),
            ('game_fork_step', {'fork': 1, 'action': 1, 'ticks': 0}),
            ('game_fork_observe', {'fork': 0, 'owner': 1}),
            ('game_fork_commit', {'fork': 1}),
        ('game_fork_clone', {'fork': 1, 'owner': 256}),
        ]
        for name, args in cases:
            with self.subTest(name=name, args=args), self.assertRaises(mcp.AgentError):
                mcp.invoke(self.bridge, name, args)
        self.assertFalse((self.directory / 'request.txt').exists())

    def test_observe_returns_the_native_image(self):
        pixels = bytes((255, 0, 0, 0, 255, 0))
        (self.directory / 'frame.ppm').write_bytes(b'P6\n2 1\n255\n' + pixels)
        thread = self.peer(lambda r: self.response(int(r[2]), screenshot='frame.ppm', width=2, height=1, owner=5))
        result = mcp.invoke(self.bridge, 'game_observe', {})
        thread.join()
        self.assertEqual(result['content'][1]['mimeType'], 'image/png')
        png = base64.b64decode(result['content'][1]['data'])
        start = png.index(b'IDAT')
        size = int.from_bytes(png[start - 4:start], 'big')
        self.assertEqual(zlib.decompress(png[start + 4:start + 4 + size]), b'\0' + pixels)

    def test_status_returns_net_state_without_an_image(self):
        net = {'lobby_screen': 'session', 'role': 'client', 'transport': 'enet', 'name': 'C1',
               'level_id': 200, 'started': False, 'player_index': 1,
               'lobby': [{'slot': 0, 'name': 'HOST1', 'faction': 1, 'ready': True, 'me': False},
                         {'slot': 1, 'name': 'C1', 'faction': 6, 'ready': False, 'me': True}],
               'seats': [], 'bots': []}
        seen = []
        def act(request):
            seen.extend(request)
            self.response(int(request[2]), screen='menu', net=net)
        thread = self.peer(act)
        result = mcp.invoke(self.bridge, 'game_status', {})
        thread.join()
        self.assertEqual(seen[4:], ['status'])
        self.assertEqual(len(result['content']), 1)
        self.assertEqual(json.loads(result['content'][0]['text'])['net'], net)
        self.assertFalse((self.directory / 'frame.ppm').exists())

    def test_status_requires_an_engine_that_advertises_it(self):
        self.session(operations=['observe', 'key', 'move', 'click', 'text', 'release'])
        with self.assertRaisesRegex(mcp.AgentError, 'does not advertise operation: status'):
            mcp.invoke(self.bridge, 'game_status', {})
        self.assertFalse((self.directory / 'request.txt').exists())

    def test_status_takes_no_arguments(self):
        with self.assertRaises(mcp.AgentError):
            mcp.invoke(self.bridge, 'game_status', {'path': 'frame.ppm'})
        self.assertFalse((self.directory / 'request.txt').exists())

    def test_observe_passes_the_net_state_through(self):
        (self.directory / 'frame.ppm').write_bytes(b'P6\n1 1\n255\n\0\0\0')
        net = {'lobby_screen': 'map', 'role': 'host', 'level_id': 0, 'lobby': [], 'seats': [],
               'bots': [{'faction': 6, 'difficulty': 'veteran', 'phase': 'lobby', 'owner': 0}]}
        thread = self.peer(lambda r: self.response(int(r[2]), screenshot='frame.ppm', net=net))
        result = mcp.invoke(self.bridge, 'game_observe', {})
        thread.join()
        self.assertEqual(json.loads(result['content'][0]['text'])['net'], net)
        self.assertEqual(result['content'][1]['mimeType'], 'image/png')

    def test_fork_create_writes_fork_request_and_returns_fork_json(self):
        seen = []
        def act(request):
            seen.extend(request)
            self.response(int(request[2]), fork={'fork': 1, 'hash': 42, 'time_ms': 0})
        thread = self.peer(act)
        result = mcp.invoke(self.bridge, 'game_fork_create', {'owner': 6})
        thread.join()
        self.assertEqual(seen[4:], ['fork_create', '6'])
        payload = json.loads(result['content'][0]['text'])
        self.assertEqual(payload['fork'], {'fork': 1, 'hash': 42, 'time_ms': 0})

    def test_fork_clone_writes_fork_request_and_returns_fork_json(self):
        seen = []
        def act(request):
            seen.extend(request)
            self.response(int(request[2]), fork={'fork': 2, 'src': 1, 'hash': 43, 'time_ms': 0})
        thread = self.peer(act)
        result = mcp.invoke(self.bridge, 'game_fork_clone', {'fork': 1, 'owner': 6})
        thread.join()
        self.assertEqual(seen[4:], ['fork_clone', '1', '6'])
        payload = json.loads(result['content'][0]['text'])
        self.assertEqual(payload['fork'], {'fork': 2, 'src': 1, 'hash': 43, 'time_ms': 0})

    def test_fork_step_writes_ordered_arguments(self):
        seen = []
        def act(request):
            seen.extend(request)
            self.response(int(request[2]), fork={'fork': 3, 'hash': 7, 'time_ms': 40})
        thread = self.peer(act)
        mcp.invoke(self.bridge, 'game_fork_step',
                   {'fork': 3, 'action': 2, 'gid': 10, 'target': 11, 'ticks': 5})
        thread.join()
        self.assertEqual(seen[4:], ['fork_step', '3', '2', '10', '11', '0', '0', '0', '0', '5'])

    def test_ppm_rejects_truncation_and_excessive_dimensions(self):
        for value in (b'P6\n2 1\n255\nabc', b'P6\n9999 1\n255\nabc', b'P3\n1 1\n255\n0 0 0'):
            with self.assertRaises(mcp.AgentError):
                mcp.ppm_png(value)

    def test_release_preempts_an_unacknowledged_request(self):
        self.bridge.capabilities()
        self.bridge.pending = self.bridge.sequence = 4
        thread = self.peer(lambda r: self.response(int(r[2])))
        result = self.bridge.command('release')
        thread.join()
        self.assertEqual(result['id'], 5)

    def test_stdio_handshake_and_tools_in_separate_process(self):
        self.bridge.close()
        requests = [
            {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'protocolVersion': mcp.PROTOCOL}},
            {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
            {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'},
            {'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call', 'params': {'name': 'game_capabilities'}},
        ]
        p = subprocess.run([sys.executable, '-B', str(MODULE), '--directory', str(self.directory)],
                           input=''.join(json.dumps(r) + '\n' for r in requests), capture_output=True, text=True, timeout=5)
        self.assertEqual(p.returncode, 0, p.stderr)
        responses = [json.loads(line) for line in p.stdout.splitlines()]
        self.assertEqual([r['id'] for r in responses], [1, 2, 3])
        self.assertEqual(len(responses[1]['result']['tools']), 14)
        self.assertIn('game_status', [tool['name'] for tool in responses[1]['result']['tools']])
        self.assertIn(SESSION, responses[2]['result']['content'][0]['text'])

    def test_oversized_line_cannot_smuggle_a_tail_command(self):
        wire = b'x' * (mcp.MAX_MESSAGE + 1) + b'{"jsonrpc":"2.0","id":4,"method":"ping"}\n'
        wire += b'{"jsonrpc":"2.0","id":5,"method":"ping"}\n'
        result = io.StringIO()
        mcp.serve(self.bridge, io.BytesIO(wire), result)
        replies = [json.loads(line) for line in result.getvalue().splitlines()]
        self.assertEqual(len(replies), 2)
        self.assertEqual(replies[0]['error']['code'], -32600)
        self.assertEqual(replies[1]['id'], 5)


if __name__ == '__main__':
    unittest.main()
