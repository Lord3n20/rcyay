# Release-Tarball statt git + gnulib (kein bootstrap); ohne attr
usrside_tarball https://ftp.gnu.org/gnu/patch/patch-2.8.1.tar.xz \
	3240c7959c5347c8ed347e3c4201c0337bf4240d5cf09d68889e58eeaba30e17
usrside_ohne attr
prepare() { :; }
build() {
	cd patch
	./configure --prefix=/usr --disable-xattr
	make
}
