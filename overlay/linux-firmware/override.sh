# Release-Tarball von kernel.org statt git (Quelle = die Firmware-Dateien selbst; „Bauen“ heißt kopieren und
# mit zstd packen). Ein Paket linux-firmware mit allen Dateien statt der Arch-Teilung (provides deckt die
# Teilpakete ab); ohne GNU parallel (einspurig, zstd mit allen Kernen) und ohne dedup (rdfind).
usrside_tarball https://cdn.kernel.org/pub/linux/kernel/firmware/linux-firmware-$pkgver.tar.xz \
	a10d1ab5baf2c74ba92e8c7cf313e691696c26bddfa6df00db795e48d70cd4ca linux-firmware linux-firmware-$pkgver
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne git parallel python rdfind
prepare() { :; }
build() { :; }
usrside_nur linux-firmware
package_linux-firmware() {
	pkgdesc="Firmware files for Linux (all)"
	license=(LicenseRef-WHENCE)
	depends=()
	optdepends=()
	provides=(linux-firmware-whence linux-firmware-other amd-ucode linux-firmware-amd linux-firmware-amdgpu
		linux-firmware-atheros linux-firmware-broadcom linux-firmware-cirrus linux-firmware-intel
		linux-firmware-liquidio linux-firmware-marvell linux-firmware-mediatek linux-firmware-mellanox
		linux-firmware-nfp linux-firmware-nvidia linux-firmware-qcom linux-firmware-qlogic linux-firmware-radeon
		linux-firmware-realtek linux-firmware-ti)
	conflicts=("${provides[@]}")
	cd "$srcdir/$pkgbase"
	ZSTD_CLEVEL=19 ZSTD_NBTHREADS=0 make FIRMWAREDIR="$pkgdir/usr/lib/firmware" install-zst
	install -Dm644 WHENCE -t "$pkgdir/usr/share/licenses/$pkgname"
	install -Dm644 LICEN[CS]E.* -t "$pkgdir/usr/share/licenses/$pkgname" 2>/dev/null || true
}
