# Contributing

Bug reports and pull requests are welcome. Use the
[issue templates](https://github.com/MashedD/q2pro/issues/new/choose) to report a
problem or suggest a feature.

To contribute code:

1. Read [AGENTS.md](AGENTS.md), which has the rules for branches, commit messages
   and code. They apply to people as well as AI agents.
2. Create a topic branch from `master`, and keep it to one topic.
3. Build with warnings as errors, as described in [BUILDING.md](BUILDING.md), and
   run the self-tests:

   ```sh
   meson setup builddir -Dtests=true -Dwerror=true
   meson compile -C builddir
   meson test -C builddir
   ```

   `-Dtests=true` adds testing and crash commands to the engine, so never use it
   for builds you give to players.

4. Open a pull request against `master` and wait for CI to pass.

Add a line to the `Unreleased` section of [CHANGELOG.md](CHANGELOG.md) for anything
users will notice.

By contributing, you agree that your work is licensed under the
[GNU General Public License, version 2](LICENSE).
