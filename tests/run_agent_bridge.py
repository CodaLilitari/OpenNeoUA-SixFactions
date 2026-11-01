"""Give the real SDL/file bridge test a disposable external build-tree fixture."""
import pathlib
import subprocess
import sys
import tempfile

binary = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve()
if any((p / '.git').exists() for p in (root, *root.parents)):
    raise SystemExit('Agent test fixture must be outside all Git checkouts')
with tempfile.TemporaryDirectory(prefix='agent-bridge-', dir=root) as directory:
    result = subprocess.run([str(binary), directory], timeout=20, capture_output=True)
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    raise SystemExit(result.returncode)
