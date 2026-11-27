# Release-Tarball, Bau mit dem mitgelieferten Makefile statt cmake
usrside_tarball https://github.com/JuliaStrings/utf8proc/archive/refs/tags/v$pkgver.tar.gz \
	f564011d38b2888d583d510b08e69ffa15aa117155db1b9b49ef1dfe1fa25111 utf8proc utf8proc-$pkgver
usrside_ohne cmake git
build() {
	make -C utf8proc libutf8proc.so.$_sover
}
package() {
	cd utf8proc
	install -Dm644 utf8proc.h "$pkgdir/usr/include/utf8proc.h"
	install -Dm644 LICENSE.md "$pkgdir/usr/share/licenses/$pkgname/LICENSE.md"
	install -Dm644 "$srcdir/$pkgname.pc" "$pkgdir/usr/lib/pkgconfig/$pkgname.pc"
	install -Dm755 libutf8proc.so.$_sover "$pkgdir/usr/lib/$pkgname.so.$_sover"
	ln -s $pkgname.so.$_sover "$pkgdir/usr/lib/$pkgname.so"
	ln -s $pkgname.so.$_sover "$pkgdir/usr/lib/$pkgname.so.${_sover%%.*}"
}
