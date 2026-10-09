#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(dirname -- "$script_dir")
build_dir=${HTCPC_BUILD_DIR:-"$project_dir/build"}
build_jobs=${HTCPC_BUILD_JOBS:-2}

mkdir -p "$build_dir"
cd "$build_dir"

qmake "$project_dir/HTCPC.pro"
make -j"$build_jobs"

# Keep the path used by start-htcpc.sh stable. Rename only after a complete build.
install -m 0755 "$build_dir/HTCPC" "$project_dir/HTCPC.next"
mv -f "$project_dir/HTCPC.next" "$project_dir/HTCPC"

printf 'HTCPC build complete: %s\n' "$project_dir/HTCPC"
