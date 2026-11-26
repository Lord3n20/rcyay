# Nur die Bibliothek (ohne ft2demos/Doku: cairo, glib2, librsvg, libx11, qt6-base), ohne harfbuzz/brotli
# (harfbuzz hängt selbst an freetype2 – Arch löst den Kreis über dynamic). Arch-Patches bleiben.
usrside_ohne brotli harfbuzz cairo glib2 librsvg libx11 qt6-base libgcc libstdc++
usrside_nur freetype2
build() {
	arch-meson freetype-$pkgver build -D default_library=shared -D error_strings=true \
		-D harfbuzz=disabled -D brotli=disabled -D png=enabled -D zlib=system -D bzip2=enabled
	meson compile -C build
}
package_freetype2() {
	optdepends=()
	provides=(libfreetype.so)
	backup=(etc/profile.d/freetype2.sh)
	meson install -C build --destdir="$pkgdir"
	install -Dm644 freetype2.sh -t "$pkgdir/etc/profile.d"
	install -Dm644 freetype-$pkgver/docs/FTL.TXT -t "$pkgdir/usr/share/licenses/$pkgname"
}
