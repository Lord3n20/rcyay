# GitLab-Archiv des Tags; System-lua (Arch-lua), ohne Introspection/Doku/elogind/systemd; ein Paket
usrside_tarball https://gitlab.freedesktop.org/pipewire/wireplumber/-/archive/$pkgver/wireplumber-$pkgver.tar.bz2 \
	e743070200e3788b8a46b5cd35438170dea4458302cfa3bb04ab4a9495ad14a4 wireplumber wireplumber-$pkgver
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne doxygen git glib2-devel gobject-introspection graphviz python-breathe python-lxml python-sphinx \
	python-sphinx_rtd_theme
usrside_nur wireplumber
prepare() { :; }
build() {
	arch-meson wireplumber build -D elogind=disabled -D systemd=disabled -D system-lua=true \
		-D introspection=disabled -D doc=disabled -D tests=false -D dbus-tests=false \
		-D systemd-system-service=false -D systemd-user-service=false
	meson compile -C build
}
package_wireplumber() {
	depends=(glib2 libg{lib,object,io}-2.0.so libpipewire-0.3.so lua pipewire)
	provides=(pipewire-session-manager libwireplumber "libwireplumber-${pkgver:0:3}.so")
	conflicts=(pipewire-media-session libwireplumber)
	meson install -C build --destdir "$pkgdir"
	rm -rf "$pkgdir/usr/share/doc"
	install -Dt "$pkgdir/usr/share/licenses/$pkgname" -m644 wireplumber/LICENSE
}
