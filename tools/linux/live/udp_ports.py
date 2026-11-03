"""Print the UDP ports a process has bound (for in-namespace iptables partition drills)."""
import os, sys
pid = sys.argv[1]
inodes = set()
for fd in os.listdir(f'/proc/{pid}/fd'):
    try:
        link = os.readlink(f'/proc/{pid}/fd/{fd}')
    except OSError:
        continue
    if link.startswith('socket:['):
        inodes.add(link[8:-1])
for table in ('udp', 'udp6'):
    try:
        lines = open(f'/proc/{pid}/net/{table}').read().splitlines()[1:]
    except OSError:
        continue
    for line in lines:
        f = line.split()
        if f[9] in inodes:
            print(table, int(f[1].split(':')[1], 16))
