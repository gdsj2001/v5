#!/bin/sh
set -eu

AUTHORIZED_KEYS=/opt/8ax-auth/storage/remote-ssh/authorized_keys

[ "$#" -eq 1 ] || exit 1
[ "$1" = "8ax-tunnel" ] || exit 1
[ -r "$AUTHORIZED_KEYS" ] || exit 0
exec /bin/cat -- "$AUTHORIZED_KEYS"
