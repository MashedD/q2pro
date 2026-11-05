#!/usr/bin/python3
"""Print the version of this source tree.

The order of preference is:

1. A VERSION file, which source archives contain instead of git history.
2. The nearest vMAJOR.MINOR.PATCH tag: "v1.2.0" for a tagged commit, or
   "v1.2.0-5-g1a2b3c4d" for five commits after it.
3. "r<commit count>~<hash>" when no version tag is reachable.
"""

import subprocess


def git(*args):
    return subprocess.run(['git', *args], capture_output=True,
                          encoding='utf-8', check=True).stdout.strip()


try:
    with open('VERSION', encoding='utf-8') as f:
        print(f.readline().strip())
except OSError:
    try:
        print(git('describe', '--tags', '--abbrev=8', '--match', 'v[0-9]*'))
    except (OSError, subprocess.CalledProcessError):
        try:
            print(f"r{git('rev-list', '--count', 'HEAD')}~"
                  f"{git('rev-parse', '--short=8', 'HEAD')}")
        except (OSError, subprocess.CalledProcessError):
            print('r666~unknown')
