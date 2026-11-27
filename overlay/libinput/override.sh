# GitLab-Archiv des Tags; libudev aus libudev-zero (kein udevd nötig), ohne libwacom/Lua-Plugins/
# Debug-Oberfläche/Tests/Doku
usrside_tarball https://gitlab.freedesktop.org/libinput/libinput/-/archive/$pkgver/libinput-$pkgver.tar.bz2 \
	3a6065c07b118c08d0c7b7b85a265ecb050a8cfc91eeb360890d768a51b4cce9 libinput libinput-$pkgver
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne cairo check git glib2 gtk4 libx11 python-libevdev python-pyudev python-yaml wayland \
	wayland-protocols libwacom lua
depends+=(libudev-zero)
prepare() { :; }
build() {
	arch-meson libinput build -D udev-dir=/usr/lib/udev -D libwacom=false -D debug-gui=false -D tests=false \
		-D install-tests=false -D documentation=false -D lua-plugins=disabled -D autoload-plugins=false
	meson compile -C build
}
# ein Paket (Werkzeuge wie „libinput list-devices“ inklusive; die Python-Skripte brauchen python)
usrside_nur libinput
package_libinput() {
	optdepends=('python: libinput-Analysewerkzeuge')
	provides=(libinput.so libinput-tools)
	conflicts=(libinput-tools)
	meson install -C build --destdir "$pkgdir"
	install -Dm644 libinput/COPYING -t "$pkgdir/usr/share/licenses/$pkgname"
}
