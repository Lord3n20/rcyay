# Release-Tarball mit fertigem configure (kein autoreconf im chroot)
usrside_tarball https://download.sourceforge.net/libpng/libpng-$pkgver.tar.xz \
	d80dd2a38a37f803cb9b6ac7b14bd6e74ddc3b654780a8380bdf93523fdb4389 libpng libpng-$pkgver
usrside_ohne git
prepare() { :; }
