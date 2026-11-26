# headers_install braucht rsync (fehlt im Bootstrap): wie LFS "make headers" und kopieren
package() {
	cd linux-$pkgver
	make headers
	find usr/include -type f ! -name '*.h' -delete
	mkdir -p "$pkgdir/usr"
	cp -r usr/include "$pkgdir/usr/"
}
