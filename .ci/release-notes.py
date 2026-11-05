#!/usr/bin/python3
"""Print the CHANGELOG.md section for a version, failing if there is none."""

import re
import sys

version = sys.argv[1].removeprefix('v')
with open('CHANGELOG.md', encoding='utf-8') as f:
    text = f.read()

match = re.search(rf'^## \[{re.escape(version)}\][^\n]*\n(.*?)(?=^## |\Z)',
                  text, re.M | re.S)
if not match or not match.group(1).strip():
    sys.exit(f'CHANGELOG.md has no notes for version {version}')
print(match.group(1).strip())
