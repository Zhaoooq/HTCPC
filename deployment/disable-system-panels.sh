#!/bin/sh

set -eu

LX_AUTOSTART="/etc/xdg/lxsession/LXDE-pi/autostart"
LABWC_AUTOSTART="/etc/xdg/labwc/autostart"

backup_once() {
    file="$1"
    if [ -f "$file" ] && [ ! -f "${file}.cpc-backup" ]; then
        cp -a "$file" "${file}.cpc-backup"
    fi
}

if [ -f "$LX_AUTOSTART" ]; then
    backup_once "$LX_AUTOSTART"
    sed -i '/^@lxpanel[[:space:]]/d' "$LX_AUTOSTART"
fi

if [ -f "$LABWC_AUTOSTART" ]; then
    backup_once "$LABWC_AUTOSTART"
    sed -i '\|/usr/bin/wf-panel-pi|d' "$LABWC_AUTOSTART"
fi
