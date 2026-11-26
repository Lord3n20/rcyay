# ohne PAM, Kerberos, libedit, ldns, FIDO; ohne systemd-Units (Dienste: usrside-dienste)
usrside_ohne pam krb5 libedit ldns libfido2 libxcrypt libcrypt.so linux-headers
build() {
	cd "$pkgname-$pkgver"
	./configure --prefix=/usr --sbindir=/usr/bin --libexecdir=/usr/lib/ssh --sysconfdir=/etc/ssh \
		--disable-strip --with-ssl-engine=no --with-privsep-user=nobody --with-pid-dir=/run \
		--with-default-path='/usr/local/sbin:/usr/local/bin:/usr/bin' --without-pam --without-kerberos5 \
		--without-libedit --without-ldns --without-security-key-builtin --without-zlib-version-check
	make
}
package() {
	cd "$pkgname-$pkgver"
	make DESTDIR="$pkgdir" install
	install -Dm644 LICENCE -t "$pkgdir/usr/share/licenses/$pkgname/"
	install -Dm755 contrib/findssl.sh "$pkgdir/usr/bin/findssl.sh"
	install -Dm755 contrib/ssh-copy-id "$pkgdir/usr/bin/ssh-copy-id"
}
