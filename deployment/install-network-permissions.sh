#!/bin/sh

set -eu

CPC_USER="${1:-pi}"
POLKIT_SOURCE="/home/pi/Desktop/HTCPC/deployment/polkit/49-cpc-network.rules"
POLKIT_TARGET="/etc/polkit-1/rules.d/49-cpc-network.rules"
NETWORK_GROUP="cpc-network"

if [ "$(id -u)" -ne 0 ]; then
    printf 'Error: run this installer with sudo.\n' >&2
    exit 1
fi

if ! id "$CPC_USER" >/dev/null 2>&1; then
    printf 'Error: CPC user does not exist: %s\n' "$CPC_USER" >&2
    exit 1
fi

if [ ! -f "$POLKIT_SOURCE" ]; then
    printf 'Error: PolicyKit rule is missing: %s\n' "$POLKIT_SOURCE" >&2
    exit 1
fi

if ! getent group "$NETWORK_GROUP" >/dev/null 2>&1; then
    groupadd --system "$NETWORK_GROUP"
fi

if ! id -nG "$CPC_USER" | tr ' ' '\n' | grep -qx "$NETWORK_GROUP"; then
    usermod -a -G "$NETWORK_GROUP" "$CPC_USER"
fi

if [ -f "$POLKIT_TARGET" ] && [ ! -f "${POLKIT_TARGET}.cpc-backup" ]; then
    cp -a "$POLKIT_TARGET" "${POLKIT_TARGET}.cpc-backup"
fi

install -d -m 0755 /etc/polkit-1/rules.d
install -o root -g root -m 0644 "$POLKIT_SOURCE" "$POLKIT_TARGET"

printf 'Installed restricted NetworkManager permission for user: %s\n' "$CPC_USER"
printf 'Rule: %s\n' "$POLKIT_TARGET"
printf 'Reboot (or log out and back in) before using CPC network configuration.\n'
