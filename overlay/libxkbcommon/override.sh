# Archiv des Tags; ohne X11-Teil (libxcb) und ohne Doku, nur das Paket libxkbcommon
usrside_tarball https://github.com/xkbcommon/libxkbcommon/archive/refs/tags/xkbcommon-$pkgver.tar.gz \
	acc4d5f7c3cbba5f9f8d08d8bdbeede84ecede46792f47929aa9321873385528 libxkbcommon libxkbcommon-xkbcommon-$pkgver
usrside_ohne bash-completion doxygen git graphviz libxcb xorgproto
usrside_nur libxkbcommon
build() {
	arch-meson libxkbcommon build -D enable-docs=false -D enable-x11=false -D enable-bash-completion=false
	meson compile -C build
}
package_libxkbcommon() {
	depends=(musl libxml2 xkeyboard-config)
	optdepends=('wayland: xkbcli interactive-wayland')
	provides=(libxkbcommon.so libxkbregistry.so)
	meson install -C build --destdir "$pkgdir"
	rm -rf "$pkgdir/usr/share/doc"
	install -Dt "$pkgdir/usr/share/licenses/$pkgname" -m644 libxkbcommon/LICENSE
}
