# Release-Tarball statt git; nur curl (ohne compat/gnutls), ohne HTTP/2-3, GSSAPI, libssh2, libpsl, brotli
usrside_tarball https://curl.se/download/curl-8.23.0.tar.xz \
	8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
usrside_nur curl
usrside_ohne brotli libbrotlidec.so krb5 libgssapi_krb5.so libidn2 libidn2.so libnghttp2 libnghttp2.so \
	libnghttp3 libnghttp3.so libngtcp2 libngtcp2.so libpsl libpsl.so libssh2 libssh2.so gnutls nettle patchelf git
prepare() { :; }
build() {
	mkdir -p "$srcdir/build-curl"
	cd "$srcdir/build-curl"
	"$srcdir/curl/configure" --prefix=/usr --mandir=/usr/share/man --disable-ldap --disable-ldaps \
		--disable-manual --enable-ipv6 --enable-threaded-resolver --enable-websockets \
		--with-openssl --with-zlib --with-zstd --without-libpsl --without-brotli --without-nghttp2 \
		--without-libidn2 --without-libssh2 --with-ca-bundle=/etc/ssl/certs/ca-certificates.crt \
		--disable-static
	make
}
package_curl() {
	depends=(ca-certificates openssl zlib zstd musl)
	provides=(libcurl.so)
	cd "$srcdir/build-curl"
	make DESTDIR="$pkgdir" install
	make DESTDIR="$pkgdir" install -C scripts
	install -Dm644 "$srcdir/curl/COPYING" -t "$pkgdir/usr/share/licenses/$pkgname"
}
