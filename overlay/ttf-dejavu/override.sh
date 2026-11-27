# Upstream-Release der fertigen TTF-Dateien (2.37) statt Neuerzeugung mit fontforge aus dem git-Stand
# (fontforge + perl-font-ttf wären ein eigener großer Baum nur für Schriftdaten); README „Was nicht … aus Quellen“
usrside_tarball https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2 \
	fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7 dejavu-fonts dejavu-fonts-ttf-2.37
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset -f pkgver
usrside_ohne fontforge git perl-font-ttf
build() { :; }
package() {
	cd dejavu-fonts
	install -Dm0644 -t "$pkgdir/usr/share/fonts/TTF" ttf/*.ttf
	install -Dm0644 -t "$pkgdir/usr/share/fontconfig/conf.avail" fontconfig/*.conf
	mkdir -p "$pkgdir/usr/share/fontconfig/conf.default"
	ln -srt "$pkgdir/usr/share/fontconfig/conf.default" "$pkgdir"/usr/share/fontconfig/conf.avail/*
	install -Dm0644 -t "$pkgdir/usr/share/licenses/$pkgname" LICENSE
}
