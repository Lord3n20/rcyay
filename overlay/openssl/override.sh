# ohne brotli; Plattform linux-x86_64 (CARCH ist x86_64_musl)
usrside_ohne brotli
build() {
	cd "$srcdir/$pkgname-$pkgver"
	./Configure --prefix=/usr --openssldir=/etc/ssl --libdir=lib \
		shared enable-ktls enable-zlib enable-zstd enable-ec_nistp_64_gcc_128 no-docs linux-x86_64
	make
}
package() {
	cd "$srcdir/$pkgname-$pkgver"
	make DESTDIR="$pkgdir" install_sw install_ssldirs
	chmod 700 "$pkgdir/etc/ssl/private"
	install -D -m644 LICENSE.txt "$pkgdir/usr/share/licenses/$pkgname/LICENSE.txt"
}
