# Release-Tarball statt git (kein autogen/doxygen nötig)
usrside_tarball https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.xz \
	4ce24038fd4221e0d13bc1a2de7a4db56e90b92b3bf75321f6c14be73f65de4b
prepare() { :; }
build() {
	cd xz
	./configure --prefix=/usr --disable-rpath --disable-doc
	make
}
