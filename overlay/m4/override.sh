# nur der Release-Tarball (Arch baut aus git + gnulib und nimmt aus dem Tarball nur die Übersetzungen)
source=("https://ftp.gnu.org/gnu/m4/m4-$pkgver.tar.xz")
sha256sums=(f25c6ab51548a73a75558742fb031e0625d6485fe5f9155949d6486a2408ab66)
unset b2sums sha512sums md5sums
usrside_ohne git gperf help2man python
prepare() { :; }
build() {
	cd m4-$pkgver
	./configure --prefix=/usr
	make
}
package() {
	cd m4-$pkgver
	make DESTDIR="$pkgdir" install
}
