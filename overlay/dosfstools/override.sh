# mkfs.vfat/fsck.vfat-Links von BusyBox bleiben dort; mkfs.fat/fsck.fat aus diesem Paket
_usrside_dosfs_pkg() {
	cd $pkgname-$pkgver
	make DESTDIR="$pkgdir" install
	usrside_busybox_doppelte
}
package() { _usrside_dosfs_pkg; }
