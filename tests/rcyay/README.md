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
