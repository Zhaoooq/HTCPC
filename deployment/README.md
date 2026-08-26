# HTCPC boot deployment

This directory contains the Raspberry Pi boot splash and desktop autostart files.

- `plymouth/cpc-gxy/`: black Plymouth screen with the supplied white GXY logo.
- `autostart/htcpc.desktop`: launches `/home/pi/Desktop/HTCPC/HTCPC` after desktop autologin.
- `session/`: user session startup files that omit LXPanel and wf-panel-pi.
- `eeprom/boot.conf`: disables the Raspberry Pi 5 network-install and HDMI diagnostic screen.
- `disable-system-panels.sh`: removes panel launch lines from the system LXDE/labwc sessions.
- `../start-htcpc.sh`: launcher used by the desktop autostart entry.
- `install.sh` also disables the duplicate `wayvnc` service (RealVNC remains enabled)
  and removes the unnecessary `NetworkManager-wait-online` boot delay.

The target machine uses LightDM desktop autologin for user `pi`. The panel is configured
separately in the user's `wf-panel-pi.ini` and LXPanel profile so either desktop backend
automatically hides the top panel.
