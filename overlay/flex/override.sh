# Release-Tarball hat configure → kein autoreconf; ohne help2man (die mitgelieferte flex.1 bleibt)
usrside_ohne help2man git
prepare() { :; }
build() {
	cd "$pkgname-$pkgver"
	./configure --prefix=/usr
	sed -i -e 's/ -shared / -Wl,-O1,--as-needed\0/g' libtool
	make HELP2MAN=true
}
check() { :; }
package() {
	cd "$pkgname-$pkgver"
	make HELP2MAN=true DESTDIR="$pkgdir" install
	ln -s flex "$pkgdir/usr/bin/lex"
	install -Dm644 COPYING "$pkgdir/usr/share/licenses/$pkgname/license.txt"
}
