# Nur adwaita-cursors (Standard-Cursor über default-cursors, Befund t1); das Icon-Thema selbst braucht
# adwaita-icon-theme-legacy/hicolor und gtk-update-icon-cache – folgt mit GTK-Anwendungen.
# Release-Tarball statt git; die Cursor liegen fertig (Xcursor) im Quellbaum, gebaut wird nichts.
usrside_tarball "https://gitlab.gnome.org/GNOME/adwaita-icon-theme/-/archive/$pkgver/adwaita-icon-theme-$pkgver.tar.gz" \
	adda5270c67ecdeb9604d24202fd8558b193379f9a9488179591cd4cdd99593b adwaita-icon-theme
unset b2sums
usrside_ohne git meson gtk-update-icon-cache adwaita-icon-theme-legacy hicolor-icon-theme
usrside_nur adwaita-cursors
# ohne meson: meson.build verlangt gtk-update-icon-cache schon beim Einrichten; die Cursor werden nur kopiert
build() { :; }
package_adwaita-cursors() {
	pkgdesc="GNOME standard cursors"
	depends=()
	install -d "$pkgdir/usr/share/icons/Adwaita"
	cp -a "$srcdir/adwaita-icon-theme/Adwaita/cursors" "$pkgdir/usr/share/icons/Adwaita/"
	install -Dm644 "$srcdir/adwaita-icon-theme/COPYING" -t "$pkgdir/usr/share/licenses/$pkgname"
}
