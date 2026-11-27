# Release-Tarball; Rust aus rust-saat (provides rust); ohne Sphinx-Doku (--help der Builtins braucht dann
# mandoc/groff, optional); Crates lädt cargo nach Cargo.lock (Prüfsummen dort)
usrside_tarball https://github.com/fish-shell/fish-shell/releases/download/$pkgver/fish-$pkgver.tar.xz \
	20998a25f73217ddcc19f499055fd587e9912d1ad6e7109120fbcf2871f0b98c fish fish-$pkgver
source=("${source[0]}") sha256sums=("${sha256sums[0]}")
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
usrside_ohne git jq python-sphinx
build() {
	cd fish
	export CXXFLAGS+=" ${CPPFLAGS}"
	cmake -B build -D CMAKE_INSTALL_PREFIX=/usr -D CMAKE_INSTALL_SYSCONFDIR=/etc -D CMAKE_BUILD_TYPE=Release \
		-D WITH_DOCS=OFF -D FISH_USE_SYSTEM_PCRE2=ON -D WITH_MESSAGE_LOCALIZATION=OFF -W no-dev
	make -C build VERBOSE=1
}
