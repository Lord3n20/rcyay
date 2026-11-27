# Release-Tarball (mit configure); ohne PAM, audit, acl/attr, libcap, yescrypt/libxcrypt (crypt() aus musl,
# SHA512), ohne Handbuchseiten (docbook) und ohne su/login (BusyBox). Was BusyBox schon liefert (passwd,
# chpasswd, login …), bleibt bei BusyBox; dazu kommen useradd/groupadd/usermod/userdel/chage …
usrside_tarball https://github.com/shadow-maint/shadow/releases/download/${_upstream_tag}/shadow-${_upstream_tag}.tar.xz \
	239781632846db3b01901028147fb4b57e6f2d5330ca63ce104b21d9b3e6031f shadow shadow-${_upstream_tag}
source=("${source[0]}" useradd.defaults LICENSE) sha256sums=("${sha256sums[0]}" SKIP SKIP)
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
backup=(etc/default/useradd etc/login.defs)
usrside_ohne git acl audit docbook-xsl itstool libcap libxslt pam
prepare() { :; }
build() {
	cd $pkgname
	./configure --prefix=/usr --bindir=/usr/bin --sbindir=/usr/bin --libdir=/usr/lib --sysconfdir=/etc \
		--mandir=/usr/share/man --disable-account-tools-setuid --disable-man --disable-nls \
		--with-group-name-max-length=32 --without-libpam --without-audit --without-acl --without-attr \
		--without-yescrypt --without-bcrypt --without-libbsd --disable-logind --without-sssd \
		--without-nscd --without-selinux --without-tcb --without-su
	make
}
package() {
	cd $pkgname
	make DESTDIR="$pkgdir" install
	usrside_busybox_doppelte
	# nologin liefert util-linux
	rm -f "$pkgdir/usr/bin/nologin" "$pkgdir/usr/share/man/man8/nologin.8"
	rm -rf "$pkgdir/usr/include" "$pkgdir/usr/lib/libsubid"* "$pkgdir/usr/lib/pkgconfig"
	install -vDm644 COPYING -t "$pkgdir/usr/share/licenses/$pkgname/"
	install -vDm644 ../LICENSE "$pkgdir/usr/share/licenses/$pkgname/0BSD.txt"
	install -vDm600 ../useradd.defaults "$pkgdir/etc/default/useradd"
}
