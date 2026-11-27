# kein c99-Hüllskript (POSIX) im System: gcc direkt
build() {
	make CC=gcc LFLAGS="${LDFLAGS} ${LTOFLAGS}"
}
