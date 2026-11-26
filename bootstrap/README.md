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
| gcc: zwei awk-Stellen umgeschrieben | gawk 5.4.1 auf musl: ein Feldelement, das nur in `arr[x] == ""` angefasst wurde, wird beim Anhängen zu `"0"` (`e[k] == ""; e[k] = e[k] "a"` → `0a`; gleiches gawk auf glibc → `a`; kein Optimierungs-/PMA-Effekt). gcc's `opt-gather.awk`/`optc-gen.awk` treffen genau das; `pakete/gcc` prüft dort mit `j > 1` bzw. `in`. BusyBox awk ist keine Alternative (frisst `\\+` im gsub-Ersatz → `CL_CXX` fehlt). Ursache in gawk/musl klärt M5. |
| Kernel nicht als Paket | usrkrnl ist absichtlich ein maschinenbezogener Bau (`usrkrnl/setup`), kein generisches Paket; das Abbild bekommt ihn direkt. |
| HTTPS mit BusyBox-wget | BusyBox-TLS prüft keine Zertifikate; für Downloads mit Prüfung nimmt rcyay/makepkg curl. |

## Quellen und Vertrauen
Alle Tarballs: HTTPS-Download, danach Vergleich mit der in `quellen` bzw. im PKGBUILD eingetragenen sha256;
Abweichung → Abbruch und Datei nach `*.falsch`. Der Linux-Tarball ist zusätzlich gegen kernel.orgs
`sha256sums.asc` verglichen. Arch-PKGBUILDs: sha256 der PKGBUILD-Datei in `stage2-liste` (die gitlab-Archive
selbst sind nicht bytegleich reproduzierbar). `SOURCE_DATE_EPOCH` ist fest gesetzt.
