# Release-Tarball statt git; xorg-fonts-encodings (nur Daten für Bitmap-Schriften) entfällt vorerst
usrside_tarball "https://xorg.freedesktop.org/releases/individual/lib/libfontenc-$pkgver.tar.xz" \
	daa186a49e3cc2b6fc585ca09971351e6f010f618abc6a164eda82e42e4aa4d6 libfontenc
unset b2sums sha512sums validpgpkeys
usrside_ohne git xorg-fonts-encodings
prepare() { :; }
