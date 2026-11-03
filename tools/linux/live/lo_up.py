"""Bring up the loopback interface inside a fresh network namespace (no iproute2 needed)."""
import fcntl, socket, struct
SIOCGIFFLAGS, SIOCSIFFLAGS, IFF_UP = 0x8913, 0x8914, 0x1
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
ifr = struct.pack('16sH14s', b'lo', 0, b'\0' * 14)
flags = struct.unpack('16sH14s', fcntl.ioctl(s, SIOCGIFFLAGS, ifr))[1]
fcntl.ioctl(s, SIOCSIFFLAGS, struct.pack('16sH14s', b'lo', flags | IFF_UP, b'\0' * 14))
print('lo up')
