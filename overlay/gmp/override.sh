# kein autoreconf nötig (Release-Tarball)
prepare() { :; }
# configure-Probe von gmp 6.3.0 ist kein gültiges C23 (gcc ≥ 15): mit gnu17 bauen
build() {
	cd gmp-$pkgver
	CFLAGS+=" -std=gnu17" ./configure --build="$CHOST" --prefix=/usr --enable-cxx --enable-fat
	make
}
