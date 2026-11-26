# Release-Tarball; nur libharfbuzz (+subset), ohne glib/gobject/graphite/icu/cairo/chafa/Doku
usrside_tarball https://github.com/harfbuzz/harfbuzz/releases/download/$pkgver/harfbuzz-$pkgver.tar.xz \
	e2ec49090744383cdbe7df808c1cedaafba06a97be8dc4211fe546ba585f12cb harfbuzz harfbuzz-$pkgver
usrside_ohne cairo chafa git glew glfw glib2 glib2-devel gobject-introspection graphite gtk-doc help2man icu \
	libgl python ragel libglib-2.0.so libgobject-2.0.so libgraphite2.so
usrside_nur harfbuzz
prepare() { :; }
build() {
	CFLAGS="${CFLAGS/-fexceptions/}"
	CXXFLAGS="${CXXFLAGS/-fexceptions/}"
	arch-meson harfbuzz build -D cpp_std=c++17 -D glib=disabled -D gobject=disabled -D graphite2=disabled \
		-D icu=disabled -D cairo=disabled -D chafa=disabled -D introspection=disabled -D docs=disabled \
		-D tests=disabled -D utilities=disabled -D freetype=enabled -D png=enabled -D zlib=enabled
	meson compile -C build
}
package_harfbuzz() {
	optdepends=()
	provides=(libharfbuzz{,-subset}.so)
	meson install -C build --destdir "$pkgdir" --no-rebuild
	install -Dt "$pkgdir/usr/share/licenses/$pkgname" -m644 harfbuzz/COPYING
}
