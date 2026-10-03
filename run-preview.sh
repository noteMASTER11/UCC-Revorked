#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cmake -S "$project_root" -B "$project_root/build-preview" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TRAY=OFF -DBUILD_CLI=OFF \
  -DBUILD_GNOME=OFF -DBUILD_TESTS=ON -DUCC_READ_ONLY_PREVIEW=ON
cmake --build "$project_root/build-preview" --target ucc-gui -j2
exec "$project_root/build-preview/bin/ucc-gui" "$@"
