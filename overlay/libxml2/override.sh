# Release-Tarball, ohne ICU und ohne Testsuite-Daten, ohne Doku-Paket
usrside_tarball https://download.gnome.org/sources/libxml2/${pkgver%.*}/libxml2-$pkgver.tar.xz \
	98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821 libxml2 libxml2-$pkgver
source=("${source[0]}") sha256sums=("${sha256sums[0]}")
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
usrside_ohne icu libicuuc.so python doxygen git
usrside_nur libxml2
# meson.build ruft git describe auf (bricht ohne git ab, auch mit check: false)
prepare() { sed -i "s/run_command('git', 'describe', check: false)/run_command('false', check: false)/" libxml2/meson.build; }
build() {
	arch-meson libxml2 build -D icu=disabled -D legacy=enabled -D python=disabled -D docs=disabled
	meson compile -C build
}
package_libxml2() {
	optdepends=()
	provides=(libxml2.so)
	meson install -C build --destdir "$pkgdir"
	install -Dm644 libxml2/libxml.m4 -t "$pkgdir/usr/share/aclocal"
	rm -rf "$pkgdir/usr/share/doc"
	install -Dm644 libxml2/Copyright -t "$pkgdir/usr/share/licenses/$pkgname"
}
