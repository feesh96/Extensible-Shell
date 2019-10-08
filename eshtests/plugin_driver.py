#!/usr/bin/env python
import argparse
import glob
import os
import signal
import sys
from os.path import relpath, join
from subprocess import call

signal.signal(signal.SIGINT, lambda s,f: sys.exit(0))

parser = argparse.ArgumentParser(
        description='Runs all plugins in the student-plugins folder'
    )
parser.add_argument('--plugin_dir',
        help='student plugin directory',
        default='/web/courses/cs3214/projects/student-plugins',
    )
args = parser.parse_args()

dev_null = open(os.devnull, 'w')
plugin_dirs = glob.glob(join(args.plugin_dir, '*/*'))
passed_tests = []
failed_tests = []

for p in plugin_dirs:
    rp = relpath(p)
    test_files = glob.glob(join(p, '*test.py'))
    test_name = relpath(p, args.plugin_dir)
    for t in test_files:
        exit_code = call(
            ['python', t, 'eshoutput.py', ' -p {}'.format(rp) ],
            stdout=dev_null,
            stderr=dev_null,
        )
        if exit_code == 0:
            print('PASSED: {}'.format(test_name))
            passed_tests.append(t)
        else:
            print('FAILED: {}'.format(test_name))
            failed_tests.append(t)

passed = len(passed_tests)
failed = len(failed_tests)
print('PASSED {} plugins'.format(passed))
print('FAILED {} plugins'.format(failed))
print('OVERALL: {}/{}'.format(passed, passed + failed))