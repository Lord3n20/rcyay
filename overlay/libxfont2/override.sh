# Release-Tarball statt git
usrside_tarball "https://xorg.freedesktop.org/releases/individual/lib/libXfont2-$pkgver.tar.xz" \
	cef1f4ad9cd69f439bb1ac484bdc8f9d7f8e70215b92d8ca86b96e31513e5f9c libxfont
unset b2sums sha512sums validpgpkeys
usrside_ohne git
prepare() { :; }
