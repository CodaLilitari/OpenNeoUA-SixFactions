#!/usr/bin/env bash
# Create a private user+network namespace (own loopback, own UDP 61234, own iptables)
# and print the PID that holds it, once its loopback is up. Run every engine and Xvfb
# with:  nsenter -t PID -n CMD   (join the network namespace only; joining the user
# namespace fails with EINVAL). Nothing on the host network changes, and another
# match or agent on the box cannot collide with the hard-coded port 61234.
# Stop the namespace by killing the PID.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
marker=$(mktemp)
trap 'rm -f -- "$marker"' EXIT
# The holder must not keep this script's stdout open: NSPID=$(ns_holder.sh) would hang.
unshare -rn bash -c "python3 -I '$here/lo_up.py' >'$marker' && exec sleep infinity" </dev/null >/dev/null 2>&1 &
pid=$!
for _ in $(seq 100); do
    [ -s "$marker" ] && { echo "$pid"; exit 0; }
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.05
done
echo "ns_holder: the namespace did not come up (unshare -rn unavailable?)" >&2
kill "$pid" 2>/dev/null || true
exit 1
