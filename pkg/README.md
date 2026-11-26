# PKGBUILDs für das Repo usrside

| Ordner | Paket | Inhalt |
|---|---|---|
| rcyay | rcyay | Programm aus diesem Baum (libalpm statisch), Overlay, Übersetzungen; pacman/makepkg bleiben vom Paket pacman |
| usrside-init | usrside-init | /etc/rcyay.conf (backup) + alpm-Hooks für rcyay-dienste |
| usrside-busybox | usrside-busybox | /usr/bin-Links auf busybox nur für Applets ohne Besitzer (pacman -F + lokale DB), alle Applets in /usr/lib/busybox/bin |
| usrside-base | usrside-base | Metapaket, Variante über arch: x86_64 (glibc) / x86_64_musl (musl) |

Bauen (in der Bau-VM, als Nutzer): `cd pkg/NAME && makepkg` (Quellbaum: `RCYAY_SRC=PFAD`, Standard `../..`);
für usrside-busybox vorher einmal `pacman -Fy` als root. Repo: `repo-add usrside.db.tar.gz *.pkg.tar.zst`.
Test: `tests/rcyay/pkg/install-chroot` (frisches Arch-chroot mit pacman -r).
