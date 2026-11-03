"""Which source revision a local build reports (tools/source-revision.sh).

The agent interface writes it into session.json, and live-test receipts quote
it, so it must name the commit of the tree that was built or say "unknown".
Cases that need git are skipped where git is missing (MSYS2 without git).
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools' / 'source-revision.sh'
BASH = shutil.which('bash')
GIT = shutil.which('git')


@unittest.skipUnless(BASH, 'bash is not installed')
class SourceRevisionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='openneoua-revision-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.env = {key: value for key, value in os.environ.items()
                    if not key.startswith('GIT_') and key != 'GITHUB_SHA'}
        # Repository discovery must not leave the temporary directory.
        self.env['GIT_CEILING_DIRECTORIES'] = str(self.root)

    def revision(self, *tree, **extra_env):
        command = [BASH, SCRIPT.as_posix()] + [path.as_posix() for path in tree]
        result = subprocess.run(command, env=dict(self.env, **extra_env),
                                capture_output=True, text=True, check=True)
        return result.stdout.strip()

    def git(self, cwd, *args):
        return subprocess.run(
            [GIT, '-c', 'user.name=Revision Test', '-c', 'user.email=revision@example.invalid',
             '-c', 'commit.gpgsign=false', '-c', 'init.defaultBranch=main', *args],
            cwd=cwd, env=self.env, capture_output=True, text=True, check=True).stdout.strip()

    def commit_tree(self, path):
        path.mkdir(parents=True)
        (path / 'file.txt').write_text('one\n')
        self.git(path, 'init', '-q')
        self.git(path, 'add', 'file.txt')
        self.git(path, 'commit', '-q', '-m', 'one')
        return self.git(path, 'rev-parse', '--short=12', 'HEAD')

    def test_ci_names_its_commit(self):
        self.assertEqual(self.revision(self.root, GITHUB_SHA='0123456789abcdef'), '0123456789abcdef')

    def test_tree_outside_any_repository_is_unknown(self):
        tree = self.root / 'export'
        tree.mkdir()
        self.assertEqual(self.revision(tree), 'unknown')

    def test_default_tree_is_the_one_holding_the_script(self):
        self.assertEqual(self.revision(), self.revision(ROOT))

    @unittest.skipUnless(GIT, 'git is not installed')
    def test_checkout_names_its_commit_and_changed_tracked_files(self):
        tree = self.root / 'checkout'
        head = self.commit_tree(tree)
        self.assertEqual(len(head), 12)
        self.assertEqual(self.revision(tree), head)
        (tree / 'untracked.txt').write_text('new\n')
        self.assertEqual(self.revision(tree), head)
        (tree / 'file.txt').write_text('two\n')
        self.assertEqual(self.revision(tree), head + '-dirty')

    @unittest.skipUnless(GIT, 'git is not installed')
    def test_tree_inside_another_repository_is_unknown(self):
        # A build lab under version control that holds an exported tree.
        lab = self.root / 'lab'
        self.commit_tree(lab)
        tree = lab / 'OpenNeoUA-src'
        tree.mkdir()
        (tree / 'README.md').write_text('exported, not checked out\n')
        self.assertEqual(self.revision(tree), 'unknown')
        self.git(lab, 'add', '.')
        self.git(lab, 'commit', '-q', '-m', 'keep a copy of the sources')
        self.assertEqual(self.revision(tree), 'unknown')

    @unittest.skipUnless(GIT, 'git is not installed')
    def test_git_environment_of_a_hook_is_ignored(self):
        lab = self.root / 'lab'
        lab_head = self.commit_tree(lab)
        tree = self.root / 'export'
        tree.mkdir()
        self.assertNotEqual(lab_head, 'unknown')
        self.assertEqual(self.revision(tree, GIT_DIR=(lab / '.git').as_posix(),
                                       GIT_WORK_TREE=tree.as_posix()), 'unknown')


if __name__ == '__main__':
    unittest.main()
