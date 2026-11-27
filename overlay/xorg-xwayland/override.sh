# Xwayland für xwayland-satellite (niri): ohne glamor/DRI3/GLX (libxshmfence wird für misyncshm trotzdem gebraucht) (mesa ohne libGL/libglvnd, kein libepoxy), ohne
# libdecor (nur rootful), libei (Portal-Eingabe), secure-rpc (libtirpc/krb5); SHA1 aus OpenSSL statt nettle.
# X-Clients rendern damit über SHM in Software – genügt für X11-Programme unter niri.
# Ohne xorg-server-common: protocol.txt und Xserver.1 bleiben in diesem Paket.
usrside_ohne nettle libepoxy libtirpc libei libdecor libglvnd mesa mesa-libgl xorg-server-common systemd
build() {
	arch-meson xwayland-$pkgver build \
		-D ipv6=true -D xvfb=false -D xdmcp=false -D xcsecurity=true \
		-D dri3=false -D glamor=false -D glx=false -D libdecor=false -D xwayland_ei=false \
		-D secure-rpc=false -D sha1=libcrypto -D libunwind=false -D docs=false \
		-D xkb_dir=/usr/share/X11/xkb -D xkb_output_dir=/var/lib/xkb
	meson configure build
	ninja -C build
}
package() {
	depends+=(openssl xorg-xkbcomp xkeyboard-config)
	DESTDIR="${pkgdir}" ninja -C build install
	install -m644 -Dt "${pkgdir}/usr/share/licenses/${pkgname}" xwayland-$pkgver/COPYING
}
