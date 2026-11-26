# usrside-udev: tmpfiles.d anwenden (wird von rc.start mit sh eingelesen; nur ohne systemd als PID 1)
if [ ! -d /run/systemd/system ] && [ -x /usr/bin/systemd-tmpfiles ]; then
	/usr/bin/systemd-tmpfiles --create --boot -E >/dev/null 2>&1 || :
fi
