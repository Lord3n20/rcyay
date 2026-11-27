# pkgrel 3 (27.11.): mit iwd.service (M7)
pkgrel=3
# Release-Tarball hat configure: kein autoreconf; ohne Handbuchseiten (python-docutils).
# iwd.service wird trotzdem installiert (Pfade fest, kein systemd.pc): rcyay-dienste übersetzt sie für
# runit/s6/BusyBox init (Befund t1 M7 Lauf 4: „iwd: no service to enable“); dazu die D-Bus-Aktivierung.
usrside_ohne python-docutils
prepare() {
	cd ${pkgname}-${pkgver}
	local p
	for p in 0001-use-network-group-for-unprivileged-access.diff 0001-rrm-fix-stack-buffer-overflow-in-rrm_report_beacon_r.patch \
		0002-ie-fix-off-by-one-in-HE-Capabilities-Channel-Width-S.patch 0003-ft-fix-mde_equal-self-comparison-and-type-mismatch.patch \
		0004-ie-fix-uint8_t-underflow-in-FTE-sub-element-parser.patch; do
		patch -Np1 -i ../$p
	done
}
build() {
	cd ${pkgname}-${pkgver}
	./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var --libexecdir=/usr/lib/iwd \
		--enable-external-ell --enable-wired --enable-ofono --enable-hwsim --disable-tools \
		--with-systemd-unitdir=/usr/lib/systemd/system --with-systemd-networkdir=/usr/lib/systemd/network \
		--with-systemd-modloaddir=/usr/lib/modules-load.d --with-dbus-busdir=/usr/share/dbus-1/system-services \
		--disable-manual-pages
	make
}
package() {
	cd ${pkgname}-${pkgver}
	make install DESTDIR="${pkgdir}"
	install -Dm 644 README -t "${pkgdir}/usr/share/doc/${pkgname}"
	# mit Unit legt make install /var/lib/iwd nicht an (StateDirectory=)
	install -dm700 "${pkgdir}/var/lib/iwd" "${pkgdir}/var/lib/ead"
	test -f "${pkgdir}/usr/lib/systemd/system/iwd.service"
}
