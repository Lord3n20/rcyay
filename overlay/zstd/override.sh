# make statt cmake (cmake fehlt im Bootstrap), ohne lz4
usrside_ohne lz4
build() {
	cd zstd-$pkgver
	make -C lib PREFIX=/usr libzstd
	make -C programs PREFIX=/usr HAVE_LZ4=0 zstd
}
package() {
	cd zstd-$pkgver
	make -C lib PREFIX=/usr DESTDIR="$pkgdir" install-shared install-includes install-pc
	make -C programs PREFIX=/usr DESTDIR="$pkgdir" HAVE_LZ4=0 install
	install -Dm644 LICENSE -t "$pkgdir/usr/share/licenses/$pkgname"
}
