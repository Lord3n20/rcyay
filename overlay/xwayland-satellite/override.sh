# ohne systemd-Feature (sd-notify) und ohne systemd-User-Dienst; niri startet xwayland-satellite selbst
build() {
	cd $pkgname-$pkgver
	cargo build --frozen --release
}
package() {
	cd $pkgname-$pkgver
	install -vDm 755 target/release/$pkgname -t "$pkgdir/usr/bin/"
	install -vDm 644 LICENSE -t "$pkgdir/usr/share/licenses/$pkgname/"
}
