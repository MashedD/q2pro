# AGENTS.md

Instructions for AI coding agents and human contributors working in this
repository. They take precedence over any `AGENTS.md` in a parent directory.

## Project

A fork of [Q2PRO](https://github.com/q2pro/q2pro), an enhanced Quake 2 client
and server written in C, built with Meson. Licensed under GPL-2.0.

- `src/client`, `src/server`, `src/common`: engine code
- `src/refresh`: renderers
- `src/game`, `src/unix`, `src/windows`: game module and platform code
- `doc`, `man`: user documentation, including every cvar
- `.github/workflows`, `.ci`: continuous integration and release packaging

## Build and test

```sh
meson setup builddir -Dtests=true -Dwerror=true
meson compile -C builddir
meson test -C builddir
```

Keep build directories outside version control. See `BUILDING.md` for
dependencies and cross compilation.

## Branches

`master` is always buildable and is the base for all work.

1. Create one branch per topic from `master`, such as `fix/demo-seek`,
   `feat/menu-scrolling`, `build/windows-curl`, `ci/release` or `docs/readme`.
2. Open a pull request against `MashedD/q2pro`, never upstream `q2pro/q2pro`
   unless a maintainer asks for it.
3. Wait for CI to pass, then merge with a merge commit (`--no-ff`). Do not
   fast-forward or squash when merging, so that `git log --first-parent master`
   reads as a list of topics.

Do not force-push `master`. Do not change or delete `archive/*` branches and
tags, which preserve the fork's history before it was rebuilt.

## Commits

History is documentation: someone reading a commit later should learn why the
change was made, how it works and what it does. Every commit must build on its
own and cover one logical change.

Use a Conventional Commits subject, followed by a body that answers these
questions:

```text
type(scope): short imperative summary

What: the behaviour or code that changes, and what users will notice.

Why: the problem being solved and how it shows up. Include the error message,
symptom or reproduction steps when there are any.

How: how the change works, the reasoning behind non-obvious choices, and
alternatives that were rejected.

Testing: what was actually run and the result, and what was not tested.
```

Types are `fix`, `feat`, `build`, `ci`, `docs`, `refactor`, `test`, `perf` and
`chore`. A tiny change can use shorter sections, but never drop the why. Do not
use subjects such as "Fixes", "Update" or "Step 12". Never claim testing that
did not happen.

## Code

- Follow the style of the surrounding code, and keep changes focused on the topic.
- Keep upstream's default behaviour. Make opinionated behaviour opt-in with a
  cvar or build option.
- Document every new or changed cvar in `doc/client.asciidoc` or
  `doc/server.asciidoc`.
- Treat network messages, downloads and map, model and demo files as untrusted.
- Keep upstream copyright notices and attribution.
- Windows, macOS and FreeBSD may only be build-tested; say so when that applies.

## Releases

Releases are built from `vMAJOR.MINOR.PATCH` tags on `master`. Update
`CHANGELOG.md` first. A maintainer must approve publishing each release.
