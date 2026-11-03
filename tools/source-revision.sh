#!/usr/bin/env bash
# Prints the source revision that the agent interface reports in
# session.json, for the source tree at $1 (by default the tree that holds
# this script). tools/linux/build.sh and tools/windows/build.sh pass it on.
#
# CI exports GITHUB_SHA, which src/CMakeLists.txt also uses as its default.
# A local build names the git commit of the tree itself, marked -dirty when
# tracked files differ from it. A tree that is not the top of a git work tree
# prints "unknown": an exported or copied tree can sit inside some other
# repository, and that repository's commit says nothing about the tree.
set -euo pipefail
source_root=${1:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)}
# A git hook exports these; they would point git at another repository.
unset GIT_DIR GIT_WORK_TREE

if [[ -n ${GITHUB_SHA:-} ]]; then
    printf '%s\n' "$GITHUB_SHA"
# --show-prefix is empty only at the top of a work tree. Comparing paths
# instead would trip over /c/... against C:/... under MSYS2.
elif prefix=$(git -C "$source_root" rev-parse --show-prefix 2>/dev/null) && [[ -z $prefix ]] &&
     revision=$(git -C "$source_root" rev-parse --short=12 HEAD 2>/dev/null); then
    git -C "$source_root" diff --quiet HEAD -- 2>/dev/null || revision+=-dirty
    printf '%s\n' "$revision"
else
    printf 'unknown\n'
fi
