# Release-Tarball statt git; ohne acl, libxml2, lz4
usrside_tarball https://github.com/libarchive/libarchive/releases/download/v3.8.11/libarchive-3.8.11.tar.xz \
	a3dc0872b1b39da34dc18ef1b944f0f11ffd37425d70ae04be679279f1cf43d2
usrside_ohne acl libacl.so libxml2 libxml2.so lz4 liblz4.so
prepare() {
	sed -n '43,65p' $pkgname/COPYING >BSD-2-Clause.txt
	sed -n '33,62p' $pkgname/$pkgname/archive_read_support_filter_compress.c >BSD-4-Clause-UC.txt
}
build() {
	cd "$pkgname"
	./configure --prefix=/usr --without-nettle --without-xml2 --without-expat --without-lz4 \
		--disable-acl --disable-static
	make
}
