# Release-Tarball statt git; ohne gprofng, debuginfod, jansson, gold
usrside_tarball https://ftp.gnu.org/gnu/binutils/binutils-2.47.tar.xz \
	154ab23b60070e8f27013c22977f1129425d67d1e8acd6e13010e617811e4cff
usrside_ohne jansson libelf elfutils debuginfod
prepare() { :; }
build() {
	mkdir -p "$srcdir/binutils-build"
	cd "$srcdir/binutils-build"
	"$srcdir/binutils-gdb/configure" --prefix=/usr --sysconfdir="$pkgdir"/etc --with-lib-path=/usr/lib:/usr/local/lib \
		--with-bugurl=https://github.com/Lord3n20/usrsideos/issues --enable-cet=no --enable-colored-disassembly \
		--enable-default-execstack=no --enable-deterministic-archives --enable-ld=default --enable-new-dtags \
		--enable-plugins --enable-relro --enable-shared --enable-targets=x86_64-pep,bpf-unknown-none \
		--enable-threads --disable-gdb --disable-gdbserver --disable-libdecnumber --disable-readline \
		--disable-sim --disable-werror --disable-gprofng --disable-nls --with-pic --with-system-zlib \
		--without-debuginfod --disable-jansson
	make configure-host
	make tooldir=/usr
}
package() {
	cd "$srcdir/binutils-build"
	make prefix="$pkgdir/usr" tooldir="$pkgdir/usr" install
	rm -f "$pkgdir"/usr/share/man/man1/{dlltool,windres,windmc}*
	rm -f "$pkgdir"/usr/lib/lib{bfd,opcodes,ctf,ctf-nobfd,sframe}.a
	rm -rf "$pkgdir/etc"
	install -Dm644 "$srcdir/binutils-gdb/COPYING3" -t "$pkgdir/usr/share/licenses/$pkgname" 2>/dev/null || true
}
# makepkg darf pkgver() nicht über git aufrufen (Tarball)
unset -f pkgver
