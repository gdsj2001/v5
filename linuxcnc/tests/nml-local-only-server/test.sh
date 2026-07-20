#!/bin/sh
set -eu

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp_dir="${TMPDIR:-/tmp}/nml-local-only-server.$$"
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM
mkdir -p "$tmp_dir"

"${CXX:-c++}" -std=c++98 -Wall -Wextra -Werror \
    "$test_dir/nml_local_only_lifecycle_smoke.cc" \
    -o "$tmp_dir/nml_local_only_lifecycle_smoke"
"$tmp_dir/nml_local_only_lifecycle_smoke"
printf '%s\n' NML_LOCAL_ONLY_LIFECYCLE_OK
