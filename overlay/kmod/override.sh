# kmod für musl (Befund e1e M7): BusyBox modprobe lädt keine .ko.zst (usrkrnl packt die Module mit zstd).
# Release-Tarball (meson), ohne Handbuchseiten (scdoc) und ohne Testsuite (braucht Kernel-Header + Module).
# modprobe/insmod/rmmod/lsmod/modinfo/depmod gehören kmod (overlay/busybox „andere“).
usrside_tarball https://www.kernel.org/pub/linux/utils/kernel/kmod/kmod-$pkgver.tar.xz \
	4c7942dda4793d08f975270322db582c9701438afd4d76ed9e6e7cf4094cb967 kmod kmod-$pkgver
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne git scdoc linux-headers libelf
build() {
	arch-meson kmod build -Dbuild-tests=false -Dmanpages=false -Dopenssl=enabled -Dxz=enabled \
		-Dzlib=enabled -Dzstd=enabled
	meson compile -C build
}
check() { :; }
