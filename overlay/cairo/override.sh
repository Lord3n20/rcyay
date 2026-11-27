# Release-Tarball; nur Wayland-taugliches Bild-/PNG-/FreeType-Backend, ohne X11 (xlib/xcb), lzo, Tests
usrside_tarball https://cairographics.org/releases/cairo-$pkgver.tar.xz \
	1c767308174337a74694da0f3ec069c271452163a1ef4540964c50c301f157d4 cairo cairo-$pkgver
source=("${source[0]}") sha256sums=("${sha256sums[0]}")
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
usrside_ohne git gtk-doc libx11 libxcb libxext libxrender lzo
prepare() { :; }
build() {
	arch-meson cairo build -D xlib=disabled -D xcb=disabled -D xlib-xcb=disabled -D lzo=disabled \
		-D spectre=disabled -D symbol-lookup=disabled -D tests=disabled -D gtk_doc=false -D glib=enabled \
		-D png=enabled -D freetype=enabled -D fontconfig=enabled -D zlib=enabled
	meson compile -C build
}
usrside_nur cairo
package_cairo() {
	provides=(libcairo-gobject.so libcairo-script-interpreter.so libcairo.so)
	options+=(!emptydirs)
	meson install -C build --destdir "$pkgdir"
	rm -rf "$pkgdir/usr/share/gtk-doc"
}
