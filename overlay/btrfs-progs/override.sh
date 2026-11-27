# Release-Tarball (mit configure, kein autogen.sh); Krypto eingebaut statt libgcrypt, ohne Doku (sphinx),
# btrfs-convert (e2fsprogs), Python-Bindung und backtrace (execinfo.h fehlt in musl); ohne initcpio/systemd
usrside_tarball https://www.kernel.org/pub/linux/kernel/people/kdave/btrfs-progs/btrfs-progs-v$pkgver.tar.xz \
	d37b63403d25fe025704c758e6a3da0979ae50c30c5f02aa78e4410e7b346e3a btrfs-progs btrfs-progs-v$pkgver
source=("${source[0]}") sha256sums=("${sha256sums[0]}")
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys install
optdepends=()
usrside_ohne git asciidoc e2fsprogs python python-setuptools python-sphinx python-sphinx_rtd_theme xmlto libgcrypt
prepare() { :; }
build() {
	cd "${pkgname}"
	./configure --prefix=/usr --with-crypto=builtin --disable-documentation --disable-convert \
		--disable-python --disable-backtrace
	make
}
package() {
	cd "${pkgname}"
	make DESTDIR="$pkgdir" install
}
