# GitLab-Archiv des Tags; nur die Grundfunktionen (ALSA, Pulse-Ersatz, dbus, libudev-zero), alles andere
# (bluez, jack, gstreamer, libcamera, sndfile/pw-cat, avahi, ffado, roc, onnx, webrtc, x11, v4l2) aus;
# ein Paket statt der Arch-Teilung (provides deckt libpipewire/pipewire-audio/-pulse/-alsa/alsa-card-profiles)
usrside_tarball https://gitlab.freedesktop.org/pipewire/pipewire/-/archive/$pkgver/pipewire-$pkgver.tar.bz2 \
	0b32fbbece4591376b45dde5a818bfa5b3517e70913a2f7414e33c47c20b07ee pipewire pipewire-$pkgver
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne avahi bluez-libs dconf doxygen fftw git glib2-devel graphviz gst-plugins-base jack2 libcamera \
	libcanberra libebur128 libfdk-aac libffado libfreeaptx liblc3 libldac libmysofa libpulse libsndfile libusb \
	libx11 libxfixes lilv onnxruntime opus python-docutils roc-toolkit rtkit sbc sdl2 spandsp valgrind \
	webrtc-audio-processing-1
usrside_nur pipewire
prepare() { :; }
build() {
	arch-meson pipewire build -D auto_features=disabled \
		-D docs=disabled -D man=disabled -D examples=disabled -D tests=disabled -D installed_tests=disabled \
		-D spa-plugins=enabled -D alsa=enabled -D audiomixer=enabled -D audioconvert=enabled \
		-D control=enabled -D support=enabled -D volume=enabled -D audiotestsrc=enabled -D videoconvert=enabled \
		-D pipewire-alsa=enabled -D dbus=enabled -D udev=enabled -D readline=enabled \
		-D flatpak=disabled -D libsystemd=disabled -D logind=disabled -D systemd-system-service=disabled \
		-D systemd-user-service=disabled -D selinux=disabled -D snap=disabled -D rlimits-install=false \
		-D session-managers=[] -D udevrulesdir=/usr/lib/udev/rules.d
	meson compile -C build
}
package_pipewire() {
	license+=(LGPL-2.1-or-later)
	depends=(alsa-lib libasound.so dbus libdbus-1.so libudev-zero ncurses readline)
	optdepends=('pipewire-session-manager: Session-Manager (wireplumber)')
	provides=(libpipewire libpipewire-0.3.so pipewire-audio pipewire-pulse pipewire-alsa alsa-card-profiles
		pulse-native-provider)
	conflicts=(libpipewire pipewire-audio pipewire-pulse pipewire-alsa alsa-card-profiles pulseaudio)
	meson install -C build --destdir "$pkgdir"
	mkdir -p "$pkgdir"/etc/pipewire/{client,client-rt,filter-chain,minimal,pipewire,pipewire-pulse}.conf.d
	mkdir -p "$pkgdir/etc/alsa/conf.d"
	ln -st "$pkgdir/etc/alsa/conf.d" /usr/share/alsa/alsa.conf.d/{50-pipewire,99-pipewire-default}.conf
	install -Dm644 pipewire/COPYING -t "$pkgdir/usr/share/licenses/$pkgname"
}
