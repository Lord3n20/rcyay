# Archiv des Tags, ohne PGO (bräuchte sway headless + llvm), ohne Handbuchseiten (scdoc)
usrside_tarball https://codeberg.org/dnkl/foot/archive/$pkgver.tar.gz \
	4296be402b5684d049534598e69db92b918f92beac9dab76b585207045f0b037 foot foot
usrside_ohne git llvm sway scdoc python
build() {
	cd "$pkgbase"
	meson setup --prefix=/usr --buildtype=release --wrap-mode=nodownload -Dterminfo-base-name=foot-extra \
		-Ddocs=disabled build
	meson compile -C build
}
