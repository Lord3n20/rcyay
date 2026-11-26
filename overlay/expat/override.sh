# Release-Tarball, Bau mit configure statt cmake (cmake ist für M5-Stufe 1 zu schwer: qt6-base, emacs …)
usrside_tarball https://github.com/libexpat/libexpat/releases/download/R_${pkgver//./_}/expat-$pkgver.tar.xz \
	c624ba2998fc65df1163a49641ff3f9ab73138b11641d92c86bee0fb01b1bc7f expat expat-$pkgver
usrside_ohne cmake git
build() {
	cd expat
	./configure --prefix=/usr --disable-static --without-docbook --without-examples --without-tests
	make
}
package() {
	cd expat
	make DESTDIR="$pkgdir" install
	install -Dm644 COPYING -t "$pkgdir/usr/share/licenses/$pkgname/"
}
