#!/usr/bin/env python3
"""Install regressions only in a disposable Pintos project copy."""
import argparse
import shutil
from pathlib import Path

NAMES = ['interrupt-sema', 'ready-donation', 'base-raise', 'same-lock-cleanup',
         'cond-live', 'cond-before-down', 'alarm-preempt', 'lock-handoff', 'lock-retry']
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('project_copy', type=Path)
args = parser.parse_args()
root = args.project_copy.resolve() / 'pintos'
if not (root / 'threads/thread.c').is_file():
    raise SystemExit('Expected a disposable project root containing pintos/')
if (root / 'tests/threads/priority-regressions.c').exists():
    raise SystemExit('Regressions already installed')
shutil.copyfile(Path(__file__).with_name('priority-regressions.c'), root / 'tests/threads/priority-regressions.c')
header = root / 'tests/threads/tests.h'
header.write_text(header.read_text().replace('void msg (', ''.join('extern test_func test_regression_' + n.replace('-', '_') + ';\n' for n in NAMES) + '\nvoid msg ('))
registry = root / 'tests/threads/tests.c'
registry.write_text(registry.read_text().replace('    {"alarm-single",', ''.join('    {"regression-' + n + '", test_regression_' + n.replace('-', '_') + '},\n' for n in NAMES) + '    {"alarm-single",'))
makefile = root / 'tests/threads/Make.tests'
with makefile.open('a') as f:
    f.write('\ntests/threads_SRC += tests/threads/priority-regressions.c\n')
    f.write('tests/threads_TESTS += ' + ' '.join('tests/threads/regression-' + n for n in NAMES) + '\n')
for name in NAMES:
    test = 'regression-' + name
    (root / 'tests/threads' / (test + '.ck')).write_text(
        'use strict;\nuse warnings;\nuse tests::tests;\ncheck_expected ([<<\'EXPECTED\']);\n'
        f'({test}) begin\n({test}) PASS\n({test}) end\nEXPECTED\npass;\n')
timer = root / 'devices/timer.c'
text = timer.read_text()
text = text.replace('static int64_t ticks;', 'static int64_t ticks;\nextern void (*priority_regression_tick_hook)(void);')
text = text.replace('    ticks++;', '    ticks++;\n    if (priority_regression_tick_hook != NULL)\n        priority_regression_tick_hook();')
timer.write_text(text)
print(' '.join('tests/threads/regression-' + n for n in NAMES))
