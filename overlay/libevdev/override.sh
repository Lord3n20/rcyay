# ohne Tests (check/valgrind)
usrside_ohne check valgrind
build() {
	arch-meson $pkgname-$pkgver build -D documentation=disabled -D tests=disabled
	meson compile -C build
}
check() { :; }
