#!/usr/bin/env python3
"""Multi-instance OpenNeoUA matches on one Linux box (Xvfb, llvmpipe, agent bridge).

One command for a match, here host + client + -bot client on level 132:

    python3 -B tools/linux/live/drive.py match --retail RETAIL --binary BUILD/engine/OpenNeoUA \\
        --lab LAB --level 132 --host HOST1:resistance --client C1:ghorkov --bot BOT1:mykonian:veteran

`match` assembles one runtime per instance (assemble_runtime.py, then real copies),
creates a private network namespace (ns_holder.sh), starts one Xvfb and one engine
per instance (launch_instance.sh) with the unattended lobby flags (-net-host,
-net-join, -net-name, -net-faction, -net-ready, -net-start-when), and polls every
instance's agent `status` until all of them passed the start barrier, an instance
reports a failed flag step, an engine exits, or --timeout runs out. Evidence is
collected in LAB/collected-<stamp>/ (collect_logs.sh). Without --keep the
instances, displays and namespace are stopped at the end. Exit code 0 means every
instance reached the game.

    drive.py status --lab LAB      # every instance's lobby/network state (JSON lines)
    drive.py stop --lab LAB        # stop engines, Xvfb, sampler and namespace
    drive.py selftest              # adapter plumbing against a fake engine (no game)

LAB must be outside any Git checkout (the engine refuses agent directories inside
one). Levels 200 and 201 are the generated six-start maps (tools/make_six_faction_map.py);
`match` generates them from the retail levels when asked for. As a library
(`import drive`), Inst wraps the repo's MCP adapter for drills: status, observe,
fork_view, netlog, key/click/type. Nothing here retries a mutation automatically.
"""
import argparse
import base64
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
_spec = importlib.util.spec_from_file_location('openneoua_mcp', REPO / 'tools/agent/openneoua_mcp.py')
mcp = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mcp)

FACTIONS = {'resistance': 1, 'sulgogars': 2, 'sulgogar': 2, 'mykonian': 3, 'mykonier': 3,
            'taerkasten': 4, 'blacksect': 5, 'black': 5, 'ghorkov': 6}
BOT_FACTION = {1: 'resistance', 2: 'sulgogars', 3: 'mykonian', 4: 'taerkasten', 5: 'blacksect', 6: 'ghorkov'}
DIFFICULTIES = ('cadet', 'normal', 'veteran')
SIX_FACTION_SOURCES = {200: 'l100100.ldf', 201: 'l140140.ldf'}   # docs/six-faction-multiplayer.md
CALLSIGN = re.compile(r'[!-)+-~]{1,20}')   # printable ASCII without space and '*', at most 20


class Inst:
    """One running engine, addressed through its OPENNEOUA_AGENT_DIR."""

    def __init__(self, name, agent_dir, runtime_dir=None, timeout=8.0, wait_session=180, pid_file=None):
        self.name, self.dir = name, Path(agent_dir)
        self.runtime = Path(runtime_dir) if runtime_dir else None
        self.pid_file = Path(pid_file) if pid_file else None
        deadline = time.monotonic() + wait_session
        while not (self.dir / 'session.json').exists():      # written by Agent::Init after InitGame
            if not self.alive():
                raise RuntimeError(f'{name}: the engine exited before its agent session started\n' + self.log_tail())
            if time.monotonic() > deadline:
                raise TimeoutError(f'{name}: no session.json (engine failed init?)\n' + self.log_tail())
            time.sleep(0.5)
        self.bridge = mcp.Bridge(self.dir, timeout)

    def close(self):
        self.bridge.close()

    def pid(self):
        try:
            return int(self.pid_file.read_text().split()[0]) if self.pid_file else None
        except (OSError, ValueError, IndexError):
            return None

    def alive(self):
        pid = self.pid()
        if pid is None:
            return True
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
        try:   # a zombie still answers kill(0)
            return Path(f'/proc/{pid}/stat').read_text().split(')')[-1].split()[0] != 'Z'
        except OSError:
            return False

    def log_tail(self, lines=25):
        if not self.runtime:
            return ''
        path = self.runtime / 'Data/Env/ypa_log.txt'
        try:
            return '\n'.join(path.read_text(errors='replace').splitlines()[-lines:])
        except OSError:
            return f'(no {path})'

    def call(self, tool, **args):
        try:
            result = mcp.invoke(self.bridge, tool, args)
        except mcp.AgentError as exc:
            if 'Previous command' in str(exc) and tool != 'game_release_all':
                mcp.invoke(self.bridge, 'game_release_all', {})   # clears the stuck request; no replay
                result = mcp.invoke(self.bridge, tool, args)
            else:
                raise
        payload = json.loads(result['content'][0]['text'])
        png = base64.b64decode(result['content'][1]['data']) if len(result['content']) > 1 else None
        return payload, png

    # --- observation -------------------------------------------------------------------
    def status(self):
        """The status receipt (local player, lobby, network), or None while the engine
        cannot answer (a level load or the start barrier holds its frame)."""
        try:
            return self.bridge.command('status')
        except mcp.AgentError as exc:
            text = str(exc)
            if 'Previous command' in text:
                # The last status had no receipt yet. status changes nothing, so
                # preempt it with release and ask again on the next poll.
                try:
                    self.bridge.command('release')
                except mcp.AgentError:
                    pass
                return None
            if 'No engine completion receipt' in text or 'restarted' in text:
                return None
            raise

    def observe(self, save_png=None):
        state, png = self.call('game_observe')
        if save_png and png:
            Path(save_png).write_bytes(png)
        return state

    def fork_view(self, owner=0):
        made, _ = self.call('game_fork_create', owner=owner)
        fork = made['fork']['fork']
        try:
            view, _ = self.call('game_fork_observe', fork=fork, owner=owner)   # pinned to local owner in net games
            return view['fork']
        finally:
            self.call('game_fork_discard', fork=fork)

    def netlog(self, pattern=None):
        text = (self.runtime / 'Data/Env/ypa_log.txt').read_text(errors='replace') if self.runtime else ''
        return [l for l in text.splitlines() if not pattern or re.search(pattern, l)]

    # --- input ---------------------------------------------------------------------------
    def key(self, key, ms=60):
        return self.call('game_key_press', key=key, duration_ms=ms)[0]

    def click(self, x, y, button='left', ms=80):
        return self.call('game_mouse_click', x=x, y=y, button=button, duration_ms=ms)[0]

    def type(self, text):
        return self.call('game_type_text', text=text)[0]

    def wait(self, predicate, timeout=60, period=1.0):
        """Poll status (no screenshots) until predicate(receipt) holds."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = self.status()
            if state is not None and predicate(state):
                return state
            time.sleep(period)
        raise TimeoutError(f'{self.name}: condition not reached in {timeout}s')


# --- match -----------------------------------------------------------------------------------
class Spec:
    def __init__(self, role, text):
        parts = text.split(':')
        self.role, self.name = role, parts[0]
        if not CALLSIGN.fullmatch(self.name):
            raise SystemExit(f'callsign {self.name!r}: 1-20 printable characters, no spaces or "*"')
        if role == 'bot':
            if len(parts) not in (2, 3):
                raise SystemExit(f'--bot {text}: use NAME:FACTION[:DIFFICULTY]')
            self.faction = faction_owner(parts[1])
            self.difficulty = parts[2].lower() if len(parts) == 3 else 'normal'
            if self.difficulty not in DIFFICULTIES:
                raise SystemExit(f'--bot {text}: difficulty is one of {", ".join(DIFFICULTIES)}')
        else:
            if len(parts) > 2:
                raise SystemExit(f'--{role} {text}: use NAME[:FACTION]')
            self.faction = faction_owner(parts[1]) if len(parts) == 2 else None
            self.difficulty = None

    def engine_args(self, level, players):
        if self.role == 'host':
            args = ['-net-host', str(level), '-net-name', self.name, '-net-start-when', str(players)]
        else:
            args = ['-net-join', '127.0.0.1', '-net-name', self.name, '-net-ready']
        if self.role == 'bot':   # the -bot faction is also the lobby faction
            args = ['-bot', f'{BOT_FACTION[self.faction]}:{self.difficulty}'] + args
        elif self.faction:
            args += ['-net-faction', str(self.faction)]
        return args


def faction_owner(text):
    key = re.sub(r'[\s_-]', '', text.lower())
    if key in FACTIONS:
        return FACTIONS[key]
    if key in ('1', '2', '3', '4', '5', '6'):
        return int(key)
    raise SystemExit(f'unknown faction {text!r}: one of {", ".join(sorted(set(FACTIONS)))} or 1-6')


def inside_git(path):
    return any((p / '.git').exists() for p in (path, *path.parents))


def free_display(start, taken):
    n = start
    while n in taken or Path(f'/tmp/.X11-unix/X{n}').exists() or Path(f'/tmp/.X{n}-lock').exists():
        n += 1
    return n


def run(cmd, **kw):
    print('+', ' '.join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], check=True, **kw)


def ci(path, *parts):
    """path/parts with each part matched case-insensitively (retail trees vary: DATA, Data)."""
    for part in parts:
        match = next((c for c in path.iterdir() if c.name.lower() == part.lower()), None) if path.is_dir() else None
        if match is None:
            return None
        path = match
    return path


def check_retail(retail):
    locale = ci(retail, 'LOCALE')
    missing = [] if locale and any(p.suffix.lower() == '.dll' for p in locale.iterdir()) else ['LOCALE/*.DLL']
    missing += ['/'.join(parts) for parts in (('DATA', 'SET46'), ('DATA', 'FONTS'), ('DATA', 'SCRIPTS'),
                                              ('LEVELS', 'MULTI')) if ci(retail, *parts) is None]
    if missing:
        raise SystemExit(f'retail data at {retail} lacks {", ".join(missing)}; the engine would stop at init')


def prepare(args, lab, specs):
    retail = Path(args.retail).resolve()
    check_retail(retail)
    binary = Path(args.binary).resolve()
    if not binary.is_file():
        raise SystemExit(f'no engine binary at {binary}')
    maps = lab / 'maps'
    if args.level in SIX_FACTION_SOURCES and not any(maps.glob(f'Levels/Multi/l{args.level}{args.level}.ldf')):
        source = ci(retail, 'LEVELS', 'MULTI', SIX_FACTION_SOURCES[args.level])
        if source is None:
            raise SystemExit(f'level {args.level} is generated from {SIX_FACTION_SOURCES[args.level]}, '
                             'which the retail LEVELS/MULTI lacks')
        run([sys.executable, '-I', REPO / 'tools/make_six_faction_map.py', source, maps,
             '--script-root', ci(retail, 'DATA', 'SCRIPTS'), '--level-id', args.level], stdout=subprocess.DEVNULL)
    base = lab / 'rt-base'
    if args.fresh and base.exists():
        shutil.rmtree(base)
    if not base.exists():
        cmd = [sys.executable, '-I', HERE / 'assemble_runtime.py', '--retail', retail, '--repo', REPO,
               '--binary', binary, '--out', base, '--low-cpu']
        if maps.is_dir():
            cmd += ['--extra-levels', maps]
        run(cmd)
    runtimes = {}
    for spec in specs:
        rt = lab / f'rt-{spec.name}'
        if args.fresh and rt.exists():
            shutil.rmtree(rt)
        if not rt.exists():
            shutil.copytree(base, rt, symlinks=False)
        shutil.copy2(binary, rt / 'OpenNeoUA')   # always the binary under test
        runtimes[spec.name] = rt
    return runtimes


def start_namespace(lab):
    pid = subprocess.run(['bash', str(HERE / 'ns_holder.sh')], check=True, capture_output=True,
                         text=True, timeout=30).stdout.strip()
    (lab / 'pids').mkdir(parents=True, exist_ok=True)
    (lab / 'pids/netns.bgpid').write_text(pid + '\n')
    print(f'network namespace held by pid {pid}', flush=True)
    return pid


def stop(lab):
    pids = lab / 'pids'
    if not pids.is_dir():
        return
    def kill(path, sig):
        try:
            os.kill(int(path.read_text().split()[0]), sig)
        except (OSError, ValueError, IndexError):
            pass
    engines = [p for p in pids.glob('*.pid') if not p.name.endswith('.xvfb.pid')]
    for p in engines:
        kill(p, signal.SIGTERM)
    time.sleep(3)
    for p in engines:
        kill(p, signal.SIGKILL)
    for p in pids.glob('*.xvfb.pid'):
        kill(p, signal.SIGTERM)
    sampler = pids / 'sampler.bgpid'
    if sampler.exists():   # its own session: stop the loop and its sleep together
        try:
            os.killpg(int(sampler.read_text().split()[0]), signal.SIGTERM)
        except (OSError, ValueError, IndexError):
            pass
    for p in pids.glob('*.bgpid'):
        kill(p, signal.SIGTERM)
    for p in list(pids.glob('*.pid')) + list(pids.glob('*.bgpid')):
        p.unlink()
    print(f'stopped the instances under {lab}', flush=True)


def describe(name, state):
    net = (state or {}).get('net') or {}
    auto = net.get('auto') or {}
    lobby = ' '.join(f"{r.get('name')}/{r.get('faction')}{'+' if r.get('ready') else '-'}"
                     for r in net.get('lobby', []))
    return (f"{name:8} screen={(state or {}).get('screen', '?'):5} step={auto.get('step', '-'):11} "
            f"role={net.get('role', '?'):6} {net.get('transport', '?'):6} level={net.get('level_id', 0):<3} "
            f"owner={net.get('local_owner', 0)} started={net.get('started', False)} lobby=[{lobby}]")


def match(args):
    lab = Path(args.lab).resolve()
    if inside_git(lab):
        raise SystemExit(f'{lab} is inside a Git checkout; the agent bridge refuses that')
    specs = [Spec('host', args.host)] + [Spec('client', c) for c in args.client or []] + \
            [Spec('bot', b) for b in args.bot or []]
    names = [s.name for s in specs]
    if len(set(n.lower() for n in names)) != len(names):
        raise SystemExit('callsigns must be unique')
    if len(specs) > 6:
        raise SystemExit('at most six players')
    if (lab / 'pids').is_dir() and any((lab / 'pids').glob('*.pid')):
        raise SystemExit(f'{lab} has running instances; drive.py stop --lab {lab} first')
    lab.mkdir(parents=True, exist_ok=True)
    runtimes = prepare(args, lab, specs)
    env = dict(os.environ)
    env.pop('OPENNEOUA_TRANSPORT', None)
    if args.transport == 'enet':
        env['OPENNEOUA_TRANSPORT'] = 'enet'
    prefix = [] if args.no_netns else ['nsenter', '-t', start_namespace(lab), '-n']
    insts, displays, ok = [], set(), False
    try:
        for spec in specs:
            display = free_display(args.display_base, displays)
            displays.add(display)
            run(prefix + ['bash', HERE / 'launch_instance.sh', spec.name, display, runtimes[spec.name], lab] +
                spec.engine_args(args.level, len(specs)), env=env)
        if args.sample:
            sampler = subprocess.Popen(['bash', str(HERE / 'sample.sh'), str(lab), str(args.sample)],
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, start_new_session=True)
            (lab / 'pids/sampler.bgpid').write_text(f'{sampler.pid}\n')
        failure = None
        for spec in specs:
            try:
                insts.append(Inst(spec.name, lab / 'agent' / spec.name, runtimes[spec.name],
                                  timeout=args.agent_timeout, wait_session=args.init_timeout,
                                  pid_file=lab / 'pids' / f'{spec.name}.pid'))
            except (RuntimeError, TimeoutError) as exc:
                failure = str(exc)
                break
        deadline, last = time.monotonic() + args.timeout, {}
        while failure is None:
            done = True
            for inst in insts:
                if not inst.alive():
                    failure = f'{inst.name}: the engine exited\n{inst.log_tail()}'
                    break
                state = inst.status()
                line = describe(inst.name, state) if state is not None else f'{inst.name:8} (busy: no receipt)'
                if line != last.get(inst.name):
                    print(time.strftime('%H:%M:%S'), line, flush=True)
                    last[inst.name] = line
                net = (state or {}).get('net') or {}
                auto = net.get('auto') or {}
                if auto.get('step') == 'failed':
                    failure = f"{inst.name}: {auto.get('error')}"
                    break
                done = done and state is not None and bool(net.get('started'))
            if failure is None and done:
                break
            if failure is None and time.monotonic() > deadline:
                failure = f'not every instance reached the game within {args.timeout} s'
            if failure is None:
                time.sleep(args.poll)
        if failure:
            print('MATCH FAILED:', failure, flush=True)
        else:
            ok = True
            print(f'MATCH STARTED: {len(insts)} instances in game on level {args.level} '
                  f'({args.transport})', flush=True)
            for inst in insts:
                state = inst.status() or {}
                print('  ' + json.dumps({'name': inst.name, 'owner': state.get('owner'),
                                         'net': state.get('net')}, separators=(',', ':')), flush=True)
    finally:
        for inst in insts:
            inst.close()
        collected = subprocess.run(['bash', str(HERE / 'collect_logs.sh'), str(lab)] +
                                   [str(runtimes[s.name]) for s in specs],
                                   capture_output=True, text=True).stdout.strip()
        print(f'evidence: {collected}', flush=True)
        if args.keep:
            print(f'left running (--keep); stop with: python3 -B {HERE / "drive.py"} stop --lab {lab}', flush=True)
        else:
            stop(lab)
    return 0 if ok else 1


def status(args):
    lab = Path(args.lab).resolve()
    for agent in sorted((lab / 'agent').glob('*')):
        name = agent.name
        try:
            inst = Inst(name, agent, lab / f'rt-{name}', timeout=args.agent_timeout, wait_session=0,
                        pid_file=lab / 'pids' / f'{name}.pid')
        except (RuntimeError, TimeoutError, mcp.AgentError) as exc:
            print(json.dumps({'name': name, 'error': str(exc).splitlines()[0]}))
            continue
        try:
            print(json.dumps({'name': name, 'alive': inst.alive(), 'status': inst.status()}))
        finally:
            inst.close()
    return 0


def selftest():
    """Fake-engine round trip (no game needed): session.json + a responder thread."""
    import tempfile
    import threading
    with tempfile.TemporaryDirectory(prefix='drive-selftest-') as d:
        d = Path(d)
        (d / 'session.json').write_text(json.dumps({'protocol': 1, 'session': '0123456789abcdef',
                                                     'operations': ['observe', 'status', 'key', 'release']}))
        def engine():
            seen = 0
            end = time.monotonic() + 5
            while time.monotonic() < end and seen < 3:
                req = d / 'request.txt'
                if req.exists():
                    parts = req.read_text().split()
                    if int(parts[2]) > seen:
                        seen = int(parts[2])
                        resp = {'protocol': 1, 'session': parts[1], 'id': seen, 'ok': True, 'screen': 'menu',
                                'op': parts[4], 'net': {'lobby_screen': 'session', 'auto': {'step': 'wait_start'}}}
                        if parts[4] == 'observe':
                            (d / 'frame.ppm').write_bytes(b'P6\n1 1\n255\n\x00\x00\x00')
                            resp['screenshot'] = 'frame.ppm'
                        tmp = d / 'r.tmp'
                        tmp.write_text(json.dumps(resp))
                        tmp.replace(d / 'response.json')
                time.sleep(0.01)
        t = threading.Thread(target=engine)
        t.start()
        i = Inst('fake', d, timeout=2)
        try:
            i.key('return')
            st = i.status()
            ob = i.observe()
        finally:
            i.close()
            t.join()
        assert st['op'] == 'status' and st['net']['auto']['step'] == 'wait_start', st
        assert ob['screen'] == 'menu' and ob['id'] == 3, ob
        print('drive.py selftest OK')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)
    m = sub.add_parser('match', help='start a scripted match and wait until every instance is in game')
    m.add_argument('--retail', required=True, help='complete retail package (has DATA/, LEVELS/, LOCALE/)')
    m.add_argument('--binary', required=True, help='engine binary under test')
    m.add_argument('--lab', required=True, help='run directory outside any Git checkout')
    m.add_argument('--level', required=True, type=int, help='network level id (200/201: generated six-start maps)')
    m.add_argument('--host', required=True, metavar='NAME[:FACTION]')
    m.add_argument('--client', action='append', metavar='NAME[:FACTION]')
    m.add_argument('--bot', action='append', metavar='NAME:FACTION[:DIFFICULTY]')
    m.add_argument('--transport', choices=('zndnet', 'enet'), default='zndnet')
    m.add_argument('--timeout', type=int, default=900, help='seconds until every instance must be in game')
    m.add_argument('--init-timeout', type=int, default=240, help='seconds for an engine to start its agent session')
    m.add_argument('--agent-timeout', type=float, default=8.0, help='seconds to wait for one status receipt')
    m.add_argument('--poll', type=float, default=2.0)
    m.add_argument('--display-base', type=int, default=150)
    m.add_argument('--sample', type=int, default=0, help='sample CPU/RSS every N seconds into logs/resources.csv')
    m.add_argument('--fresh', action='store_true', help='re-assemble the runtimes')
    m.add_argument('--keep', action='store_true', help='leave the match running after it started')
    m.add_argument('--no-netns', action='store_true', help='use the host network (port 61234 must be free)')
    s = sub.add_parser('status', help="print every instance's status receipt")
    s.add_argument('--lab', required=True)
    s.add_argument('--agent-timeout', type=float, default=8.0)
    t = sub.add_parser('stop', help='stop every instance, display, sampler and the namespace')
    t.add_argument('--lab', required=True)
    sub.add_parser('selftest', help='adapter plumbing against a fake engine')
    args = ap.parse_args()
    if args.command == 'match':
        return match(args)
    if args.command == 'status':
        return status(args)
    if args.command == 'stop':
        stop(Path(args.lab).resolve())
        return 0
    return selftest()


if __name__ == '__main__':
    raise SystemExit(main())
