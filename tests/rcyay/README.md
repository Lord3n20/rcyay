# Tests für rcyay (PLAN.md 3)

## Einheitstests (libaur)
`meson test -C BUILD --suite rcyay` – `unit/test_*.c`: JSON-Parser, .SRCINFO (split, `*_ARCH`,
Überschreiben im Paketabschnitt, Fehler), RPC-Antworten, Abhängigkeiten/Versionen, sicheres Entpacken,
Abhängigkeitsgraph (Ketten, provides mit Version, split-Pakete, Zyklen, fehlende Abhängigkeiten,
Bau-/Laufzeitabhängigkeiten). Die ganze pacman-Suite als normaler Nutzer laufen lassen
(als root schlagen hook-/ldconfig-/scriptlet-Tests auch auf unverändertem pacman fehl).

## Ende-zu-Ende gegen das Mock-AUR
`aur/run-tests PFAD/ZU/rcyay [NUTZER]` – **nur in einer Wegwerf-VM als root**.
- `aur/server.py`: Mock-AUR (RPC v5 info/search, cgit-Snapshots und -plain, gitlab-Archiv für `-G`).
- `aur/pkgs/`: AUR-Testpakete (rc-*), `aur/pkgs-v2/`: neuere Fassungen für `-Sua`,
  `aur/repo/`: Pakete für ein lokales Repo `rcyaytest` (Repo-Abhängigkeiten).
- `.SRCINFO` erzeugt das Skript mit `makepkg --printsrcinfo` (als nobody).
- Geprüft: Suche/Info/Sortierung, Installation als root (Bau-Nutzer rcyay-bau) und als Nutzer (sudo),
  AUR→AUR→provides-Ketten, Repo-Abhängigkeiten, Entfernen der Bauabhängigkeiten, split-Pakete, Zyklen,
  fehlende Abhängigkeiten, Prüfsummenfehler, `--needed`, `-Sua`, `--devel`, Diff-/PKGBUILD-Anzeige mit
  Abbruch, `-G`/`-Gp`, `-Yc`, `-Ps`, interaktive Auswahl.
- Seit 26.11. außerdem: validpgpkeys (Mock-HKP-Schlüsselserver, `--nopgpfetch`, Rückfrage, `--noconfirm`),
  Waisen nach gescheitertem Bau, PATH der Bauumgebung (pod2man/core_perl), deutsches Locale (de_DE.UTF-8
  per `locale-gen`, sonst SKIP), musl-Regeln (`x86_64_musl`, `--ignorearch`, „nur glibc“, `--force-musl`).

## rcyay-dienste (Init-neutral, M2)
- Einheitstest `unit/test_unit.c` (in `meson test`): Übersetzer mit echten Arch-Units aus `dienste/units/`
  (sshd, sshd@, sshdgenkeys, dhcpcd, dhcpcd@, seatd, NetworkManager[-dispatcher/-wait-online], pipewire
  als Nutzer-Unit) für busybox/runit/s6 und gebaute Grenzfälle (Quoting, `$VAR`, Spezifizierer, forking,
  Fehlerfälle).
- `dienste/run-tests PFAD/ZU/rcyay` – Dateisystem mit `--root` (ohne root-Rechte, ohne laufendes Init):
  `--init`, `--enable/--disable`, Hook über stdin, Overlay, Paket-Dienst `/etc/sv`, Umschalten
  runit → s6 → busybox → systemd, inittab-Block.
- `dienste/e2e-chroot BAUORDNER BOOTSKRIPTE` – **nur Wegwerf-VM als root**: Arch-chroot (pacman -r) mit
  s6 aus dem AUR (mit rcyay gebaut) und den runit-Applets von BusyBox; jedes Init läuft als PID 1 in
  einem PID-/Mount-/Netz-Namensraum mit den Startskripten von usrside-dienste (rcyay-e2). Geprüft:
  sshd/dhcpcd/seatd laufen unter runsv / s6-supervise / BusyBox init, Neustart nach kill, sshd antwortet,
  Einmal-Dienst über start.d, Hook beim Installieren/Entfernen von networkmanager.
  In bau-e1: `cp ~/rcyay-bootstrap/bootstrap/pakete/usrside-dienste/dateien/* ~/vm/rcyay/vms/bau-e1/aus/boot/`,
  dann `rvm bau-e1 ssh 'bash -lc "/root/rcyay/tests/rcyay/dienste/e2e-chroot /home/bau/b /root/aus/boot"'`.
