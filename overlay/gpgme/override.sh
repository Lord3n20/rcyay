# Release-Tarball statt git (kein autoreconf); gnupg folgt später (rcyay prüft dann Signaturen)
usrside_tarball https://gnupg.org/ftp/gcrypt/gpgme/gpgme-2.3.1.tar.bz2 \
	2bf94e0e677e5a8fc31f79dd323f91c5416b36fb851058bc1e8bce39f2642434
usrside_ohne gnupg
usrside_patchordner=gpgme-$pkgver
prepare() { :; }
build() {
	cd gpgme
	./configure --prefix=/usr --disable-fd-passing --disable-static --disable-gpg-test \
		--disable-gpgsm-test --disable-gpgconf-test --disable-g13-test
	make
}
