# Release-Tarball; ohne Qt-Oberfläche, Sphinx-Doku und Emacs-Modus; jsoncpp/libuv/rhash/cppdap/nghttp2
# aus dem Quellbaum (spart vier Pakete), curl/expat/libarchive/zlib/zstd/xz/bzip2 vom System
usrside_tarball https://github.com/Kitware/CMake/releases/download/v$pkgver/cmake-$pkgver.tar.gz \
	a79bf33cbe05686fdf79c2bbbcfe5e8e62ad16d3d540fa737cbedfeed41fb69e cmake cmake-$pkgver
source=("${source[0]}") sha256sums=("${sha256sums[0]}")
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne emacs git nlohmann-json python-sphinx qt6-base cppdap jsoncpp libuv rhash hicolor-icon-theme
build() {
	cd cmake
	./bootstrap --prefix=/usr --mandir=/share/man --docdir=/share/doc/cmake --datadir=/share/cmake \
		--system-libs --no-system-jsoncpp --no-system-libuv --no-system-librhash --no-system-cppdap \
		--no-system-nghttp2 --parallel="$(getconf _NPROCESSORS_ONLN)"
	make
}
package() {
	optdepends=('make: for unix Makefile generator' 'ninja: for ninja generator')
	cd cmake
	make DESTDIR="$pkgdir" install
	rm -rf "$pkgdir/usr/share/doc/cmake/html" "$pkgdir/usr/share/emacs"
	install -Dm644 LICENSE.rst -t "$pkgdir/usr/share/licenses/$pkgname"
	install -Dm644 Utilities/cmrapidhash/LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE.rapidhash"
}
