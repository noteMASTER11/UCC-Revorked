#!/usr/bin/env bash
set -euo pipefail
# dbus-run-session supplies a private bus; never contact the running daemon.
export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
export GIO_USE_VFS=local
# GJS on some distributions misdecodes non-ASCII module filenames. Keep the
# unmodified test and its relative import together under an ASCII temp path.
fixture_root=$(mktemp -d /tmp/ucc-gjs-test.XXXXXX)
trap 'rm -rf "$fixture_root"' EXIT
mkdir -p "$fixture_root/tests" "$fixture_root/ucc-gnome"
cp "$(dirname "$0")/test_gnome_async.js" "$fixture_root/tests/"
cp "$(dirname "$0")/../ucc-gnome/uccdClient.js" "$fixture_root/ucc-gnome/"
"$1" -m "$fixture_root/tests/test_gnome_async.js"
