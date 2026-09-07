#!/usr/bin/env python3
"""Make the pinned FSR2 Vulkan backend compile with 32-bit MinGW headers.

Vulkan represents non-dispatchable handles as integers on 32-bit targets,
while the upstream backend uses nullptr for those handles. VK_NULL_HANDLE is
the portable spelling for both representations and preserves the source API.
"""

import pathlib
import sys


source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
source = source.replace("nullptr", "VK_NULL_HANDLE")
pathlib.Path(sys.argv[2]).write_text(source, encoding="utf-8")
