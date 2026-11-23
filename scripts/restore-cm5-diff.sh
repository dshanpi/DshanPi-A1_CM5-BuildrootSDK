#!/bin/sh
set -eu
exec python3 "$(dirname "$0")/board-config.py" restore "$@"
