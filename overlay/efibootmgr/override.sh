# GitHub-Archiv des Tags statt git
usrside_tarball https://github.com/rhboot/efibootmgr/archive/refs/tags/$pkgver.tar.gz \
	442867d12f8525034a404fc8af3036dba8e1fc970998af2486c3b940dfad0874 efibootmgr efibootmgr-$pkgver
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne git
