# ohne Doku (doxygen, xmlto, mdbook …), nur das Paket wayland
usrside_ohne libxslt doxygen xmlto graphviz docbook-xsl mdbook
usrside_nur wayland
build() {
	arch-meson $pkgbase-$pkgver build -D documentation=false -D tests=false
	meson compile -C build
}
package_wayland() {
	provides=(libwayland-{client,cursor,egl,server}.so)
	meson install -C build --destdir "$pkgdir"
	rm -rf "$pkgdir"/usr/share/{doc,man}
	install -Dm 644 $pkgbase-$pkgver/COPYING "$pkgdir/usr/share/licenses/$pkgname/COPYING"
}
