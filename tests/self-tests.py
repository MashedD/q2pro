#!/usr/bin/env python3
"""Run the engine's built-in self-checks and fail if any of them fails.

Builds configured with -Dtests=true add console commands that check string,
path and checksum helpers against tables of expected results (see
src/common/tests.c). Each prints "N failures, M <things> tested", but the
engine exits with status 0 either way, so this script starts the dedicated
server, runs every self-checking command and inspects the output.

Usage: self-tests.py PATH-TO-Q2PRODED
"""

import re
import subprocess
import sys
import tempfile

# Commands that compare results against expected values. Other test commands
# need game data (bsptest, modeltest), only print information (nextpathtest)
# or deliberately crash (crash, doublefree), so they are not run here.
CHECKS = [
    "wildtest",
    "normtest",
    "infotest",
    "snprintftest",
    "mdfourtest",
    "extcmptest",
]

SUMMARY = re.compile(r"^(?:\d+ msec, )?(\d+) failures?, \d+ \w+ tested", re.M)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip().splitlines()[-1])

    with tempfile.TemporaryDirectory() as tmp:
        # An empty, private game directory keeps the user's configuration out
        # of the test. Port 0 on the loopback address lets the system pick a
        # free port, so a running server or a parallel test can't collide.
        args = [sys.argv[1],
                "+set", "basedir", tmp, "+set", "homedir", tmp,
                "+set", "net_ip", "127.0.0.1", "+set", "net_ip6", "",
                "+set", "net_port", "0"]
        for check in CHECKS:
            args += ["+" + check]
        args += ["+quit"]

        proc = subprocess.run(args, stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace", timeout=120)

    output = proc.stdout
    sys.stdout.write(output)

    problems = []
    if proc.returncode != 0:
        problems.append(f"q2proded exited with status {proc.returncode}")
    if "Unknown command" in output:
        problems.append("a test command is missing; was the build "
                        "configured with -Dtests=true?")

    results = [int(n) for n in SUMMARY.findall(output)]
    if len(results) != len(CHECKS):
        problems.append(f"expected {len(CHECKS)} result lines, "
                        f"found {len(results)}")
    failures = sum(results)
    if failures:
        problems.append(f"{failures} check(s) failed")

    if problems:
        for problem in problems:
            print("self-tests: " + problem, file=sys.stderr)
        sys.exit(1)
    print(f"self-tests: {len(CHECKS)} commands passed")


if __name__ == "__main__":
    main()
