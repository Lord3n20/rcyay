# Release-Tarball (enthält sljit und ein fertiges configure), kein autogen/git
usrside_tarball https://github.com/PCRE2Project/pcre2/releases/download/pcre2-$pkgver/pcre2-$pkgver.tar.bz2 \
	53c156e1ba416a20da8e65395daa132da0d80e76910424caca3fcdae7831d384 pcre2 pcre2-$pkgver
source=("${source[0]}") sha256sums=("${sha256sums[0]}")
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne git
prepare() {
	cd pcre2
	cp -v deps/sljit/LICENSE ../BSD-2-Clause.txt
	sed -n '70,94p' LICENCE.md >../BSD-3-Clause.txt
	sed -n '100,104p' LICENCE.md >../PCRE2-exception.txt
}
