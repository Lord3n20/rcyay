# ohne Handbuchseiten (scdoc)
usrside_ohne scdoc
build() {
	arch-meson $pkgname build -Dgrapheme-shaping=enabled -Drun-shaping=enabled -Ddocs=disabled
	meson compile -C build
}
