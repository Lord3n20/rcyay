# usrside-udev: systemd-udevd als Gerätemanager statt mdev (nur ohne systemd als PID 1)
if [ ! -d /run/systemd/system ] && [ -x /usr/lib/systemd/systemd-udevd ]; then
	[ -w /proc/sys/kernel/hotplug ] && echo >/proc/sys/kernel/hotplug
	/usr/lib/systemd/systemd-udevd --daemon
	/usr/bin/udevadm trigger --type=subsystems --action=add
	/usr/bin/udevadm trigger --type=devices --action=add
	/usr/bin/udevadm settle --timeout=30 || :
fi
