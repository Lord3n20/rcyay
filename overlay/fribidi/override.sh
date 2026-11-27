# Release-Tarball, ohne Handbuchseiten (c2man/help2man)
usrside_tarball https://github.com/fribidi/fribidi/releases/download/v$pkgver/fribidi-$pkgver.tar.xz \
	1de5e8073d035388b71d0b2d452b1da1f8caeea8a7702997b401fe1b6250727f fribidi fribidi-$pkgver
source=("${source[0]}") sha256sums=("${sha256sums[0]}")
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne git help2man
prepare() { :; }
build() {
	arch-meson fribidi build -D docs=false -D tests=false
	meson compile -C build
}
