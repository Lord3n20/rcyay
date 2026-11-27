# GitHub-Archiv des Tags statt git
usrside_tarball https://github.com/vcrhonek/hwdata/archive/v$pkgver/hwdata-$pkgver.tar.gz \
	f0c64cd7e31d70a5fb3a52e53ab50a61e74c0421a6381eaa45114eec3bde5fe7 hwdata hwdata-$pkgver
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne git
