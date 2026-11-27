# usrside-udev: tmpfiles.d anwenden (wird von rc.start mit sh eingelesen; nur ohne systemd als PID 1)
if [ ! -d /run/systemd/system ]; then
	# devtmpfs hat keine /dev/fd-Links (bash <(…) in makepkg braucht sie); Notbehelf bis rc.start sie anlegt
	[ -e /dev/fd ] || ln -s /proc/self/fd /dev/fd
	for i in 0:stdin 1:stdout 2:stderr; do [ -e "/dev/${i#*:}" ] || ln -s "/proc/self/fd/${i%:*}" "/dev/${i#*:}"; done
	[ -x /usr/bin/systemd-tmpfiles ] && /usr/bin/systemd-tmpfiles --create --boot --exclude-prefix=/dev --exclude-prefix=/proc --exclude-prefix=/sys --exclude-prefix=/run/nologin >/dev/null 2>&1   # nicht -E: das schließt auch /run aus (/run/utmp fehlte, Befund e1e)
	# /run/nologin (systemd-nologin.conf) entfernt sonst systemd-user-sessions; ohne systemd sperrte es jeden Nicht-root-Login (pam_nologin)
	rm -f /run/nologin
	:
fi
