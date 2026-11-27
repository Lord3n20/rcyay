# Ohne Standard-Features (dbus, systemd, xdp-gnome-screencast/pipewire): kein D-Bus/logind auf dem musl-
# System, Bildschirmfreigabe kommt mit pipewire+dbus später. Rust aus rust-saat, libudev aus libudev-zero.
# Crates nach Cargo.lock (--locked, Prüfsummen dort). Ohne Bauzeit-Commit (git).
source=("niri-$pkgver.tar.gz::https://github.com/YaLTeR/niri/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=(134c602d8e0d53413a52d6cd58f9ce7e79a07d03288ee0a51ba1abd5db1b1ad9)
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
usrside_ohne clang git libpipewire pipewire xdg-desktop-portal-impl
depends+=(libudev-zero)
build() {
	cd "niri-$pkgver"
	CFLAGS+=" -ffat-lto-objects"
	cargo build --frozen --release --no-default-features
	for shell in bash fish zsh; do
		./target/release/niri completions "$shell" >"$shell-completions"
	done
}
