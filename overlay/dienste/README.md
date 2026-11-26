# overlay/dienste – eigene Dienste für rcyay-dienste

`rcyay-dienste` (Hook aus `pkg/usrside-init`, `rcyay --services …`) übersetzt die systemd-Units der Pakete
für das aktive Init (`Init =` in `/etc/rcyay.conf`: `busybox`, `runit`, `s6`; bei `systemd` passiert nichts).
Lässt sich eine Unit nicht übersetzen, warnt rcyay und verweist hierher.

## Eigener Dienst
`overlay/dienste/NAME/` (installiert nach `/usr/share/rcyay/overlay/dienste/NAME/`, Vorrang hat
`/etc/rcyay/overlay/dienste/NAME/` für eigene Anpassungen) mit
- `run` – Langläufer im Vordergrund (`exec programm …`), gilt für runit, s6 und BusyBox init,
- optional `finish` (runit/s6, beim Stoppen),
- oder `start` – Einmal-Dienst (Type=oneshot), läuft beim Systemstart über `/etc/usrside/start.d`.

Liefert ein Paket selbst `/etc/sv/NAME/run` (z. B. `usrside-dienste`), wird dieser Dienst genommen.

## Wohin rcyay die Dienste legt
| | Dienstordner | eingeschaltet |
|---|---|---|
| alle | `/etc/rcyay/dienste/NAME/` (erzeugt) bzw. `/etc/sv/NAME/` (Paket) | Liste `/etc/rcyay/dienste.an` |
| runit | – | Link `/etc/runit/runsvdir/default/NAME` |
| s6 | – | Kopie in `/etc/s6-linux-init/current/run-image/service/NAME` (+ live in `/run/service`) |
| BusyBox init | – | `::respawn:ORDNER/run` im Block `rcyay-dienste` der `/etc/inittab` (SIGHUP an PID 1) |
| Einmal-Dienste | – | `/etc/usrside/start.d/rcyay-NAME.sh` (von `rc.start` gelesen, alle drei Inits) |

`rcyay --enable/--disable NAME`, `rcyay --services list`, `rcyay --init X` (schaltet alles um und erzeugt
neu), `rcyay --services apply` (alles Eingeschaltete erneut eintragen, z. B. nach dem Neuaufbau von
s6-linux-init). Beim Übersetzen wird `systemctl enable` übernommen (`/etc/systemd/system/*.wants/`).

## s6: Dienstordner statt s6-rc (Entscheidung rcyay-e1b, 26.11.2026)
s6 bekommt einfache s6-svscan-Dienstordner (`run`, `finish`), kein s6-rc:
- dasselbe `run` wie bei runit und BusyBox init – ein Übersetzer, ein Overlay-Format für alle drei Inits;
- passt zum Abbild von rcyay-e2 (s6-linux-init mit `run-image/service`, `rc.init` startet die Dienste);
- kein Übersetzungsschritt (`s6-rc-compile`, Datenbank-Wechsel mit `s6-rc-update`) bei jeder Paket-
  installation, Dienste kommen live mit `s6-svscanctl -a` hinzu;
- was s6-rc zusätzlich böte, deckt rcyay einfacher ab: Einmal-Dienste über `start.d` vor den Langläufern,
  Abhängigkeiten (`Requires=`/`BindsTo=`) als Warten im `run` (`s6-svwait -u`, bei runit `sv check`) –
  der Supervisor startet neu, bis die Abhängigkeit läuft.

## Was übersetzt wird
`Type=simple|exec|notify|notify-reload|idle|dbus` (Langläufer), `forking` mit `PIDFile=` (Beobachtung über
die PID-Datei + `finish`), `oneshot`; `ExecStartPre/ExecStart(/ExecStartPost bei oneshot)` mit den
Präfixen `- + ! :`, `User=/Group=` (chpst – runit bzw. BusyBox-Applet), `Environment=`, `EnvironmentFile=`,
`WorkingDirectory=`, `UMask=`, `LimitNOFILE=`, `Runtime-/State-/Cache-/Logs-/ConfigurationDirectory=`,
`ConditionPathExists=` u. ä. (auch `|` und `!`), Spezifizierer `%n %N %p %i %t %S %C %L %E %T %V %u %h %%`.
Abschottung (`Protect*`, `Private*`, `SystemCall*`, `Capability*` …) entfällt ohne systemd.

Nicht übersetzt (Hinweis): Nutzer-Units (`/usr/lib/systemd/user`, z. B. pipewire → in der Sitzung starten),
Vorlagen `NAME@.service`, statische Units ohne `[Install]` (werden von anderen Units/D-Bus gestartet).
Nicht übersetzbar (Warnung → eigener Dienst hier): `forking` ohne `PIDFile=`, mehrere `ExecStart=` bei
Langläufern, Präfix `@`, unbekannte Spezifizierer, `RootDirectory=`, andere `Type=`.
