# UsrsideOS-Overlay-Helfer (PLAN.md 1.2). Wird von bootstrap/overlay-anwenden zusammen mit
# overlay/NAME/override.sh an das Arch-PKGBUILD angehängt und läuft also beim Einlesen durch makepkg.
# Ein override.sh darf alles neu setzen (source, Prüfsummen, build(), package() …) und diese Helfer nutzen:
#   usrside_tarball URL SHA256 [ORDNER [TARORDNER]]  erste git-Quelle durch Release-Tarball ersetzen
#                 (weitere git-Quellen wie gnulib entfallen); ORDNER = Name des git-Ordners (Standard:
#                 aus der URL), TARORDNER = Ordner im Tarball (Standard: Dateiname ohne .tar.*)
#   usrside_ohne NAME …                   Abhängigkeiten zusätzlich entfernen (z. B. lz4 libxml2)
#   usrside_nur PAKET …                   von Split-Paketen nur diese bauen
# Danach (automatisch) usrside_abschluss: glibc → musl, systemd*/lib32-*/libxcrypt/libnsl entfernen,
# Paketfunktionen einwickeln, damit auch dort gesetzte depends umgeschrieben werden.
# shellcheck shell=bash disable=SC2034,SC2154,SC2128,SC2329

_usrside_ohne=(systemd systemd-libs libsystemd.so libudev.so libxcrypt libcrypt.so libnsl valgrind)
_usrside_link_von=() _usrside_link_nach=()

usrside_ohne() { _usrside_ohne+=("$@"); }

usrside_nur() {
	local p alt=("${pkgname[@]}")
	pkgname=()
	for p in "${alt[@]}"; do
		[[ " $* " == *" $p "* ]] && pkgname+=("$p")
	done
}

usrside_tarball() {
	local url=$1 sum=$2 ordner=${3:-} tardir=${4:-} i a n erst=-1 neu=() behalten=() alt
	for i in "${!source[@]}"; do
		if [[ ${source[i]} == *git+* ]]; then
			((erst < 0)) || continue # weitere git-Quellen (z. B. gnulib) entfallen
			erst=${#behalten[@]}
			if [[ -z $ordner ]]; then
				if [[ ${source[i]} == *::* ]]; then
					ordner=${source[i]%%::*}
				else
					n=${source[i]%%#*}
					n=${n%%\?*}
					n=${n##*/}
					ordner=${n%.git}
				fi
			fi
		fi
		behalten+=("$i")
	done
	if ((erst < 0)); then # keine git-Quelle: Tarball vorn ergänzen
		source=("$url" "${source[@]}")
		for a in md5sums sha1sums sha224sums sha256sums sha384sums sha512sums b2sums cksums; do
			declare -p "$a" &>/dev/null || continue
			eval "${a}=(SKIP \"\${${a}[@]}\")"
		done
		erst=0
	else
		for a in source md5sums sha1sums sha224sums sha256sums sha384sums sha512sums b2sums cksums; do
			declare -p "$a" &>/dev/null || continue
			eval "alt=(\"\${${a}[@]}\")"
			neu=()
			for i in "${behalten[@]}"; do neu+=("${alt[i]:-SKIP}"); done
			eval "$a=(\"\${neu[@]}\")"
		done
		source[erst]=$url
	fi
	# Prüfsumme: sha256 für den Tarball, in allen anderen Feldern SKIP
	for a in md5sums sha1sums sha224sums sha384sums sha512sums b2sums cksums; do
		declare -p "$a" &>/dev/null || continue
		eval "${a}[${erst}]=SKIP"
	done
	if ! declare -p sha256sums &>/dev/null; then
		sha256sums=()
		for i in "${!source[@]}"; do sha256sums[i]=SKIP; done
	fi
	sha256sums[erst]=$sum
	# Tarball-Ordner unter dem Namen des git-Ordners verfügbar machen
	if [[ -z $tardir ]]; then
		tardir=${url##*/}
		tardir=${tardir%.tar.*}
		tardir=${tardir%.tgz}
	fi
	if [[ -n $ordner && $ordner != "$tardir" ]]; then
		_usrside_link_von+=("$tardir") _usrside_link_nach+=("$ordner")
	fi
}

_usrside_map() {
	local -n _feld=$1
	local d name o weg neu=()
	for d in "${_feld[@]}"; do
		name=${d%%[<>=:]*}
		case $name in
		glibc) d=musl ;;
		lib32-*) continue ;;
		esac
		weg=0
		for o in "${_usrside_ohne[@]}"; do [[ $name == "$o" ]] && weg=1; done
		((weg)) || neu+=("$d")
	done
	_feld=("${neu[@]}")
}

usrside_abschluss() {
	local f a
	for a in depends makedepends checkdepends optdepends; do
		if declare -p "$a" &>/dev/null; then _usrside_map "$a"; fi
	done
	for f in $(declare -F | awk '{print $3}' | grep -E '^package(_.*)?$'); do
		eval "_usrside_orig_$f() $(declare -f "$f" | tail -n +2)"
		eval "$f() { _usrside_orig_$f \"\$@\"; local _a; for _a in depends optdepends; do if declare -p \"\$_a\" &>/dev/null; then _usrside_map \"\$_a\"; fi; done; }"
	done
	if ((${#_usrside_link_von[@]})) || [[ -n ${_usrside_patchliste:-} ]]; then
		if declare -F prepare >/dev/null; then
			eval "_usrside_orig_prepare() $(declare -f prepare | tail -n +2)"
		else
			_usrside_orig_prepare() { :; }
		fi
		prepare() {
			local i
			cd "$srcdir" || return 1
			for i in "${!_usrside_link_von[@]}"; do
				[[ -e ${_usrside_link_nach[i]} ]] || ln -s "${_usrside_link_von[i]}" "${_usrside_link_nach[i]}"
			done
			_usrside_patches || return 1
			cd "$srcdir" || return 1
			_usrside_orig_prepare
		}
	fi
}

# Overlay-Patches (overlay/NAME/*.patch, nach $startdir/usrside-patches kopiert) vor prepare()
# im Ordner ${usrside_patchordner:-$pkgbase bzw. $pkgname-$pkgver} anwenden
_usrside_patches() {
	local p d
	[[ -n ${_usrside_patchliste:-} ]] || return 0
	d=${usrside_patchordner:-}
	if [[ -z $d ]]; then
		for d in "${pkgbase:-$pkgname}" "${pkgbase:-$pkgname}-$pkgver" "$pkgname-$pkgver" ""; do
			[[ -n $d && -d $srcdir/$d ]] && break
		done
	fi
	cd "$srcdir/$d" || return 1
	for p in $_usrside_patchliste; do
		echo "usrside-overlay: Patch $p"
		patch -Np1 -i "$startdir/usrside-patches/$p" || return 1
	done
}

# usrside_busybox_doppelte [BEHALTEN …]: in package() aufrufen; entfernt aus $pkgdir/usr/bin alles, was im
# Bau-chroot als BusyBox-Applet-Link existiert (passwd, setfont …) – sonst Dateikonflikt mit dem Paket busybox.
# BEHALTEN: Namen, die dieses Paket trotzdem liefert; das busybox-Paket lässt sie weg (overlay/busybox „andere“),
# ohne dieses Paket legt usrside-busybox die Links an.
usrside_busybox_doppelte() {
	local f n
	for f in "$pkgdir"/usr/bin/*; do
		[[ -e $f || -L $f ]] || continue
		n=${f##*/}
		[[ " $* " == *" $n "* ]] && continue
		if [[ -L /usr/bin/$n && $(readlink "/usr/bin/$n") == busybox ]]; then
			echo "usrside-overlay: $n bleibt bei BusyBox"
			rm -f "$f"
			rm -f "$pkgdir/usr/share/man/man1/$n.1" "$pkgdir/usr/share/man/man8/$n.8"
		fi
	done
}
