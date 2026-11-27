# Release-Tarball mit fertigem configure (kein autoreconf im chroot); eigener Dateiname, da Arch
# unter libffi-$pkgver.tar.gz das github-Archiv (anderer Inhalt) lädt
source=("libffi-$pkgver-release.tar.gz::https://github.com/libffi/libffi/releases/download/v$pkgver/libffi-$pkgver.tar.gz")
sha256sums=(81e879c094cc5ec3186c48510c6385ac6b3e866192e8bc71a35245703757ee40)
unset b2sums sha512sums md5sums
prepare() { :; }
