# ohne PAM (Passwortprüfung über /etc/shadow), ohne LDAP/SSSD
usrside_ohne pam libldap
unset backup
backup=(etc/sudo.conf etc/sudo_logsrvd.conf etc/sudoers)
build() {
	cd "${pkgname}-${_sudover}"
	./configure --prefix=/usr --sbindir=/usr/bin --libexecdir=/usr/lib --with-rundir=/run/sudo \
		--with-vardir=/var/db/sudo --with-logfac=auth --without-pam --with-env-editor \
		--with-passprompt="[sudo] password for %p: " \
		--with-secure-path-value=/usr/local/sbin:/usr/local/bin:/usr/bin --with-all-insults --disable-nls
	sed -i -e 's/ -shared / -Wl,-O1,--as-needed\0/g' libtool
	make
}
package() {
	depends+=('libcrypto.so' 'libssl.so')
	cd "${pkgname}-${_sudover}"
	make DESTDIR="$pkgdir" install
	rm -f "$pkgdir/etc/sudoers.dist"
	rm -rf "$pkgdir/run"
	install -Dm644 LICENSE.md -t "$pkgdir/usr/share/licenses/sudo"
}
