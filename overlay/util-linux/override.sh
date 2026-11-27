# Release-Tarball statt git; ohne PAM (login, su, runuser, chfn/chsh bleiben weg – BusyBox login/su),
# ohne systemd/udev-Regeln, Python-Bindung und lastlog2 (sqlite). Was BusyBox schon liefert (kill, more, dmesg …),
# bleibt bei BusyBox (usrside_busybox_doppelte), außer _usrside_ul_vorrang (lsblk, blkid, mount …); dazu kommen
# agetty, findmnt, sfdisk, wipefs … und die Bibliotheken (libblkid/libuuid für btrfs-progs).
usrside_tarball https://www.kernel.org/pub/linux/utils/util-linux/v${pkgver%.*}/util-linux-$pkgver.tar.xz \
	fbd62a100ab7bb8746ba0661255c3c48185b1e9021507c624da01fbc696330ec util-linux util-linux-$pkgver
source=("${source[0]}") sha256sums=("${sha256sums[0]}")
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne git asciidoctor bash-completion libcap-ng libxcrypt po4a python sqlite systemd pam shadow coreutils \
	file libmagic.so libcrypt.so
prepare() { :; }
# diese Namen liefert util-linux statt BusyBox (Liste auch in overlay/busybox)
_usrside_ul_vorrang="lsblk blkid findfs mount umount losetup fdisk mkswap swapon swapoff"
build() {
	arch-meson util-linux build -Dfs-search-path=/usr/bin:/usr/local/bin -Dlibuser=disabled \
		-Dlibutempter=disabled -Dncurses=disabled -Dncursesw=enabled -Deconf=disabled \
		-Dbuild-login=disabled -Dbuild-su=disabled -Dbuild-runuser=disabled -Dbuild-chfn-chsh=disabled \
		-Dbuild-sulogin=disabled -Dbuild-newgrp=disabled -Dbuild-vipw=disabled -Dbuild-line=disabled \
		-Dbuild-liblastlog2=disabled -Dbuild-pam-lastlog2=disabled -Dbuild-python=disabled \
		-Dbuild-pylibmount=disabled -Dbuild-bash-completion=disabled -Dbuild-uuidd=disabled \
		-Dsystemd=disabled -Dmagic=disabled -Dcryptsetup=disabled -Dprogram-tests=false -Dnls=disabled
	meson compile -C build
}
package_util-linux() {
	conflicts=(rfkill hardlink) provides=(rfkill hardlink) replaces=(rfkill hardlink)
	depends=("util-linux-libs=${pkgver}" musl ncurses readline zlib)
	unset backup
	DESTDIR="${pkgdir}" meson install -C build
	rm -f "${pkgdir}"/usr/lib/lib*.a
	rm -rf "${pkgdir}"/usr/lib/systemd "${pkgdir}"/usr/share/bash-completion
	install -d util-linux-libs/lib
	mv "$pkgdir"/usr/lib/lib*.so* util-linux-libs/lib/
	mv "$pkgdir"/usr/lib/pkgconfig util-linux-libs/lib/pkgconfig
	mv "$pkgdir"/usr/include util-linux-libs/include
	if [[ -d $pkgdir/usr/share/man/man3 ]]; then mv "$pkgdir"/usr/share/man/man3 util-linux-libs/man3; fi
	# volle Fassungen für Installer/usrkrnl (lsblk -o, blkid -s/-o, mount -o subvol …)
	usrside_busybox_doppelte $_usrside_ul_vorrang
	# nologin liefert filesystem (bootstrap/pakete/filesystem)
	rm -f "$pkgdir/usr/bin/nologin" "$pkgdir/usr/share/man/man8/nologin.8"
	# sbin-Programme legt meson nach /usr/bin (--sbindir bin); leere Ordner weg
	find "$pkgdir" -depth -type d -empty -delete
	install -vDm644 util-linux/Documentation/licenses/COPYING.{BSD*,ISC} -t "$pkgdir/usr/share/licenses/$pkgname/"
}
package_util-linux-libs() {
	pkgdesc='util-linux runtime libraries'
	depends=(musl)
	provides=(libutil-linux libblkid.so libfdisk.so libmount.so libsmartcols.so libuuid.so)
	conflicts=(libutil-linux) replaces=(libutil-linux) optdepends=()
	install -d "$pkgdir"/usr/lib "$pkgdir"/usr/share/man
	mv util-linux-libs/lib/* "$pkgdir"/usr/lib/
	mv util-linux-libs/include "$pkgdir"/usr/include
	if [[ -d util-linux-libs/man3 ]]; then mv util-linux-libs/man3 "$pkgdir"/usr/share/man/man3; fi
	install -vDm644 util-linux/Documentation/licenses/COPYING.{BSD*,ISC} -t "$pkgdir/usr/share/licenses/$pkgname/"
}
