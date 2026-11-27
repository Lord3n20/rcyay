# GitHub-Archiv des Tags statt git; ohne Handbuchseiten (mandoc) und ohne -Werror
usrside_tarball https://github.com/rhboot/efivar/archive/refs/tags/$pkgver.tar.gz \
	c9edd15f2eeeea63232f3e669a48e992c7be9aff57ee22672ac31f5eca1609a6 efivar efivar-$pkgver
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne git mandoc grub
build() {
	make ERRORS='' ENABLE_DOCS=0 all -C $pkgname
}
package() {
	make V=1 DESTDIR="$pkgdir/" libdir=/usr/lib/ bindir=/usr/bin/ mandir=/usr/share/man/ \
		includedir=/usr/include/ ENABLE_DOCS=0 install -j1 -C $pkgname
	install -vDm 644 $pkgname/{README.md,TODO} -t "$pkgdir/usr/share/doc/$pkgname/"
}
