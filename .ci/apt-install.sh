#!/bin/sh
# Install Ubuntu packages on a GitHub Actions runner.
#
# Usage: .ci/apt-install.sh PACKAGE...
#
# The runners' default mirror, azure.archive.ubuntu.com, sometimes serves
# files at a few dozen KB/s, which made jobs hit their time limit while
# downloading the compiler. Each download attempt is therefore limited to
# three minutes. apt keeps partly downloaded files, so a new attempt resumes
# where the last one stopped. Only the download is limited: stopping apt
# while it unpacks leaves dpkg half-configured, so the packages are installed
# from the cache in a separate step with no limit of its own. From the second attempt on, the main Ubuntu archive
# is used instead of the Azure mirror. The runners select their mirror
# through /etc/apt/apt-mirrors.txt (a "mirror+file:" source), so that file
# is what gets replaced.

set -eu

[ $# -gt 0 ] || { echo "usage: $0 PACKAGE..." >&2; exit 2; }

opts="-o Acquire::Retries=3 -o Acquire::http::Timeout=30"
attempts=3
limit=180

for attempt in $(seq 1 $attempts); do
    if [ "$attempt" -gt 1 ]; then
        echo "::warning::apt attempt $((attempt - 1)) failed or took over ${limit}s; retrying with archive.ubuntu.com"
        # The runners' sources point at a mirror list file rather than a
        # host, so replace the list; also rewrite any direct references.
        if [ -f /etc/apt/apt-mirrors.txt ]; then
            echo "http://archive.ubuntu.com/ubuntu/" |
                sudo tee /etc/apt/apt-mirrors.txt >/dev/null
        fi
        sudo sed -i 's|azure\.archive\.ubuntu\.com|archive.ubuntu.com|g' \
            /etc/apt/sources.list /etc/apt/sources.list.d/*.sources 2>/dev/null || :
    fi
    # shellcheck disable=SC2086 # $opts holds several words on purpose
    if sudo timeout "$limit" apt-get $opts update &&
       sudo timeout "$limit" apt-get $opts install -y --no-install-recommends \
           --download-only "$@"; then
        downloaded=1
        break
    fi
done

if [ -z "${downloaded:-}" ]; then
    echo "::error::could not download packages after $attempts attempts" >&2
    exit 1
fi

# Everything is in the cache now, so this only unpacks and configures.
# shellcheck disable=SC2086
sudo apt-get $opts install -y --no-install-recommends "$@"
