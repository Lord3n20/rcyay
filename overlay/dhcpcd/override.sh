# ohne udev/systemd und ohne privsep-Nutzer
usrside_ohne systemd-libs
build() {
	cd "$pkgname-$pkgver"
	./configure --prefix=/usr --sbindir=/usr/bin --sysconfdir=/etc --dbdir=/var/lib/dhcpcd \
		--libexecdir=/usr/lib/dhcpcd --rundir=/run/dhcpcd --without-udev --disable-privsep
	make
}
package() {
	cd "$pkgname-$pkgver"
	make DESTDIR="$pkgdir" install
	install -Dm644 LICENSE -t "$pkgdir/usr/share/licenses/$pkgname"
}
