# codeberg hat das Tag-Archiv neu erzeugt (anderer Hash als in Arch, Inhalt gleiche Version):
# eigene Prüfsumme (27.11.2026, zweimal geladen, gleich)
sha256sums=(b0c0f4a599f43723736c8565b8b84337c4195077f07f1bb8bb3252bb13a2306a)
# ohne Handbuchseiten (scdoc)
usrside_ohne scdoc
build() {
	arch-meson $pkgname build -Dgrapheme-shaping=enabled -Drun-shaping=enabled -Ddocs=disabled
	meson compile -C build
}
