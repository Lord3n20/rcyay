# Release-Tarball (mit configure, kein autoreconf); ohne vlock (PAM) und ohne Tests (check)
usrside_tarball https://www.kernel.org/pub/linux/utils/kbd/kbd-$pkgver.tar.xz \
	6e5ca4f8d76ee9e3a8db700b667f13e12aac9933828a64e1aaad93d26be9b479 kbd kbd-$pkgver
source=("${source[0]}" fix-euro2.patch) sha256sums=("${sha256sums[0]}" SKIP)
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys backup
provides=() conflicts=() replaces=()
usrside_ohne git check pam gzip
prepare() {
	cd "${pkgname}"
	mv data/keymaps/i386/qwertz/cz{,-qwertz}.map
	mv data/keymaps/i386/olpc/es{,-olpc}.map
	mv data/keymaps/i386/olpc/pt{,-olpc}.map
	mv data/keymaps/i386/fgGIod/trf{,-fgGIod}.map
	mv data/keymaps/i386/colemak/{en-latin9,colemak}.map
	patch -Np1 -i ../fix-euro2.patch
}
build() {
	cd "${pkgname}"
	./configure --prefix=/usr --sysconfdir=/etc --datadir=/usr/share/kbd --mandir=/usr/share/man \
		--enable-optional-progs --disable-tests --disable-vlock --disable-nls
	make KEYCODES_PROGS=yes RESIZECONS_PROGS=yes
}
package() {
	cd "${pkgname}"
	make KEYCODES_PROGS=yes RESIZECONS_PROGS=yes DESTDIR="${pkgdir}" install
	usrside_busybox_doppelte
}
