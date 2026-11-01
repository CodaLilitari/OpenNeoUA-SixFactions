#!/usr/bin/env python3
"""Opt-in local OpenNeoUA gameplay tools over MCP stdio (Python 3.9+)."""
import argparse
import base64
import contextlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
import uuid
import zlib

PROTOCOL = '2025-06-18'
MAX_MESSAGE = 65536
MAX_RESPONSE = 1048576
KEYS = {chr(97 + i): 4 + i for i in range(26)}
KEYS.update({str(i): 29 + i for i in range(1, 10)})
KEYS.update({'0': 39, 'return': 40, 'escape': 41, 'backspace': 42,
             'tab': 43, 'space': 44, 'right': 79, 'left': 80, 'down': 81,
             'up': 82, 'lctrl': 224, 'lshift': 225, 'lalt': 226,
             'rctrl': 228, 'rshift': 229, 'ralt': 230})
KEYS.update({'f' + str(i): 57 + i for i in range(1, 13)})


class AgentError(Exception):
    pass


def read_bounded(path, limit):
    if path.is_symlink():
        raise AgentError('Refusing a symbolic link in the session directory')
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise AgentError('Session file exceeds the size limit')
    return data


def read_json(path):
    try:
        result = json.loads(read_bounded(path, MAX_RESPONSE))
        if not isinstance(result, dict):
            raise AgentError('Expected an object in ' + path.name)
        return result
    except (OSError, ValueError) as exc:
        raise AgentError('Cannot read ' + path.name + ': ' + str(exc)) from exc


def integer(value, low, high, name):
    if type(value) is not int or not low <= value <= high:
        raise AgentError(f'{name} must be an integer from {low} to {high}')
    return value


def ppm_png(data):
    """Encode a bounded binary P6 RGB frame as PNG, without external packages."""
    match = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255(?:\r\n|\n)', data)
    if not match:
        raise AgentError('Unsupported screenshot header')
    width, height = map(int, match.groups())
    if not (1 <= width <= 4096 and 1 <= height <= 4096):
        raise AgentError('Screenshot dimensions exceed the supported range')
    pixels = data[match.end():]
    if len(pixels) != width * height * 3:
        raise AgentError('Incomplete screenshot pixels')
    def chunk(kind, payload):
        return struct.pack('!I', len(payload)) + kind + payload + struct.pack('!I', zlib.crc32(kind + payload))
    stride = width * 3
    rows = b''.join(b'\0' + pixels[y * stride:(y + 1) * stride] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 3)) + chunk(b'IEND', b''))


class Bridge:
    def __init__(self, directory, timeout=8.0):
        raw = Path(directory)
        if not raw.is_absolute() or not raw.is_dir() or raw.is_symlink():
            raise AgentError('Use an existing absolute session directory')
        self.directory = raw.resolve()
        if any((parent / '.git').exists() for parent in (self.directory, *self.directory.parents)):
            raise AgentError('The agent session directory must be outside a Git checkout')
        self.timeout = timeout
        self.session = None
        self.sequence = 0
        self.pending = None
        self.lock_token = uuid.uuid4().hex
        self.lock = self.directory / 'adapter.lock'
        try:
            with self.lock.open('x', encoding='ascii') as stream:
                stream.write(self.lock_token)
        except FileExistsError as exc:
            raise AgentError('Another adapter owns this directory; inspect a stale adapter.lock manually') from exc

    def close(self):
        with contextlib.suppress(OSError, AgentError):
            if read_bounded(self.lock, 64).decode('ascii') == self.lock_token:
                self.lock.unlink()

    def capabilities(self):
        info = read_json(self.directory / 'session.json')
        session = info.get('session')
        if info.get('protocol') != 1 or not isinstance(session, str) or not re.fullmatch(r'[a-fA-F0-9]{8,128}', session):
            raise AgentError('Unsupported engine session protocol')
        if session != self.session:
            self.session, self.sequence, self.pending = session, 0, None
            with contextlib.suppress(AgentError):
                response = read_json(self.directory / 'response.json')
                if response.get('session') == session:
                    self.sequence = integer(response.get('id'), 0, 2147483647, 'response id')
            with contextlib.suppress(OSError, AgentError, UnicodeError):
                request = read_bounded(self.directory / 'request.txt', 4096).decode('ascii').split()
                if len(request) >= 4 and request[0] == '1' and request[1] == session:
                    old_id = integer(int(request[2]), 1, 2147483647, 'request id')
                    if old_id > self.sequence:
                        self.pending = old_id
                        self.sequence = old_id
        return info

    def _response(self, request_id):
        with contextlib.suppress(AgentError):
            response = read_json(self.directory / 'response.json')
            if (response.get('protocol') == 1 and response.get('session') == self.session
                    and type(response.get('id')) is int and response.get('id') == request_id):
                return response
        return None

    def command(self, operation, args=()):
        capabilities = self.capabilities()
        if operation not in capabilities.get('operations', []):
            raise AgentError('Engine does not advertise operation: ' + operation)
        if self.pending is not None:
            if self._response(self.pending) is not None:
                self.pending = None
            elif operation != 'release':
                raise AgentError('Previous command has no completion receipt; inspect the game or use release_all')
        if self.sequence >= 2147483647:
            raise AgentError('Request ID space exhausted; start a new game session')
        self.sequence += 1
        request_id = self.sequence
        budget = max(self.timeout, 30.0) if operation == 'text' else self.timeout
        expiry_ms = int((time.time() + budget) * 1000)
        request = ' '.join(map(str, (1, self.session, request_id, expiry_ms, operation, *args))) + '\n'
        if len(request.encode('ascii')) > 4096:
            raise AgentError('Request too large')
        target = self.directory / 'request.txt'
        temp = self.directory / ('request-' + self.lock_token + '.tmp')
        if target.is_symlink():
            raise AgentError('Refusing a symbolic request file')
        with temp.open('x', encoding='ascii', newline='\n') as stream:
            stream.write(request)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, target)
        self.pending = request_id
        deadline = time.monotonic() + budget
        while time.monotonic() < deadline:
            response = self._response(request_id)
            if response is not None:
                self.pending = None
                if response.get('ok') is not True:
                    raise AgentError(str(response.get('error', 'Engine rejected the command')))
                return response
            # A restarted engine must never receive an automatic replay.
            try:
                if read_json(self.directory / 'session.json').get('session') != self.session:
                    raise AgentError('Engine restarted; command completion is unknown and was not retried')
            except AgentError as exc:
                if 'restarted' in str(exc):
                    raise
            time.sleep(0.025)
        raise AgentError('No engine completion receipt before timeout; command was not retried')


def schema(properties=None, required=()):
    return {'type': 'object', 'properties': properties or {}, 'required': list(required), 'additionalProperties': False}


INT = {'type': 'integer'}
TOOLS = [
    ('game_capabilities', 'Read the opted-in engine session and supported controls.', schema()),
    ('game_observe', 'Observe the rendered game and local-player state; returns a native PNG image.', schema()),
    ('game_key_press', 'Press a normal game key for a bounded duration. Debug bindings follow game settings.',
     schema({'key': {'type': 'string', 'enum': list(KEYS)}, 'duration_ms': {'type': 'integer', 'minimum': 1, 'maximum': 2000}}, ('key',))),
    ('game_mouse_move', 'Move in framebuffer pixel coordinates, with optional relative motion for the view.',
     schema({'x': INT, 'y': INT, 'dx': INT, 'dy': INT}, ('x', 'y'))),
    ('game_mouse_click', 'Press and release a mouse button at framebuffer pixel coordinates.',
     schema({'x': INT, 'y': INT, 'button': {'type': 'string', 'enum': ['left', 'middle', 'right']},
             'duration_ms': {'type': 'integer', 'minimum': 1, 'maximum': 2000}}, ('x', 'y'))),
    ('game_type_text', 'Type at most 128 printable ASCII characters into the currently focused game control.',
     schema({'text': {'type': 'string', 'minLength': 1, 'maxLength': 128}}, ('text',))),
    ('game_release_all', 'Release agent-held controls, including an outstanding timed action.', schema()),
]


def invoke(bridge, name, args):
    definition = next((item for item in TOOLS if item[0] == name), None)
    if definition is None:
        raise AgentError('Unknown tool')
    if not isinstance(args, dict):
        raise AgentError('arguments must be an object')
    spec = definition[2]
    if set(args) - set(spec['properties']) or set(spec['required']) - set(args):
        raise AgentError('Unexpected or missing tool arguments')
    duration = lambda: integer(args.get('duration_ms', 100), 1, 2000, 'duration_ms')
    xy = lambda: (integer(args['x'], 0, 4095, 'x'), integer(args['y'], 0, 4095, 'y'))
    if name == 'game_capabilities':
        result = bridge.capabilities()
    elif name == 'game_observe':
        result = bridge.command('observe')
    elif name == 'game_key_press':
        key = args['key']
        if not isinstance(key, str) or key not in KEYS:
            raise AgentError('Unsupported key name')
        result = bridge.command('key', (KEYS[key], duration()))
    elif name == 'game_mouse_move':
        result = bridge.command('move', (*xy(), integer(args.get('dx', 0), -4095, 4095, 'dx'),
                                         integer(args.get('dy', 0), -4095, 4095, 'dy')))
    elif name == 'game_mouse_click':
        button = args.get('button', 'left')
        if not isinstance(button, str) or button not in ('left', 'middle', 'right'):
            raise AgentError('Unsupported mouse button')
        result = bridge.command('click', (('left', 'middle', 'right').index(button) + 1, *xy(), duration()))
    elif name == 'game_type_text':
        value = args['text']
        if not isinstance(value, str) or not 1 <= len(value) <= 128 or any(not 32 <= ord(c) <= 126 for c in value):
            raise AgentError('text must contain 1 to 128 printable ASCII characters')
        result = bridge.command('text', (value.encode('ascii').hex(),))
    else:
        result = bridge.command('release')
    content = [{'type': 'text', 'text': json.dumps(result, ensure_ascii=True)}]
    if name == 'game_observe':
        if result.get('screenshot') != 'frame.ppm':
            raise AgentError('Observation did not include the expected frame file')
        png = ppm_png(read_bounded(bridge.directory / 'frame.ppm', 4096 * 4096 * 3 + 128))
        content.append({'type': 'image', 'mimeType': 'image/png', 'data': base64.b64encode(png).decode('ascii')})
    return {'content': content, 'isError': False}


def serve(bridge, source, sink):
    initialized = False
    for line in iter(lambda: source.readline(MAX_MESSAGE + 1), b''):
        request_id = None
        try:
            if len(line) > MAX_MESSAGE:
                while not line.endswith(b'\n'):
                    line = source.readline(MAX_MESSAGE + 1)
                    if not line:
                        break
                raise AgentError('MCP message too large')
            message = json.loads(line)
            if not isinstance(message, dict) or message.get('jsonrpc') != '2.0' or not isinstance(message.get('method'), str):
                raise AgentError('Invalid JSON-RPC request')
            request_id = message.get('id')
            if request_id is not None and type(request_id) not in (int, str):
                raise AgentError('Invalid request id')
            method, params = message['method'], message.get('params', {})
            if not isinstance(params, dict):
                raise AgentError('params must be an object')
            if 'id' not in message:
                if method == 'notifications/initialized':
                    initialized = True
                continue
            if method == 'initialize':
                initialized = False
                result = {'protocolVersion': PROTOCOL, 'serverInfo': {'name': 'openneoua-agent', 'version': '0.1.0'},
                          'capabilities': {'tools': {'listChanged': False}},
                          'instructions': 'Controls one explicitly opted-in local game. Observe after actions; action completion is not proof of gameplay outcome.'}
            elif method == 'ping':
                result = {}
            elif not initialized:
                raise AgentError('Complete the MCP initialization handshake first')
            elif method == 'tools/list':
                result = {'tools': [{'name': n, 'description': d, 'inputSchema': s} for n, d, s in TOOLS]}
            elif method == 'tools/call':
                try:
                    result = invoke(bridge, params.get('name'), params.get('arguments', {}))
                except (AgentError, OSError, ValueError) as exc:
                    result = {'content': [{'type': 'text', 'text': str(exc)}], 'isError': True}
            else:
                sink.write(json.dumps({'jsonrpc': '2.0', 'id': request_id, 'error': {'code': -32601, 'message': 'Method not found'}}) + '\n')
                sink.flush()
                continue
            response = {'jsonrpc': '2.0', 'id': request_id, 'result': result}
        except (ValueError, UnicodeError) as exc:
            response = {'jsonrpc': '2.0', 'id': None, 'error': {'code': -32700, 'message': 'Invalid JSON'}}
        except AgentError as exc:
            response = {'jsonrpc': '2.0', 'id': request_id, 'error': {'code': -32600, 'message': str(exc)}}
        sink.write(json.dumps(response, ensure_ascii=True, separators=(',', ':')) + '\n')
        sink.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', required=True, help='Same absolute directory as OPENNEOUA_AGENT_DIR')
    args = parser.parse_args()
    bridge = None
    try:
        bridge = Bridge(args.directory)
        serve(bridge, sys.stdin.buffer, sys.stdout)
    except (AgentError, OSError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
    finally:
        if bridge is not None:
            bridge.close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
