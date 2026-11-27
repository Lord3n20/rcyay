# Release-Tarball (gvdb liegt bei), ohne libmount/sysprof/Introspection/Doku; ein Paket glib2 (enthält
# auch glib2-devel: gdbus-codegen, glib-mkenums … – laufen mit dem Vorbau-python)
usrside_tarball https://download.gnome.org/sources/glib/${pkgver%.*}/glib-$pkgver.tar.xz \
	93c941aa17d5eb1d53fe838365f29a8b4e539c222a256d974ec8f30fc413e396 glib glib-$pkgver
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne dbus dconf gettext gi-docgen git gobject-introspection libelf python-docutils python-packaging \
	shared-mime-info util-linux util-linux-libs libmount.so libsysprof-capture
usrside_nur glib2
prepare() {
	cd glib
	patch -Np1 -i ../0001-glib-compile-schemas-Remove-noisy-deprecation-warnin.patch
	patch -Np1 -i ../0002-gdesktopappinfo-Add-more-known-terminals.patch
}
build() {
	CFLAGS+=" -ffat-lto-objects" CXXFLAGS+=" -ffat-lto-objects"
	arch-meson glib build --default-library both -D documentation=false -D dtrace=disabled \
		-D glib_debug=disabled -D introspection=disabled -D man-pages=disabled -D selinux=disabled \
		-D sysprof=disabled -D systemtap=disabled -D libmount=disabled -D tests=false -D libelf=disabled
	meson compile -C build
}
package_glib2() {
	depends+=(libffi.so)
	provides+=(glib2-devel "libg"{lib,io,irepository,module,object,thread}"-2.0.so")
	conflicts=(glib2-devel)
	optdepends=()
	options+=(staticlibs)
	meson install -C build --no-rebuild --destdir "$pkgdir"
	install -Dt "$pkgdir/usr/share/libalpm/hooks" -m644 ./*.hook
	touch "$pkgdir/usr/lib/gio/modules/.keep"
	rm -rf "$pkgdir/usr/share/doc"
}
