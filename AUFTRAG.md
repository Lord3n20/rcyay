# Auftrag rcyay (Leitung, Start 26.11.2026)

Du bist die **Projektleitung rcyay**. Lies zuerst die Memory-Dateien
`~/.claude/projects/-home-enzo/memory/plan-usrside-paketmanager.md` (Ziel + Entscheidungen von Enzo)
und `usrsideos.md` (Testumgebung ~/vm, Kniffe, Instanz-Organisation).

## Ziel
rcyay = Fork von pacman, kann alles wie yay (Arch-Repos + AUR: PKGBUILDs bauen, Abhängigkeiten, Updates),
damit UsrsideOS eine eigene Distro wird: BusyBox-Userland + usrkrnl (ohne Initramfs), zwei Varianten
**glibc** (Arch-Binärpakete, NVIDIA proprietär, Steam …; Variante für Enzos Laptop) und **musl** (Quell-Builds).

## Entscheidungen Enzo (fest)
- Init: systemd, BusyBox init, runit und s6 müssen alle funktionieren.
- Paketquellen: Arch-Repos.
- Bootstrap: aus Quellen selbst kompilieren.
- AUR/Pakete mit glibc-/systemd-Annahmen auf musl: selbst entscheiden (Vorschlag: auf musl aus Quellen mit
  Patches nach Alpine/Chimera-Art; was nicht geht, als „nur glibc“ markieren).

## Regeln
- Enzo nicht fragen, alles selbst entscheiden; gut testen (nur in VMs, ~/vm), keine Fehler.
- Erster Schritt: PLAN.md in diesem Repo (Architektur, Meilensteine, Testplan), dann Gerüst.
- Organisation wie bei UsrsideOS (Entwickler/Tester-Instanzen per `claude --bg --permission-mode auto`
  im vertrauten Ordner ~/usrsideos starten; Neustart > 150k Tokens mit Übergabe), fertige Instanzen beenden.
- Stand im Infoschirm: `projektstand schritt|rolle|aufgabe|notiz|name` (~/.local/bin), am Ende `projektstand ende`.
- Übergabe-Memory `uebergabe-rcyay.md` (+ eine Zeile in MEMORY.md) aktuell halten, klein.
- Push immer erlaubt (Enzo 26.11.); Repo github.com/Lord3n20/rcyay (privat). Commit-Autor siehe usrsideos.md.
- Arbeitsordner ist /home/enzo/rcyay (du startest in ~/usrsideos, weil der Ordner vertraut ist).
