# Release-Tarball statt git (enthält configure, kein autoreconf/xorg-util-macros nötig)
usrside_tarball "https://xorg.freedesktop.org/releases/individual/lib/libX11-$pkgver.tar.xz" \
	bbc78c46217174655c2b32b138ad119f3ac15f50808b4d0b3a2677a4b1afc950 libx11
unset b2sums sha512sums validpgpkeys
usrside_ohne git
prepare() { :; }
