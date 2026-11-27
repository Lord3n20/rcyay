# pkgrel 5 (27.11.): lsblk/blkid/mount … gehören util-linux; pkgrel 6: ln -r / ln -t (0001-ln-relativ.patch,
# update-ca-trust; dieselbe Datei in bootstrap/pakete/busybox für glibc); modprobe … depmod gehören kmod)
pkgrel=6
# dynamisch gegen musl (nicht musl-gcc); Applet-Links für alles, was kein anderes Basispaket liefert
# GNU sed gibt es nicht als Paket: das Applet erfüllt die Abhängigkeit (fakeroot)
provides=(sed)
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
	# lsblk … swapoff: volle Fassungen aus util-linux (Installer, usrkrnl); fehlt util-linux, legt usrside-busybox
	# die Links an (Hook, freie Pfade)
	local andere=" sh bash patch xz unxz xzcat lzma unlzma lzcat bzip2 bunzip2 bzcat poweroff reboot halt lsblk blkid findfs mount umount losetup fdisk mkswap swapon swapoff modprobe insmod rmmod lsmod modinfo depmod "
	while read -r l; do
		l=${l##*/}
		[[ $andere == *" $l "* || $l == busybox || $l == linuxrc ]] && continue
		# was ein anderes installiertes Paket schon liefert (runit sv/chpst, binutils strings …), bleibt dessen
		[[ -e /usr/bin/$l ]] && pacman -Qqo "/usr/bin/$l" >/dev/null 2>&1 && continue
		ln -sf busybox "$pkgdir/usr/bin/$l"
	done <busybox.links
}
