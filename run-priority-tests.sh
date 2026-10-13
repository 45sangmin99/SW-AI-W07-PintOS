#!/bin/sh
# Run inside the Linux Pintos development container.
set -eu
merge_project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export PATH="$merge_project_dir/pintos/utils:$PATH"
cd "$merge_project_dir/pintos/threads"
make
make -C build check TESTS="tests/threads/alarm-single tests/threads/alarm-multiple tests/threads/alarm-simultaneous tests/threads/alarm-priority tests/threads/alarm-zero tests/threads/alarm-negative tests/threads/priority-change tests/threads/priority-donate-one tests/threads/priority-donate-multiple tests/threads/priority-donate-multiple2 tests/threads/priority-donate-nest tests/threads/priority-donate-sema tests/threads/priority-donate-lower tests/threads/priority-fifo tests/threads/priority-preempt tests/threads/priority-sema tests/threads/priority-condvar tests/threads/priority-donate-chain"
