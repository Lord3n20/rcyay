# minizip (autoreconf) und zlib-static entfallen
usrside_nur zlib
prepare() { :; }
build() {
	cd zlib-$pkgver
	CFLAGS+=" -ffat-lto-objects" ./configure --prefix=/usr
	make
}
