# Release-Tarball (enthält den Arch-Cherry-Pick noch nicht), ohne Doku (docbook)
usrside_tarball https://gitlab.freedesktop.org/api/v4/projects/890/packages/generic/fontconfig/$pkgver/fontconfig-$pkgver.tar.xz \
	95013637eff5f81d4871dd16cd098c273264c16d0f9f0b27cc43b81b2794bee3 fontconfig fontconfig-$pkgver
usrside_ohne docbook-sgml docbook-utils git perl-sgmls
prepare() { :; }
build() {
	arch-meson fontconfig build -D bitmap-conf=noinstall -D default-sub-pixel-rendering=noinstall \
		-D doc=disabled -D tests=disabled
	meson compile -C build
}
