#!/bin/sh

set -eu

THEME_NAME="cpc-gxy"
THEME_SOURCE="/home/pi/Desktop/CPC_1/deployment/plymouth/${THEME_NAME}"
THEME_TARGET="/usr/share/plymouth/themes/${THEME_NAME}"
AUTOSTART_SOURCE="/home/pi/Desktop/CPC_1/deployment/autostart/cpc-1.desktop"
AUTOSTART_TARGET="/home/pi/.config/autostart/cpc-1.desktop"
LX_SESSION_SOURCE="/home/pi/Desktop/CPC_1/deployment/session/LXDE-pi-autostart"
LX_SESSION_TARGET="/home/pi/.config/lxsession/LXDE-pi/autostart"
LABWC_SESSION_SOURCE="/home/pi/Desktop/CPC_1/deployment/session/labwc-autostart"
LABWC_SESSION_TARGET="/home/pi/.config/labwc/autostart"
WF_PANEL_CONFIG="/home/pi/.config/wf-panel-pi.ini"
LX_PANEL_CONFIG="/home/pi/.config/lxpanel/LXDE-pi/panels/panel"
CMDLINE_FILE="/boot/firmware/cmdline.txt"
CONFIG_FILE="/boot/firmware/config.txt"

backup_once() {
    file="$1"
    if [ -f "$file" ] && [ ! -f "${file}.cpc-backup" ]; then
        cp -a "$file" "${file}.cpc-backup"
    fi
}

install -d -m 0755 "$THEME_TARGET"
install -m 0644 "$THEME_SOURCE/cpc-gxy.plymouth" "$THEME_TARGET/cpc-gxy.plymouth"
install -m 0644 "$THEME_SOURCE/cpc-gxy.script" "$THEME_TARGET/cpc-gxy.script"
install -m 0644 "$THEME_SOURCE/gxylogo.png" "$THEME_TARGET/gxylogo.png"

install -d -o pi -g pi -m 0755 /home/pi/.config/autostart
install -o pi -g pi -m 0644 "$AUTOSTART_SOURCE" "$AUTOSTART_TARGET"
install -d -o pi -g pi -m 0755 /home/pi/.config/lxsession/LXDE-pi
install -o pi -g pi -m 0644 "$LX_SESSION_SOURCE" "$LX_SESSION_TARGET"
install -d -o pi -g pi -m 0755 /home/pi/.config/labwc
install -o pi -g pi -m 0755 "$LABWC_SESSION_SOURCE" "$LABWC_SESSION_TARGET"
chmod 0755 /home/pi/Desktop/CPC_1/start-cpc-1.sh

# This image has both RealVNC and wayvnc enabled.  wayvnc cannot bind while
# RealVNC owns the desktop session, then its wrapper takes roughly 15 seconds
# to time out during shutdown.  Keep the working RealVNC service and remove
# the duplicate wayvnc boot job.
systemctl disable --now wayvnc.service 2>/dev/null || true

# CPC_1 is a local hardware UI and does not need a fully configured network
# before LightDM starts.  Waiting for network-online currently adds about six
# seconds to every boot.
systemctl disable NetworkManager-wait-online.service 2>/dev/null || true

if [ -f "$WF_PANEL_CONFIG" ]; then
    backup_once "$WF_PANEL_CONFIG"
    if grep -q '^autohide=' "$WF_PANEL_CONFIG"; then
        sed -i 's/^autohide=.*/autohide=true/' "$WF_PANEL_CONFIG"
    else
        sed -i '/^\[panel\]$/a autohide=true' "$WF_PANEL_CONFIG"
    fi
    if grep -q '^autohide_duration=' "$WF_PANEL_CONFIG"; then
        sed -i 's/^autohide_duration=.*/autohide_duration=250/' "$WF_PANEL_CONFIG"
    else
        sed -i '/^autohide=true$/a autohide_duration=250' "$WF_PANEL_CONFIG"
    fi
    chown pi:pi "$WF_PANEL_CONFIG"
fi

# The Raspberry Pi Desktop package can reset Plymouth to its own white splash
# during package upgrades. It is not needed when the CPC theme is installed.
if dpkg-query -W -f='${Status}' rpd-plym-splash 2>/dev/null | grep -q 'install ok installed'; then
    apt-get remove -y rpd-plym-splash
fi

if [ -f "$LX_PANEL_CONFIG" ]; then
    backup_once "$LX_PANEL_CONFIG"
    sed -i 's/^  autohide=.*/  autohide=1/' "$LX_PANEL_CONFIG"
    chown pi:pi "$LX_PANEL_CONFIG"
fi

if [ -f "$CMDLINE_FILE" ]; then
    backup_once "$CMDLINE_FILE"
    for option in quiet splash logo.nologo vt.global_cursor_default=0 loglevel=3; do
        if ! grep -qw "$option" "$CMDLINE_FILE"; then
            sed -i "1 s/$/ $option/" "$CMDLINE_FILE"
        fi
    done
fi

if [ -f "$CONFIG_FILE" ]; then
    backup_once "$CONFIG_FILE"
    if ! grep -q '^disable_splash=1$' "$CONFIG_FILE"; then
        printf '\n# Hide the firmware splash before the CPC Plymouth theme.\ndisable_splash=1\n' >> "$CONFIG_FILE"
    fi
fi

plymouth-set-default-theme -R "$THEME_NAME"

printf 'Installed Plymouth theme: %s\n' "$(plymouth-set-default-theme)"
printf 'Installed autostart entry: %s\n' "$AUTOSTART_TARGET"
printf 'Disabled desktop panels in LXDE and labwc user sessions.\n'
printf 'Reboot is required to verify the complete boot sequence.\n'
