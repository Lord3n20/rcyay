# Release-Tarball; ohne Xft/libthai/sysprof/Introspection/Doku
usrside_tarball https://download.gnome.org/sources/pango/${pkgver%.*}/pango-$pkgver.tar.xz \
	342385b6ca3b7c73455d7c80a13b7dbe4489e00bc3bd4c5bd6ed4dce421e374a pango pango-$pkgver
source=("${source[0]}") sha256sums=("${sha256sums[0]}")
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
usrside_ohne gi-docgen git glib2-devel gobject-introspection help2man python-docutils libsysprof-capture \
	libthai libx11 libxft libxrender
prepare() { :; }
build() {
	arch-meson pango build -D introspection=disabled -D documentation=false -D man-pages=false \
		-D xft=disabled -D libthai=disabled -D sysprof=disabled -D build-testsuite=false \
		-D build-examples=false -D cairo=enabled -D fontconfig=enabled -D freetype=enabled
	meson compile -C build
}
