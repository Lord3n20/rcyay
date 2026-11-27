# Release-Tarball statt git; ohne systemd (makedepends nur für die Erkennung des Neustart-Befehls)
usrside_tarball https://github.com/NetworkConfiguration/openresolv/releases/download/v$pkgver/openresolv-$pkgver.tar.xz \
	901d83a15520b80e117aeca8cb6f5b70ceb205ba050a638d45cebcf304df729b openresolv openresolv-$pkgver
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne git
