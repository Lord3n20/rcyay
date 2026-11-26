# dynamisch gegen musl (nicht musl-gcc); Applet-Links für alles, was kein anderes Basispaket liefert
makedepends=()
build() {
	cd "$srcdir/$pkgname-$pkgver"
	unset LDFLAGS
	make defconfig
	sed -i -e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' \
		-e 's/^# CONFIG_FEATURE_WGET_HTTPS is not set/CONFIG_FEATURE_WGET_HTTPS=y/' \
		-e 's/^CONFIG_FEATURE_SH_STANDALONE=y/# CONFIG_FEATURE_SH_STANDALONE is not set/' .config
	{ yes '' || true; } | make oldconfig >/dev/null
	export KCONFIG_NOTIMESTAMP=1
	make BB_EXTRA_VERSION="-$pkgrel" busybox busybox.links
}
package() {
	cd "$srcdir/$pkgname-$pkgver"
	install -Dm0755 busybox "$pkgdir/usr/bin/busybox"
	install -Dm644 .config "$pkgdir/usr/share/doc/$pkgname/config"
	local l
	# diese Namen liefern andere Pakete (bash, xz, bzip2, patch, usrside-dienste …)
	local andere=" sh bash patch xz unxz xzcat lzma unlzma lzcat bzip2 bunzip2 bzcat poweroff reboot halt "
	while read -r l; do
		l=${l##*/}
		[[ $andere == *" $l "* || $l == busybox || $l == linuxrc ]] && continue
		# was ein anderes installiertes Paket schon liefert (runit sv/chpst, binutils strings …), bleibt dessen
		[[ -e /usr/bin/$l ]] && pacman -Qqo "/usr/bin/$l" >/dev/null 2>&1 && continue
		ln -sf busybox "$pkgdir/usr/bin/$l"
	done <busybox.links
}
