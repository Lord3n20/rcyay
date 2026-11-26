# Debian-Release-Tarball (1.37.1.1) statt git-Archiv; ohne po4a-Handbücher und capset-Hülle (musl)
pkgver=1.37.1.1
usrside_tarball https://deb.debian.org/debian/pool/main/f/fakeroot/fakeroot_1.37.1.1.orig.tar.gz \
	86b0b75bf319ca42e525c098675b6ed10a06b76e69ec9ccf20ef5e03883b3a14 fakeroot-archive fakeroot-$pkgver
usrside_ohne util-linux po4a systemd
usrside_patchordner=fakeroot-$pkgver
prepare() { :; }
build() {
	cd fakeroot-archive
	./configure --prefix=/usr --libdir=/usr/lib/libfakeroot --disable-static --with-ipc=sysv \
		ac_cv_func_capset=no
	make
}
package() {
	cd fakeroot-archive
	make DESTDIR="$pkgdir" install
	install -Dm644 COPYING -t "$pkgdir/usr/share/licenses/$pkgname"
}
