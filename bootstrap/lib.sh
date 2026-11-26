# Gemeinsame Funktionen und Einstellungen für bootstrap/stage0, stage1, stage2, abbild.
# Wird mit "." eingebunden. Alle Einstellungen lassen sich per Umgebung ändern:
#   VARIANTE   musl (Standard) | glibc
#   TC         Kreuz-Toolchain              (Standard /opt/usrside-tc)
#   ARBEIT     Bau-, Quell- und Logordner   (Standard /var/tmp/usrside)
#   WURZEL     Sysroot von stage1           (Standard $ARBEIT/$VARIANTE/wurzel)
#   JOBS       parallele Jobs               (Standard: Kerne, höchstens 1 je GiB freiem Speicher)
#   NEU=1      alle Schritte neu bauen (sonst werden fertige übersprungen)
# shellcheck shell=bash disable=SC2034

BOOTSTRAP=${BOOTSTRAP:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}
QUELLLISTE=$BOOTSTRAP/quellen
VARIANTE=${VARIANTE:-musl}
case $VARIANTE in
musl)
	ZIEL=x86_64-linux-musl
	ARCH_KENNUNG=x86_64-musl
	LADER=/lib/ld-musl-x86_64.so.1
	;;
glibc)
	ZIEL=x86_64-usrside-linux-gnu
	ARCH_KENNUNG=x86_64
	LADER=/lib64/ld-linux-x86-64.so.2
	;;
*)
	echo "VARIANTE muss musl oder glibc sein, nicht '$VARIANTE'" >&2
	exit 1
	;;
esac
TC=${TC:-/opt/usrside-tc}
TCSYS=$TC/$ZIEL/sysroot
ARBEIT=${ARBEIT:-/var/tmp/usrside}
QUELLEN=${QUELLEN:-$ARBEIT/quellen}
BAU=${BAU:-$ARBEIT/$VARIANTE/bau}
LOGS=${LOGS:-$ARBEIT/$VARIANTE/logs}
FERTIG=${FERTIG:-$ARBEIT/$VARIANTE/fertig}
WURZEL=${WURZEL:-$ARBEIT/$VARIANTE/wurzel}
if [[ -z ${JOBS:-} ]]; then
	JOBS=$(nproc)
	_gib=$(awk '/^MemAvailable:/{print int($2/1048576)}' /proc/meminfo 2>/dev/null || echo "$JOBS")
	((_gib >= 1 && _gib < JOBS)) && JOBS=$_gib
	unset _gib
fi
export LC_ALL=C TZ=UTC
# Fehler in $(…) sollen wie überall sonst abbrechen
shopt -s inherit_errexit
# reproduzierbare Zeitstempel (gzip, ar, Python, …)
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1795651200} # 2026-11-26

meldung() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warnung() { printf '\033[1;33m==> WARNUNG:\033[0m %s\n' "$*" >&2; }
abbruch() {
	printf '\033[1;31m==> FEHLER:\033[0m %s\n' "$*" >&2
	exit 1
}

# lade URL DATEI – Download mit Wiederholung, erst nach Erfolg unter dem endgültigen Namen
lade() {
	mkdir -p "$(dirname "$2")"
	curl -fL --retry 4 --retry-delay 5 --connect-timeout 30 -o "$2.teil" "$1" || {
		rm -f "$2.teil"
		abbruch "Download fehlgeschlagen: $1"
	}
	mv "$2.teil" "$2"
}

# _eintrag NAME – setzt q_ver q_sum q_url aus bootstrap/quellen
_eintrag() {
	local n v s u
	while read -r n v s u; do
		[[ $n == "$1" ]] && {
			q_ver=$v q_sum=$s q_url=$u
			return 0
		}
	done < <(grep -v '^#' "$QUELLLISTE")
	abbruch "Quelle '$1' steht nicht in $QUELLLISTE"
}

_sha256() { sha256sum "$1" | cut -d' ' -f1; }

version() {
	_eintrag "$1"
	printf '%s\n' "$q_ver"
}

# quelle NAME – gibt den Pfad der geprüften Quelldatei aus (lädt sie bei Bedarf)
quelle() {
	_eintrag "$1"
	[[ $q_sum =~ ^[0-9a-f]{64}$ ]] || abbruch "Quelle '$1' hat keine Prüfsumme (bootstrap/quellen-pruefen $1)"
	local datei=$QUELLEN/${q_url##*/}
	if [[ ! -s $datei || $(_sha256 "$datei") != "$q_sum" ]]; then
		lade "$q_url" "$datei"
	fi
	[[ $(_sha256 "$datei") == "$q_sum" ]] || {
		mv "$datei" "$datei.falsch"
		abbruch "Prüfsumme von $1 stimmt nicht (Datei nach $datei.falsch verschoben)"
	}
	printf '%s\n' "$datei"
}

# auspacken NAME [ZIELORDNER] – packt frisch aus und gibt den Quellordner aus
auspacken() {
	local datei ziel=${2:-$BAU/$1-src} oben
	datei=$(quelle "$1")
	rm -rf "$ziel"
	mkdir -p "$ziel"
	if command -v bsdtar >/dev/null; then
		LC_ALL=C.UTF-8 bsdtar -xf "$datei" -C "$ziel" --no-same-owner
	else
		tar -xf "$datei" -C "$ziel" --no-same-owner
	fi
	# genau ein Ordner auf oberster Ebene → diesen nehmen
	oben=$(find "$ziel" -mindepth 1 -maxdepth 1)
	if [[ $(wc -l <<<"$oben") == 1 && -d $oben ]]; then
		printf '%s\n' "$oben"
	else
		printf '%s\n' "$ziel"
	fi
}

# flicken QUELLORDNER NAME – wendet bootstrap/patches/NAME/*.patch an (falls vorhanden)
flicken() {
	local p
	for p in "$BOOTSTRAP/patches/$2"/*.patch; do
		[[ -e $p ]] || continue
		meldung "Patch ${p##*/}"
		patch -d "$1" -Np1 -i "$p"
	done
}

# schritt NAME FUNKTION – führt FUNKTION einmal aus (Log in $LOGS/NAME.log, Marke in $FERTIG/NAME)
schritt() {
	local name=$1 rc
	shift
	mkdir -p "$LOGS" "$FERTIG" "$BAU"
	if [[ -e $FERTIG/$name && ${NEU:-0} != 1 ]]; then
		meldung "$name: schon fertig (NEU=1 baut neu)"
		return 0
	fi
	meldung "$name …"
	local start=$SECONDS
	set +e
	(
		set -euo pipefail
		cd "$BAU"
		"$@"
	) >"$LOGS/$name.log" 2>&1 </dev/null
	rc=$?
	set -e
	if ((rc != 0)); then
		tail -n 40 "$LOGS/$name.log" >&2
		abbruch "$name fehlgeschlagen (rc $rc), Log: $LOGS/$name.log"
	fi
	date -u +%FT%TZ >"$FERTIG/$name"
	meldung "$name fertig ($((SECONDS - start)) s)"
}

# pruefe_elf ORDNER – alle ELF-Dateien müssen zur Variante passen (kein glibc in musl und umgekehrt)
pruefe_elf() {
	local f interp needed schlecht=0
	while IFS= read -r -d '' f; do
		[[ $(head -c4 "$f" 2>/dev/null | od -An -c | tr -d ' ') == 177ELF ]] || continue
		interp=$(readelf -l "$f" 2>/dev/null | sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p')
		needed=$(readelf -d "$f" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
		if [[ -n $interp && $interp != "$LADER" ]]; then
			warnung "$f: falscher Lader $interp"
			schlecht=1
		fi
		if [[ $VARIANTE == musl ]] && grep -qxE 'libc\.so\.6|ld-linux-x86-64\.so\.2|libm\.so\.6|libpthread\.so\.0' <<<"$needed"; then
			warnung "$f: braucht glibc ($(tr '\n' ' ' <<<"$needed"))"
			schlecht=1
		fi
	done < <(find "$1" -type f \( -perm -u+x -o -name '*.so*' \) -print0)
	return "$schlecht"
}

# chroot-Einhängepunkte: einhaengen WURZEL QUELLE ZIEL [mount-Optionen …]; aushaengen räumt alles ab
eingehaengt=()
einhaengen() {
	local w=$1 q=$2 z=$1$3
	shift 3
	mkdir -p "$z"
	mountpoint -q "$z" && return 0
	mount "$@" "$q" "$z"
	eingehaengt=("$z" "${eingehaengt[@]}")
}
aushaengen() {
	local z
	for z in "${eingehaengt[@]}"; do umount -l "$z" 2>/dev/null || true; done
	eingehaengt=()
}
# chroot_grund WURZEL – /dev, /dev/pts, /proc, /sys, /run
chroot_grund() {
	einhaengen "$1" /dev /dev --bind
	einhaengen "$1" devpts /dev/pts -t devpts -o gid=5,mode=620
	einhaengen "$1" proc /proc -t proc
	einhaengen "$1" sysfs /sys -t sysfs
	einhaengen "$1" tmpfs /run -t tmpfs
}

# brauche BEFEHL … – bricht ab, wenn ein Werkzeug des Bauwirts fehlt
brauche() {
	local b fehlt=()
	for b; do command -v "$b" >/dev/null || fehlt+=("$b"); done
	((${#fehlt[@]} == 0)) || abbruch "auf dem Bauwirt fehlt: ${fehlt[*]} (Arch: pacman -S --needed base-devel …)"
}
