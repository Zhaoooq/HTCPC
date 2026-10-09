#!/bin/sh

set -u

# Wait for the desktop session and runtime directory to become ready.  HTCPC
# controls real hardware, so never allow a second copy to claim the same GPIO,
# PWM and FTDI devices.
sleep 3
cd /home/pi/Desktop/HTCPC || exit 1

runtime_dir="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
state_dir="${XDG_STATE_HOME:-${HOME}/.local/state}/htcpc"
lock_file="${runtime_dir}/htcpc.lock"
log_file="${state_dir}/startup.log"

mkdir -p "${state_dir}"
printf '\n[%s] HTCPC launcher requested\n' "$(date --iso-8601=seconds)" >>"${log_file}"

/usr/bin/flock -n -E 73 "${lock_file}" /home/pi/Desktop/HTCPC/HTCPC >>"${log_file}" 2>&1
status=$?

if [ "${status}" -eq 73 ]; then
    printf '[%s] HTCPC is already running; duplicate launch ignored\n' \
        "$(date --iso-8601=seconds)" >>"${log_file}"
else
    printf '[%s] HTCPC exited with status %s\n' \
        "$(date --iso-8601=seconds)" "${status}" >>"${log_file}"
fi

exit "${status}"
