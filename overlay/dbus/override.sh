# Release-Tarball; ohne systemd/audit/libcap-ng, ohne Doku; ein Paket (keine systemd-Units);
# Nutzer dbus (81) steht in filesystem (kein sysusers auf musl)
usrside_tarball https://dbus.freedesktop.org/releases/dbus/dbus-$pkgver.tar.xz \
	d08b05526c2cb7966a5fb79210d6112b1aca3d135f315c1b302e14b4b0297dac dbus dbus-$pkgver
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne audit libcap-ng docbook-xsl doxygen git mallard-ducktype qt5-tools xmlto yelp-tools glib2
usrside_nur dbus
prepare() {
	cd dbus
	# entspricht 0001-Arch-Linux-tweaks.patch (ohne den Lock-Teil, der erst nach 1.16.4 kommt)
	sed -i "/^subdir('legacy-config')/d; /runstatedir \/ 'dbus',/d" bus/meson.build
	sed -i 's/@DBUS_USER@ - "System Message Bus"/@DBUS_USER@ 81 "System Message Bus"/' bus/sysusers.d/dbus.conf.in
}
build() {
	arch-meson dbus build -D apparmor=disabled -D dbus_user=dbus -D kqueue=disabled -D launchd=disabled \
		-D relocation=disabled -D selinux=disabled -D x11_autolaunch=disabled -D systemd=disabled \
		-D libaudit=disabled -D doxygen_docs=disabled -D ducktype_docs=disabled -D qt_help=disabled \
		-D xml_docs=disabled -D modular_tests=disabled -D installed_tests=false
	meson compile -C build
}
package_dbus() {
	depends=(expat libexpat.so)
	provides=(libdbus libdbus-1.so)
	conflicts=(libdbus)
	replaces=(libdbus)
	meson install -C build --destdir "$pkgdir"
	rm -rf "$pkgdir/usr/share/doc" "$pkgdir/usr/lib/systemd"
	install -Dt "$pkgdir/usr/share/libalpm/hooks" -m644 ./*.hook
	install -Dt "$pkgdir/usr/share/licenses/$pkgname" -m644 dbus/COPYING dbus/LICENSES/AFL-2.1.txt
}
