#!/bin/sh -eu
# Create the source archive in the given output directory. Run from the
# repository root with full git history, so that the version can be determined.

out=$1
ver=$(python3 version.py)
src=q2pro-$ver
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

git archive --format=tar --prefix="$src/" HEAD | tar -x -C "$tmp"
echo "$ver" > "$tmp/$src/VERSION"
rm -rf "$tmp/$src/.gitignore" "$tmp/$src/.ci" "$tmp/$src/.github"

mkdir -p "$out"
fakeroot tar -czf "$out/$src-source.tar.gz" -C "$tmp" "$src"
