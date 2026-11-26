# UsrsideOS glibc-Variante (M4) – Aufbau und Entscheidungen

Stand 26.11.2026, rcyay-e1d. Laptop von Enzo = glibc, **kein Flatpak/Snap** (alles nativ über rcyay).

## Bauen

```
bootstrap/glibc-rootfs [ZIEL]                 # in einer Arch-Bau-VM als root (bau-e1)
VARIANTE=glibc bootstrap/abbild --rootfs ZIEL # qcow2 + Kernel (rcyay-e2, Branch bootstrap)
~/vm/rcyay/test-rcyay glibc systemd|busybox|runit|s6
```

`glibc-rootfs` baut das Repo `usrside` (pkg/ dieses Baums + runit/skalibs/execline/s6/s6-linux-init/usrside-dienste
aus `bootstrap/pakete` von rcyay-e2, nur `depends` musl → glibc) und installiert mit rcyay per `--root`:
Arch `core`/`extra`/`multilib` (Binärpakete) + `usrside`. Ergebnis: vollständiges `/` ohne Kernel, fstab,
hostname und root-Schlüssel – das ergänzt `abbild --rootfs`. Init-Pfade stehen in `/usr/share/usrside/inits`.
Zum Testen ohne Kernelbau: `tests/rcyay/glibc/test-abbild ROOTFS KERNEL.tar AUS` (bildet den abbild-Vertrag nach).

## systemd ohne systemd als Init

`pactree -r systemd`: in Arch hängen u. a. **pacman**, polkit, pulseaudio, xdg-desktop-portal, mkinitcpio, cups,
gvfs, greetd, swayidle an `systemd` (nicht nur `systemd-libs`). Damit ist `systemd` auf einem Arch-Binärsystem
praktisch immer installiert. Entscheidung (Zuverlässigkeit vor Reinheit, Artix-Vorbild nur, wo nötig):

- Das Paket `systemd` bleibt installiert, ist aber bei Init = busybox|runit|s6 **nicht PID 1**, sondern Werkzeugkasten:
  - `systemd-sysusers`/`systemd-tmpfiles` laufen weiter als alpm-Hooks bei jeder Installation (Nutzer wie `alpm`,
    `dhcpcd`, `seat`-Gruppe entstehen wie unter Arch),
  - `systemd-udevd`/`udevadm` sind der Gerätemanager (libinput, mesa, seatd, niri brauchen die udev-Datenbank;
    mdev reicht dafür nicht).
- `usrside-udev` (pkg/): startet beim Booten über `/etc/usrside/start.d` (rc.start von usrside-dienste)
  `systemd-tmpfiles --create --boot` und `systemd-udevd --daemon` + `udevadm trigger/settle` statt mdev.
  Unter systemd tut es nichts (`/run/systemd/system`).
- Sitzungen: **seatd** (Arch `extra`) statt logind; rcyay-dienste übersetzt `seatd.service` für jedes Init.
  elogind gibt es in Arch nicht; erst nötig, wenn ein Paket logind-D-Bus zwingend braucht (dann AUR/usrside).
- Nicht per VirtualProvides ersetzt: `systemd` selbst. Ein „provides=systemd“ wäre nicht ABI-frei (Pakete rufen
  `systemd-sysusers`, `systemd-tmpfiles`, `udevadm`, `systemctl --root` in Hooks/Skripten auf).
- systemd-Hooks wie `daemon-reload` prüfen selbst, ob systemd läuft, und überspringen sich sonst.

## BusyBox auf glibc

`pacman` (Arch) hängt an `coreutils gawk grep`, `systemd` an `util-linux`, `base-devel` an `sed findutils …`.
Ergebnis mit Profil **standard**: GNU-Werkzeuge sind installiert und besitzen ihre Pfade; BusyBox (usrside-busybox)
belegt alle *freien* Pfade (z. B. `which`, `init`, `getty`, `mdev`, `killall5`) und steht vollständig unter
`/usr/lib/busybox/bin`.

| Profil | VirtualProvides | Folge |
|---|---|---|
| standard (Standard) | `which` | GNU coreutils/findutils/grep/sed/gawk/util-linux bleiben (makepkg, Paketskripte, AUR sicher) |
| minimal (Option, `PROFIL=minimal`) | `which coreutils findutils grep sed gawk diffutils` | BusyBox statt GNU; makepkg und manche Paketskripte (GNU-Optionen) können scheitern |

`sh` braucht keinen Eintrag (Arch-bash stellt `sh` bereit, bash bleibt für makepkg). `inetutils` (hostname, ftp,
telnet …) zieht kein Grundpaket; `hostname` kommt bei Bedarf von BusyBox über den freien Pfad.
`base-devel` gehört zur glibc-Grundinstallation (wie bei yay Voraussetzung für AUR-Bauten: fakeroot, debugedit).
`wget` (GNU) ist dabei, weil BusyBox-wget in Arch kein HTTPS kann.

## Dienste je Init

| Init | init= | sshd, dhcpcd, getty | seatd |
|---|---|---|---|
| systemd | /usr/lib/systemd/systemd | `systemctl enable sshd dhcpcd` (getty über den Generator) | `seatd.service` |
| busybox | /usr/bin/init (BusyBox) | /etc/inittab von usrside-dienste | inittab-Block von rcyay-dienste |
| runit | /usr/bin/runit-init | /etc/sv + /etc/runit/runsvdir/default | /etc/rcyay/dienste/seatd |
| s6 | /etc/s6-linux-init/current/bin/init | run-image/service | run-image/service/seatd |

`glibc-rootfs` schaltet seatd mit je einer rcyay.conf pro Init ein (`rcyay --services enable seatd`), damit ein
Abbild alle vier Inits kann; `Init =` in /etc/rcyay.conf (Standard runit) bestimmt, wofür spätere Pakete übersetzt werden.
