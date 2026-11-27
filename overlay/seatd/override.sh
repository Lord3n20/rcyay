# ohne logind (systemd) und ohne Handbuchseiten; seatd-Server für runit/s6/BusyBox-init (Dienst: overlay/dienste)
usrside_ohne systemd scdoc
build() {
	meson setup --prefix=/usr --buildtype=plain -Dexamples=disabled -Dserver=enabled \
		-Dlibseat-logind=disabled -Dlibseat-seatd=enabled -Dlibseat-builtin=enabled -Dman-pages=disabled \
		"seatd-$pkgver" build
	meson compile -C build
}
package() {
	meson install -C build --destdir "$pkgdir"
	install -Dm644 "seatd-$pkgver/LICENSE" -t "$pkgdir/usr/share/licenses/$pkgname/"
	install -Dm644 sysuser.conf "$pkgdir/usr/lib/sysusers.d/seatd.conf"
}
