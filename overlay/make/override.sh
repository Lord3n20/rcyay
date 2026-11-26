# ohne guile; make 4.4.1 deklariert getenv() ohne Prototyp (C23-Fehler mit gcc ≥ 15)
usrside_ohne guile
build() {
	cd "$pkgname-$pkgver"
	CFLAGS+=" -std=gnu17" ./configure --prefix=/usr --without-guile
	make
}
