#!/bin/sh
set -eu
here=$(dirname "$(realpath "$0")")
rsync -a "$here/root/" "$1/"
# The generic RK3576 overlay installs this flag even when fsck is enabled.
[ -n "${RK_DISK_SKIP_FSCK:-}" ] || rm -f "$1/.skip_fsck"
