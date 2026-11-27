# Release-Tarball hat configure: kein autoreconf; ohne systemd-Unit (Dienst: rcyay-dienste/overlay dienste),
# ohne Handbuchseiten (python-docutils)
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
		--disable-systemd-service --disable-manual-pages
	make
}
