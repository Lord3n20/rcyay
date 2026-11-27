# Arch-gawk (mit MPFR) ersetzt den Vorbau-gawk: gawk 5.4.1 OHNE MPFR (--without-mpfr) hat einen Fehler –
# ein nur gelesenes Feldelement wird zu "0" (BEGIN{z[1]; print "[" z[1] "]"} → [0]), auf glibc genauso.
# Trifft gcc (opt-gather.awk) und libpng (pnglibconf.h fast leer). Ohne PMA (persistenter Speicher, auf musl ungeprüft).
build() {
	cd ${pkgname}-${pkgver}
	./configure --prefix=/usr --libexecdir=/usr/lib --sysconfdir=/etc --without-libsigsegv --disable-pma
	make
}
