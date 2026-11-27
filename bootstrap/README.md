# Bootstrap aus Quellen (PLAN.md 1.4, M3)

Reine Shell-Skripte. Sie laufen als root auf einem Arch-Bauwirt (Test: VM `bau-e2`, `~/vm/rcyay/rvm`)
und bauen UsrsideOS **musl** (pacman-Architektur `x86_64_musl`, Toolchain-Triplette `x86_64-linux-musl`)
vollständig aus Quellen.

```sh
bootstrap/stage0                # Kreuz-Toolchain x86_64-linux-musl nach /opt/usrside-tc   (~12 min, 12 Kerne)
bootstrap/stage1                # kreuzübersetztes Sysroot /var/tmp/usrside/musl/wurzel      (~15 min)
bootstrap/stage2                # chroot: Basis als Pakete aus Arch-PKGBUILDs + overlay/ → Repo usrside-musl
bootstrap/abbild                # qcow2 (btrfs @/@home) + usrkrnl ohne Initramfs → /root/aus
bootstrap/abbild --nur-kernel   # nur den Kernel bauen (kann parallel zu stage2 laufen)
```

Jeder Schritt schreibt ein Log nach `$ARBEIT/musl/logs/SCHRITT.log` und eine Marke nach `$ARBEIT/musl/fertig/`;
ein erneuter Aufruf macht beim ersten unfertigen Schritt weiter (`NEU=1` baut alles neu).
Einstellungen (Umgebung, siehe `lib.sh`): `VARIANTE` (musl | glibc), `TC`, `ARBEIT`, `WURZEL`, `JOBS`,
`RCYAY_QUELLE`/`RCYAY_REF` (Standard `/root/rcyay` bzw. `~/rcyay`, `main`).

## Dateien

| Datei | Inhalt |
|---|---|
| `quellen` | alle Quell-Tarballs mit **fest eingetragener sha256** (Versionen wie Arch am 26.11.2026) |
| `quellen-pruefen` | Pflegewerkzeug: lädt neue Versionen und trägt Prüfsummen ein (`--alle` prüft alles nach) |
| `lib.sh` | gemeinsame Funktionen: `quelle` (Download + Prüfsumme), `auspacken`, `flicken`, `schritt`, `pruefe_elf`, chroot-Einhängen |
| `patches/NAME/` | Patches für stage0/1 (gpgme: `ino64_t`; fakeroot: `SEND_GET_XATTR` ohne stat64) |
| `stage2-liste` | Pakete von stage2 in Bau-Reihenfolge: Arch-Tag + sha256 des PKGBUILD oder `eigen` |
| `stage2-innen` | Teil von stage2 im chroot (Vorbau + makepkg-Schleife) |
| `overlay-anwenden` | wendet `overlay/NAME/` auf einen PKGBUILD-Ordner an (Helfer: `overlay/usrside-overlay.sh`) |
| `pakete/NAME/` | eigene PKGBUILDs, wo Arch nichts Passendes hat (siehe unten) |
| `live-init` | `/init` des Live-Initramfs (`abbild --squashfs`) |
| `besitzer-reparieren` | alter Notbehelf (vor 774ac5e3): packt das Repo als root:root neu; seit dem fakeroot-Fix überflüssig |
| `arch-eintrag` | Pflege (Arch-Rechner): Listenzeile `NAME arch:TAG SHA` aus den Sync-Datenbanken |
| `m5-ordnung` | Pflege: Bau-Reihenfolge für M5 aus den Sync-Datenbanken (depends des gebrauchten Teilpakets, makedepends je Basis, minus `usrside_ohne` des Overlays; `--warum NAME` zeigt die Kette zum Beschneiden) |
| `m5-liste` | M5-Pakete (Stufe 1: gawk, bison, Wayland-Grundlage, xkb, Schriften, seatd, foot – 22 Pakete, in bau-e2 gebaut 27.11.) |
| `kernel-krypto-test` | bootet einen Kernel mit Mini-Initramfs (`test/kernel-krypto.c` als /init) und prüft, was iwd vom Kernel braucht |

## Ablauf

**stage0** (wie CLFS/musl-cross-make): Linux-Header → binutils → gcc pass 1 (nur C, ohne libc) → musl ins
Toolchain-Sysroot `$TC/x86_64-linux-musl/sysroot` → gcc pass 2 (C/C++, shared, threads). Probe: C, C++ mit
Threads, statisch, Lader `/lib/ld-musl-x86_64.so.1`, keine glibc-Abhängigkeit. `VARIANTE=glibc` baut statt musl
glibc 2.44 (Vorarbeit M4, noch ungetestet).

**stage1** kreuzübersetzt mit `x86_64-linux-musl-gcc --sysroot=$WURZEL`: Header, musl, zlib, bzip2, xz, zstd,
openssl, CA-Bündel, libarchive, curl, libgpg-error, libassuan, gpgme, bash, make, patch, fakeroot, pkgconf,
rcyay (pacman, makepkg; aus `main`, Commit in `/usr/share/doc/rcyay.commit`), binutils und gcc nativ (Canadian
Cross), zuletzt BusyBox (Links nur dort, wo kein echtes Programm liegt). `pruefe_elf` bricht ab, sobald eine
Datei glibc braucht oder einen fremden Lader hat; im chroot muss gcc ein Programm übersetzen und starten.

**stage2** hängt Bootstrap, Overlay, Quellen und Repo in das Sysroot ein und arbeitet im chroot:
1. *Vorbau* (nativ, ohne Paket): m4, sed, grep, diffutils, findutils, gawk, coreutils, file, perl, python,
   meson, samurai (+ `arch-meson`). Arch-PKGBUILDs setzen GNU-Werkzeuge und b2sum voraus.
2. *Pakete*: für jede Zeile von `stage2-liste` das PKGBUILD holen (gitlab.archlinux.org, Archiv des Tags,
   sha256 des PKGBUILD geprüft) oder `pakete/NAME` nehmen, `overlay/NAME` anwenden, als Nutzer `bau`
   `makepkg -f --nodeps --nocheck --skippgpcheck --ignorearch` und sofort mit `rcyay -U --overwrite '*'`
   in den chroot installieren. So ersetzen Pakete Schritt für Schritt das Sysroot. `busybox` wird nur gebaut,
   nicht in den chroot installiert (seine Applet-Links würden die GNU-Werkzeuge ersetzen, die die übrigen
   Bauten brauchen). rcyay kommt als `git archive` aus `main` (Version `7.1.0.rN.gHASH`).
3. `repo-add` → `$ARBEIT/musl/repo/usrside-musl.db.tar.zst`, `pacman -Dk` als Prüfung.

**abbild** baut usrkrnl (`usrkrnl/setup --config-only` als normaler Nutzer, Grundlage: Kernel-Konfiguration
des Bauwirts, dazu virtio, btrfs, 9p, serielle Konsole fest eingebaut, Module unkomprimiert für BusyBox
modprobe) mit eingebauter Befehlszeile `root=/dev/vda2 rootfstype=btrfs rootflags=subvol=@ rw`, legt eine
GPT-Platte an (ESP + btrfs mit `@`, `@home`), installiert **alle Pakete des Repos** mit `rcyay -r` aus dem
musl-chroot, kopiert das Repo nach `/srv/usrside-musl` (Server in `/etc/pacman.conf`: `rcyay -Syu` geht im
Abbild ohne eigenes Netz-Repo), dazu Module, Kernel (auch als `EFI/BOOT/BOOTX64.EFI`) und SSH-Schlüssel.
Ausgabe nach dem Vertrag von `~/vm/rcyay/test-rcyay`: `usrside-musl.{qcow2,vmlinuz,cmdline,inits,commit}`.
Seit 26.11. abends in zwei Schritten: **Rootfs-Bau** nach `$ARBEIT/$VARIANTE/abbild-rootfs` (Pakete bzw.
`--rootfs DIR`, Module, Grundeinstellungen; bricht ab, wenn außer `/home` etwas uid 1000 gehört) und **Ausgabe**:
`--platte` (Standard, qcow2 wie oben) und/oder `--squashfs` (Live für M7: `usrside-VARIANTE-live.squashfs` =
künftiges `usrside/rootfs.sfs` im ISO, `…-live.vmlinuz` ohne eingebaute Befehlszeile, `…-live.initramfs`
aus BusyBox + musl-Lader + `live-init`); `--nur-rootfs` hört nach Schritt 1 auf. Das Initramfs sucht
`usrside/rootfs.sfs` auf allen Geräten (`/dev/mapper/ventoy`, `/dev/dm-*`, `/dev/sr*`, alle aus
`/proc/partitions`) und in `*.iso`-Dateien bis Tiefe 3 (bzw. `usrside.iso=PFAD`), hängt squashfs + tmpfs als
overlayfs ein, lässt Medium/ISO/Schichten unter `/run/usrside/` und wechselt mit `switch_root` auf `init=`.
Kernel-Parameter `usrside.warte=S` (Standard 30), `usrside.debug` (Shell statt Neustart bei Fehler).
Der Kernel hat dafür USB-Storage/UAS, NVMe, SR, exFAT, ISO9660, squashfs, overlayfs, dm, loop fest eingebaut,
dazu die Kernel-Krypto für iwd.

**M5** (Desktop aus Quellen): `LISTE=m5-liste bootstrap/stage2 [PAKET …]` baut eine weitere Liste im selben
chroot/Repo. Zeilen erzeugt `arch-eintrag`, die Reihenfolge `m5-ordnung --haben bootstrap/stage2-liste --ohne
DOKU-WERKZEUGE,VORBAU ZIEL…`. Vorgehen wie Alpine/Chimera: Arch-PKGBUILD + Overlay; schwere *optionale*
Abhängigkeiten (Doku, Tests, Demos, X11, glib/ICU wo verzichtbar) per `usrside_ohne` + meson-Option ab, git-Quellen
→ Release-Tarball (Prüfsumme), cmake/autoreconf vermeiden, wo das Projekt configure/Makefile mitliefert.

## Offen

- M5 Stufe 2 ff.: mesa, pipewire, niri, fish, kitty, chromium (Rust-Toolchain nötig, siehe unten).

### Gelöst: fakeroot auf musl (27.11.)
musl exportiert `__xstat`, `__lxstat`, `__fxstat`, `__fxstatat`, `__xmknod(at)` nur als glibc-Kompatibilitätssymbole;
`stat()` & Co. rufen sie nicht auf. fakeroots configure findet sie zuerst und wickelt dann *nur* sie ein → libarchive
(ruft `stat`/`lstat`/`fstat`/`fstatat`) sah die echten Besitzer, Pakete gehörten `bau`. Fix (stage1 und
`overlay/fakeroot`): `ac_cv_func___xstat=no` usw., dann wickelt libfakeroot `stat()` usw. direkt ein (wie bei
glibc-Architekturen ohne xstat). stage2 installiert neu gebaute Pakete jetzt ohne `--needed` (gleiche Version
nach NEU=1/Overlay-Änderung muss in den chroot).

### iwd-Kernel-Krypto (M7)
`bootstrap/kernel-krypto-test KERNEL` (Wirt mit gcc, openssl, bsdtar, qemu): AF_ALG md4/md5/sha1/sha256/384/512,
hmac(…), cmac(aes), ecb/cbc/ctr(aes), ecb(des), cbc(des3_ede), ecb(arc4); keyctl DH_COMPUTE (1536-Bit-Gruppe);
asymmetrische Schlüssel X.509 und PKCS#8 + PKEY_QUERY. Abbild-Kernel 2 (26.11.): alles da außer ecb(arc4) →
ab abbild-kernel-3 `CRYPTO_ARC4`, `CRYPTO_USER_API_ENABLE_OBSOLETE`, `CRYPTO_CTR` im Fragment.
Abbild-Kernel 3 (27.11., Abbild f564bd53): alles ok **außer weiterhin ecb(arc4)** (bind → ENOENT), obwohl
`CRYPTO_ARC4=y`, `ECB=y`, `USER_API_ENABLE_OBSOLETE=y` gesetzt und arc4.o gebaut ist.
**Ursache (27.11.):** Seit Linux 7.3 lässt AF_ALG nur eine feste Liste zu (`crypto/algif_skcipher.c` `skcipher_allowlist`,
sysctl `crypto.af_alg_restrict`, Vorgabe 1); `ecb(arc4)` steht nicht darauf, `af_alg_check_restriction()` liefert absichtlich
ENOENT. Entscheidung: Vorgabe 1 bleibt (AF_ALG ist eine häufige Lückenquelle); TKIP/WPA1 und alte EAP-Verfahren in iwd gehen
damit nicht, WPA2/WPA3 schon. Wer WPA1 braucht: `sysctl crypto.af_alg_restrict=0`. Der Test erwartet deshalb ENOENT bei
restrict=1 und prüft zusätzlich mit restrict=0, dass der Kernel `ecb(arc4)` wirklich hat (Fragment bleibt).

### Inits im Abbild (Paket `usrside-dienste`)
Alle drei liegen im selben Abbild, gewählt wird mit `init=`:

| Init | `init=` | Dienste |
|---|---|---|
| BusyBox init | `/usr/bin/init` (= `/sbin/init`) | `/etc/inittab` (respawn) |
| runit | `/usr/bin/runit-init` | `/etc/runit/{1,2,3}`, `/etc/sv/*`, `/var/service` |
| s6 | `/etc/s6-linux-init/current/bin/init` | `s6-linux-init-maker` beim Installieren, Dienste im run-image |

Gemeinsam: `/usr/lib/usrside/rc.start` (Dateisysteme, Hostname, lo, mdev, `ssh-keygen -A`) und `rc.stop`;
Dienste sshd, dhcpcd, getty auf ttyS0. `poweroff`/`reboot`/`halt` (`/usr/bin/poweroff`) richten sich nach PID 1.
**systemd gibt es für musl nicht** (PLAN 1.2: systemd setzt glibc voraus; die glibc-Variante M4 hat es).
Die Dienstdateien sind bewusst statisch; sobald `rcyay-dienste` (M2) sie aus systemd-Units erzeugt, kann
`usrside-dienste` darauf umgestellt werden (`overlay/dienste/` gehört M2).

## Was nicht (ganz) aus den Arch-PKGBUILDs gebaut wird – und warum

| Was | Warum |
|---|---|
| eigene PKGBUILDs `filesystem`, `musl`, `gcc`, `ca-certificates` | Arch-`filesystem` ist auf glibc/systemd zugeschnitten; Arch-`musl` ist nur ein Kreuz-Wrapper unter `/usr/lib/musl`, hier ist musl die System-libc; Arch-`gcc` baut ada/d/fortran/go/rust und multilib (32-Bit-glibc); `ca-certificates` erzeugt Arch über p11-kit/nss, hier direkt das Mozilla-Bündel von curl.se (fester Hash). |
| eigene PKGBUILDs `rcyay`, `runit`, `skalibs`, `execline`, `s6`, `s6-linux-init`, `usrside-dienste` | nicht in den Arch-Repos (rcyay ist unser Paketmanager; runit/s6 gibt es nur im AUR bzw. bei Artix). |
| git-Quellen → Release-Tarballs (xz, ncurses, libarchive, curl, gpgme, patch, pkgconf, fakeroot, binutils) | Arch baut viele Pakete aus git-Tags mit `autoreconf`/gnulib-`bootstrap`; dafür bräuchte stage2 git, autoconf, automake, libtool, gettext, gperf, help2man, texinfo. Die Release-Tarballs derselben Version enthalten die erzeugten Dateien und haben feste Prüfsummen. fakeroot: Debian-Tarball 1.37.1.1 statt Arch-git 1.37.2. |
| abgeschaltete Abhängigkeiten (overlay `usrside_ohne`) | brotli, nghttp2/3, ngtcp2, libpsl, libssh2, krb5, libidn2 (curl); acl, libxml2, lz4 (libarchive, zstd); guile (make); attr (patch); PAM, libedit, ldns, FIDO (openssh); udev (dhcpcd); jansson, debuginfod (binutils). Sie sind (noch) nicht gebaut – M5 ergänzt sie, ohne am Verfahren etwas zu ändern. |
| `linux-api-headers` ohne `headers_install` | braucht rsync; stattdessen `make headers` und Kopieren (wie LFS). |
| gnupg fehlt noch | gpgme ist da, aber ohne gpg prüft rcyay keine Signaturen: Repo `usrside-musl` hat `SigLevel = Never`, PKGBUILD-Signaturen werden mit `--skippgpcheck` übergangen (die sha256/b2-Prüfsummen gelten weiter). |
| Python ohne `_ssl` im Vorbau | Python 3.14.10 nutzt noch `TLSv1_method`, das OpenSSL 4 nicht mehr hat; meson braucht kein ssl. |
| gcc: zwei awk-Stellen umgeschrieben | gawk 5.4.1 auf musl: ein Feldelement, das nur in `arr[x] == ""` angefasst wurde, wird beim Anhängen zu `"0"` (`e[k] == ""; e[k] = e[k] "a"` → `0a`; gleiches gawk auf glibc → `a`; kein Optimierungs-/PMA-Effekt). gcc's `opt-gather.awk`/`optc-gen.awk` treffen genau das; `pakete/gcc` prüft dort mit `j > 1` bzw. `in`. BusyBox awk ist keine Alternative (frisst `\\+` im gsub-Ersatz → `CL_CXX` fehlt). **Ursache (27.11.):** kein musl-Fehler, sondern gawk 5.4.1 ohne MPFR (`--without-mpfr`, so baut der Vorbau) – auf glibc genauso (`BEGIN{z[1]; print "[" z[1] "]"}` → `[0]`). M5 baut deshalb zuerst Arch-gawk mit MPFR (`overlay/gawk`); traf auch libpng (`pnglibconf.h`). |
| Kernel nicht als Paket | usrkrnl ist absichtlich ein maschinenbezogener Bau (`usrkrnl/setup`), kein generisches Paket; das Abbild bekommt ihn direkt. |
| Rust (niri, fish ≥ 4, M5 Stufe 2) | rustc lässt sich nicht ohne vorhandenes rustc bauen; Saat ist das offizielle `x86_64-unknown-linux-musl`-Toolchain-Archiv von static.rust-lang.org (feste sha256, eigenes Paket `rust-saat` 1.100.0, provides rust/cargo). fish und niri werden zunächst direkt damit gebaut; Arch-`rust` aus Quellen braucht llvm und folgt mit Stufe 2c. `makepkg.conf`: `RUSTFLAGS=-C target-feature=-crt-static` (das musl-Ziel linkt sonst statisch). Crates lädt cargo nach `Cargo.lock` (dort mit Prüfsummen). |
| `libudev-zero` statt systemd-libudev | kein udevd auf dem musl-System; libinput/niri/mesa brauchen nur die libudev-API. Hotplug ohne udevd: Helfer aus `contrib/` (mdev) – noch offen. Patches: libudev-zero zählt auch `/sys/class/sound/cardN` auf (haben keinen dev-Eintrag), pipewire verlangt kein `SOUND_INITIALIZED` (setzt nur eine udev-Regel). |
| mesa ohne LLVM (Stufe 2b) | iris/crocus/anv (Intel), llvmpipe und rusticl brauchen CLC = LLVM + clang + libclc + SPIRV-LLVM-Translator. Stufe 2b baut nur virgl (VM) + softpipe, ohne GLX/X11/libglvnd/Vulkan/VA; die Python-Bauhelfer (mako, markupsafe, packaging, pyyaml) kommen als Quell-Tarballs über PYTHONPATH. Echte Intel-Grafik auf musl erst mit llvm (2c). |
| `ttf-dejavu` aus dem Upstream-TTF-Release 2.37 | Arch erzeugt die TTF-Dateien mit fontforge + perl-font-ttf aus dem git-Stand (2.37+18). Für reine Schriftdaten wäre fontforge (Python, GTK-frei, aber groß) ein eigener Baum; das Release-Archiv der Schriftautoren hat eine feste sha256. Versionsnummer bleibt die von Arch. |
| niri ohne Standard-Features | `dbus`, `systemd`, `xdp-gnome-screencast` (pipewire) aus: kein D-Bus/logind auf musl; Bildschirmfreigabe folgt mit dbus + pipewire (2c). |
| cmake mit gebündelten Kleinbibliotheken | jsoncpp, libuv, rhash, cppdap, nghttp2 aus dem cmake-Quellbaum statt eigener Pakete; ohne cmake-gui (Qt), Sphinx-Doku, Emacs-Modus. |
| kitty zurückgestellt | braucht Go (zweite Saat-Toolchain), ein paketiertes Python zur Laufzeit, X11-Bibliotheken zum Bauen, shader-slang/simde; foot ist das Terminal der musl-Variante. Nicht „nur glibc“, nur später. |
| HTTPS mit BusyBox-wget | BusyBox-TLS prüft keine Zertifikate; für Downloads mit Prüfung nimmt rcyay/makepkg curl. |

## Quellen und Vertrauen
Alle Tarballs: HTTPS-Download, danach Vergleich mit der in `quellen` bzw. im PKGBUILD eingetragenen sha256;
Abweichung → Abbruch und Datei nach `*.falsch`. Der Linux-Tarball ist zusätzlich gegen kernel.orgs
`sha256sums.asc` verglichen. Arch-PKGBUILDs: sha256 der PKGBUILD-Datei in `stage2-liste` (die gitlab-Archive
selbst sind nicht bytegleich reproduzierbar). `SOURCE_DATE_EPOCH` ist fest gesetzt.
