# rcyay – Plan (Projektleitung, 26.11.2026)

rcyay („really cool yet another yaourt“) ist ein Fork von pacman 7.1.0 (Basis-Commit `v7.1.0`,
Remote `upstream` = gitlab.archlinux.org/pacman/pacman). Er kann alles wie yay (Arch-Repos + AUR) und ist
der Paketmanager der eigenständigen Distribution **UsrsideOS**: BusyBox-Userland, usrkrnl ohne Initramfs,
zwei Varianten **glibc** (Arch-Binärpakete; Enzos Laptop: NVIDIA proprietär, Steam, Signal, Zotero) und
**musl** (alles aus Quellen).

## 1. Architektur

### 1.1 Programm
- **C, im pacman-Baum.** libalpm bleibt (fast) unverändert, damit Upstream-Updates leicht einfließen
  (`git merge v7.x`). Neues steckt in eigenen Dateien: `src/rcyay/` (Oberfläche) und `lib/libaur/`
  (AUR-Bibliothek). Binärname `rcyay`; `pacman` und `makepkg` werden weiter mitgebaut (makepkg braucht bash).
- **Bedienung wie yay/pacman:** alle pacman-Operationen (`-S -R -Q -U -F -D -T`) unverändert; zusätzlich
  - `-S paket` sucht erst in den Repos, dann im AUR; `-Ss/-Si` durchsucht beides (AUR mit `aur/`-Präfix),
  - `-Syu` aktualisiert Repos und danach AUR-Pakete (`-Sua` nur AUR, `--devel` für -git-Pakete),
  - `-G paket` holt den Bauordner, `-Gp` zeigt PKGBUILD, `--repo`/`--aur` schränkt ein,
  - `rcyay` ohne Argumente = `-Syu`, `rcyay suchwort` = interaktive Auswahl wie yay,
  - `-Yc` entfernt unnötige Abhängigkeiten, `-Ps` Statistik.
- **AUR-Zugriff ohne git:** RPC v5 (`/rpc/v5/info`, `/search`) über libcurl (pacman hat es schon), kleiner
  JSON-Parser in `lib/libaur` (keine neue Abhängigkeit). Quellen als Snapshot
  `https://aur.archlinux.org/cgit/aur.git/snapshot/NAME.tar.gz` (libarchive entpackt); `git` nur optional
  für Diffs zwischen Versionen. Bauordner `~/.cache/rcyay/NAME`.
- **Abhängigkeiten:** `.SRCINFO` parsen (eigener Parser), Graph aus Repo-Paketen (über alpm) + AUR-Paketen
  (über RPC, auch provides), Zyklen erkennen, topologisch bauen; makedepends nach dem Bau wieder entfernen
  (`--removemake`, Standard ja). Bau mit `makepkg` als normaler Nutzer (nie als root; root → Nutzer `rcyay-bau`).
- **Sicherheit:** vor dem Bau PKGBUILD/Diff zeigen (abschaltbar `--noconfirm`), Prüfsummen von makepkg,
  Signaturprüfung der Repo-Pakete bleibt wie pacman.
- **Konfiguration:** `/etc/pacman.conf` bleibt gültig (Kompatibilität), zusätzlich Abschnitt `[rcyay]`
  bzw. `/etc/rcyay.conf` (AUR-URL, Bauordner, Variante, Patch-Overlay).

### 1.2 Varianten
| | glibc | musl |
|---|---|---|
| `Architecture` | `x86_64` | `x86_64-musl` (eigene Arch-Kennung, damit nie glibc-Binärpakete landen) |
| Repo-Pakete | Arch `core/extra/multilib` binär + Repo `usrside` | aus Arch-PKGBUILDs (gitlab.archlinux.org/archlinux/packaging) selbst gebaut, lokales Repo `usrside-musl` |
| AUR | wie yay | Quell-Build mit Patch-Overlay; `-bin`-Pakete → „nur glibc“ |
| NVIDIA/Steam | ja (Arch-Pakete) | nein |

- **Patch-Overlay nach Alpine/Chimera-Art:** `overlay/NAME/` im rcyay-Repo (bzw. `/usr/share/rcyay/overlay`):
  `*.patch` (werden vor `prepare()` angewendet), optional `PKGBUILD.sed`/`override.sh` (Variablen ändern,
  z. B. `depends` ohne systemd, `--disable-systemd`). Bekannte Fälle (`-bin`, glibc-Symbole, systemd zwingend)
  stehen in `overlay/nur-glibc.txt`; rcyay meldet sie auf musl klar statt zu scheitern.
- **systemd-Abhängigkeiten (Artix-Vorbild):** glibc-Variante nutzt Arch-Binärpakete unverändert;
  `systemd-libs` ist nur eine Bibliothek und darf bleiben. Für Nicht-systemd-Inits liefert `usrside`
  Ersatz/Brücken, wo nötig (elogind, seatd, eudev bzw. mdev). musl: Pakete werden ohne systemd konfiguriert.
- **BusyBox-Userland:** BusyBox stellt Kernwerkzeuge. Über die neue Konfiguration
  `VirtualProvides = coreutils findutils …` (rcyay, nicht in libalpm-Datenbank) kann das System erklären,
  dass BusyBox Abhängigkeiten erfüllt; Standard ist **aus** für alles, wo GNU-Optionen gebraucht werden.
  Tatsächlich installiert: bash (makepkg), sonst BusyBox. Welche Arch-Abhängigkeiten sicher durch BusyBox
  ersetzt werden, klärt M4 mit Tests.

### 1.3 Init-neutral (systemd, BusyBox init, runit, s6)
- Paket `usrside-init` + alpm-Hook `rcyay-dienste`: nach jeder Installation werden systemd-Units der Pakete
  (`/usr/lib/systemd/system/*.service`, einfache Fälle `Type=simple|exec|forking|oneshot`, `ExecStart`,
  `User`, `Environment`) in das aktive Init übersetzt: runit `/etc/sv/NAME/run`, s6 `/etc/s6/sv/NAME/{run,type}`,
  BusyBox `/etc/inittab`-Respawn bzw. `/etc/init.d/NAME`. Nicht übersetzbare Units → Warnung + eigene
  Dienstdatei in `overlay/dienste/NAME/`.
- Aktives Init steht in `/etc/rcyay.conf` (`Init = runit`), umschaltbar mit `rcyay --init s6` (erzeugt alle
  Dienste neu). Bei systemd passiert nichts (Units gelten direkt).

### 1.4 Bootstrap (aus Quellen)
`bootstrap/` – reine Shell-Skripte, laufen in einer Arch-VM als Bauwirt:
1. **stage0:** Kreuz-Toolchain (binutils, gcc, musl bzw. glibc, Linux-Header) nach `/opt/usrside-tc`.
2. **stage1:** in ein Sysroot: busybox, bash, make, libarchive, zlib, openssl, curl, gpgme-Kette, rcyay,
   fakeroot, patch, Toolchain nativ.
3. **stage2:** chroot ins Sysroot, rcyay baut die Basis aus Arch-PKGBUILDs + Overlay noch einmal mit sich selbst
   (reproduzierbar, Paketdatenbank korrekt). Ergebnis: Repo `usrside-musl` bzw. `usrside`.
4. **Abbild:** `bootstrap/abbild` erzeugt qcow2 (btrfs wie UsrsideOS) mit usrkrnl ohne Initramfs, Init wählbar.
glibc-Variante: stage0–1 genauso (glibc selbst gebaut), danach Arch-Binärpakete für den Rest erlaubt.

### 1.5 Repo-Aufbau
```
lib/libalpm   (upstream)        lib/libaur      AUR-RPC, JSON, SRCINFO, Graph
src/pacman    (upstream)        src/rcyay       Oberfläche (nutzt src/pacman-Code wieder)
scripts/      makepkg (upstream, nur kleine Erweiterungen: Overlay-Patches, Arch x86_64-musl)
overlay/      Patches + nur-glibc.txt + dienste/      bootstrap/  stage0..2, abbild
tests/rcyay/  eigene Tests (Mock-AUR, SRCINFO, Graph)  pkg/        PKGBUILDs für usrside-Repo
```

## 2. Meilensteine
| M | Inhalt | fertig wenn |
|---|---|---|
| M0 | Gerüst: Build `rcyay` (meson), Grundgerüst `src/rcyay`, `lib/libaur`, Testskripte in ~/vm | pacman-Testsuite grün in Arch-VM, `rcyay -V` |
| M1 | AUR: -Ss/-Si/-S/-G, SRCINFO, Graph, Bau, -Syu/-Sua, --devel, Diff-Anzeige | Mock-AUR-Tests + echtes AUR (z. B. `yay-bin`, `paru`, Kette mit AUR→AUR-Abh.) in VM grün |
| M2 | Init-neutral: `rcyay-dienste` für 4 Inits | sshd/dhcpcd/seatd laufen in VM mit jedem Init |
| M3 | Bootstrap musl → bootfähiges Abbild (usrkrnl, BusyBox, rcyay) | VM bootet ohne Initramfs mit allen 4 Inits, `rcyay -Syu` funktioniert |
| M4 | glibc-Variante: Abbild + Arch-Repos, BusyBox-Userland, VirtualProvides | VM bootet, Arch-Pakete + Steam/nvidia-utils installierbar, niri läuft |
| M5 | musl-Quellbuilds: Overlay, nur-glibc, Basis + Desktop (niri, foot/kitty, pipewire) | Liste der Pakete baut in VM |
| M6 | UsrsideOS-Installer `os/usrside` (glibc/musl) in ~/usrsideos | `test-install usrside 1` grün, Laptop-Profil (NVIDIA+TUXEDO) grün |

Reihenfolge: M0 → (M1 ‖ M3-Vorarbeit stage0/1) → M2 → M3 → M4 → M5 → M6.

## 3. Testplan (nur in VMs, ~/vm)
- **Einheit:** `meson test` in der Arch-VM: pacman-Testsuite (pactest, muss grün bleiben) + `tests/rcyay`
  (SRCINFO-Parser, JSON, Versionsvergleich, Graph mit Zyklen/provides/split-Paketen).
- **Mock-AUR:** kleiner HTTP-Server (python) in der VM mit RPC + Snapshot-Tarballs aus `tests/rcyay/aur/`
  → reproduzierbare Ende-zu-Ende-Tests (Installieren, Update, Abh.-Ketten, Fehlerfälle, Prüfsummenfehler).
- **Echtes AUR:** Rauchtest mit wenigen kleinen Paketen (nur lesend + Bau in VM).
- **Abbilder:** `~/vm/test-rcyay VARIANTE INIT` bootet das erzeugte qcow2 (direkter Kernelstart,
  serielle Konsole, SSH Port 2222 mit id_vm), prüft Init, Dienste, `rcyay -Syu`, Installieren aus Repo + AUR.
- Jeder Meilenstein: Serienskript `~/vm/serie-rcyay-HASH.sh` mit HEAD-Schutz, Logs in `~/vm/logs/rcyay-*/`.
- Tests laufen nie auf dem Laptop selbst (nur Bauen/Lesen des Quelltexts dort).

## 4. Organisation
- Leitung (diese Instanz), Entwickler `rcyay-e1` (M0/M1/M2, Programm), `rcyay-e2` (Bootstrap M3),
  Tester `rcyay-t1` (Testgerüst in ~/vm, prüft jeden Meilenstein unabhängig). Start per
  `claude --bg --permission-mode auto -n NAME "AUFTRAG"` in ~/usrsideos, Arbeitsordner ~/rcyay.
- Jede Instanz committet nur ihre eigenen Ordner (Autor Lord3n20, s. usrsideos.md), kein Push.
- Stand im Infoschirm über `projektstand`; Übergabe in `uebergabe-rcyay.md`.
