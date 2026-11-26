/* Einheitstests für den Unit-Übersetzer (src/rcyay/unit.c) mit echten
 * Arch-Units aus tests/rcyay/dienste/units (Stand 26.11.2026). */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include "check.h"
#include "unit.h"

static const char *dir = ".";

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "r");
	char *buf = NULL;
	size_t len = 0;
	if(!f) return NULL;
	if(getdelim(&buf, &len, '\0', f) < 0) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

/* übersetzt units/FILE (unter fake-Pfad SYSDIR/FILE) */
static int tr(const char *file, const char *sysdir, rc_init_t init, rc_svout_t *o, char **why, alpm_list_t **notes)
{
	char *p = NULL, *fake = NULL, *text, *err = NULL;
	rc_unit_t *u;
	int r;
	if(asprintf(&p, "%s/%s", dir, file) < 0 || asprintf(&fake, "%s/%s", sysdir, file) < 0) return -9;
	text = slurp(p);
	free(p);
	if(!text) {
		printf("# %s fehlt\n", file);
		free(fake);
		return -9;
	}
	u = rc_unit_parse(fake, text, &err);
	free(text);
	free(fake);
	if(!u) {
		printf("# %s: %s\n", file, err);
		free(err);
		return -9;
	}
	*notes = NULL;
	r = rc_unit_translate(u, init, o, why, notes);
	rc_unit_free(u);
	return r;
}

#define SYS "/usr/lib/systemd/system"
#define HAS(s, x) ((s) && strstr((s), (x)) != NULL)

static void freenotes(alpm_list_t *n)
{
	alpm_list_free_inner(n, free);
	alpm_list_free(n);
}

static int notes_has(alpm_list_t *n, const char *x)
{
	for(; n; n = n->next) if(strstr(n->data, x)) return 1;
	return 0;
}

static rc_unit_t *P(const char *name, const char *text)
{
	char *err = NULL;
	rc_unit_t *u = rc_unit_parse(name, text, &err);
	if(!u) {
		printf("# %s\n", err);
		free(err);
	}
	return u;
}

static int T(const char *name, const char *text, rc_init_t init, rc_svout_t *o, char **why)
{
	alpm_list_t *notes = NULL;
	rc_unit_t *u = P(name, text);
	int r = u ? rc_unit_translate(u, init, o, why, &notes) : -9;
	rc_unit_free(u);
	freenotes(notes);
	return r;
}

int main(int argc, char *argv[])
{
	rc_svout_t o;
	char *why = NULL;
	alpm_list_t *notes = NULL;
	int r;
	const rc_init_t inits[] = { RC_INIT_BUSYBOX, RC_INIT_RUNIT, RC_INIT_S6 };

	if(argc > 1) dir = argv[1];

	CHECK(rc_init_parse("runit") == RC_INIT_RUNIT);
	CHECK(rc_init_parse("s6") == RC_INIT_S6);
	CHECK(rc_init_parse("busybox") == RC_INIT_BUSYBOX);
	CHECK(rc_init_parse("systemd") == RC_INIT_SYSTEMD);
	CHECK(rc_init_parse(NULL) == RC_INIT_NONE);
	CHECK((int)rc_init_parse("openrc") == -1);

	/* sshd: Type=notify-reload → überwacht, sshd -D im Vordergrund */
	for(int k = 0; k < 3; k++) {
		r = tr("sshd.service", SYS, inits[k], &o, &why, &notes);
		CHECK(r == 0 && o.kind == RC_SV_LONGRUN);
		CHECK(HAS(o.run, "\nexec /usr/bin/sshd -D\n"));
		CHECK(HAS(o.run, "#!/bin/sh\n") && HAS(o.run, "exec 2>&1\n"));
		CHECK(o.start == NULL && o.finish == NULL);
		rc_svout_free(&o);
		freenotes(notes);
		free(why);
	}

	/* sshdgenkeys: oneshot mit ODER-Bedingungen (|!) */
	r = tr("sshdgenkeys.service", SYS, RC_INIT_RUNIT, &o, &why, &notes);
	CHECK(r == 0 && o.kind == RC_SV_ONESHOT && o.run == NULL);
	CHECK(HAS(o.start, "! [ -e /etc/ssh/ssh_host_ed25519_key.pub ] || ! [ -e /etc/ssh/ssh_host_mldsa44_ed25519_key ]"));
	CHECK(HAS(o.start, "|| exit 0\n"));
	CHECK(HAS(o.start, "/usr/bin/ssh-keygen -A || exit $?\n"));
	rc_svout_free(&o);
	freenotes(notes);

	/* Vorlage sshd@ → übersprungen */
	r = tr("sshd@.service", SYS, RC_INIT_RUNIT, &o, &why, &notes);
	CHECK(r == 1 && HAS(why, "template unit"));
	free(why);
	freenotes(notes);

	/* dhcpcd: einfacher Dienst ohne Type */
	for(int k = 0; k < 3; k++) {
		r = tr("dhcpcd.service", SYS, inits[k], &o, &why, &notes);
		CHECK(r == 0 && HAS(o.run, "exec /usr/bin/dhcpcd -q -B\n"));
		rc_svout_free(&o);
		freenotes(notes);
	}
	/* dhcpcd@: Vorlage (forking mit PIDFile) */
	r = tr("dhcpcd@.service", SYS, RC_INIT_S6, &o, &why, &notes);
	CHECK(r == 1);
	free(why);
	freenotes(notes);

	/* seatd: ExecStart ohne Pfad, viel Abschottung (entfällt) */
	r = tr("seatd.service", SYS, RC_INIT_S6, &o, &why, &notes);
	CHECK(r == 0 && HAS(o.run, "umask 0077\n") && HAS(o.run, "exec seatd -g seat\n"));
	CHECK(!HAS(o.run, "Protect"));
	rc_svout_free(&o);
	freenotes(notes);

	/* NetworkManager: Type=dbus → Hinweis D-Bus, Warten auf dbus (BindsTo) */
	r = tr("NetworkManager.service", SYS, RC_INIT_RUNIT, &o, &why, &notes);
	CHECK(r == 0 && HAS(o.run, "exec /usr/bin/NetworkManager --no-daemon\n"));
	CHECK(notes_has(notes, "D-Bus"));
	CHECK(HAS(o.run, "sv check dbus") && o.needs && !strcmp(o.needs->data, "dbus"));
	CHECK(HAS(o.run, "ulimit -n 65536"));
	rc_svout_free(&o);
	freenotes(notes);
	r = tr("NetworkManager.service", SYS, RC_INIT_S6, &o, &why, &notes);
	CHECK(r == 0 && HAS(o.run, "s6-svwait -u -t 10000 /run/service/dbus"));
	rc_svout_free(&o);
	freenotes(notes);
	r = tr("NetworkManager.service", SYS, RC_INIT_BUSYBOX, &o, &why, &notes);
	CHECK(r == 0 && !HAS(o.run, "dbus ||"));
	rc_svout_free(&o);
	freenotes(notes);
	/* NetworkManager-dispatcher: statisch (nur D-Bus-Aktivierung) */
	r = tr("NetworkManager-dispatcher.service", SYS, RC_INIT_RUNIT, &o, &why, &notes);
	CHECK(r == 1 && HAS(why, "static unit"));
	free(why);
	freenotes(notes);
	/* NetworkManager-wait-online: oneshot */
	r = tr("NetworkManager-wait-online.service", SYS, RC_INIT_RUNIT, &o, &why, &notes);
	CHECK(r == 0 && o.kind == RC_SV_ONESHOT && HAS(o.start, "nm-online"));
	rc_svout_free(&o);
	freenotes(notes);

	/* pipewire als Nutzer-Unit → Hinweis */
	r = tr("pipewire.service", "/usr/lib/systemd/user", RC_INIT_RUNIT, &o, &why, &notes);
	CHECK(r == 1 && HAS(why, "user unit"));
	free(why);
	freenotes(notes);

	/* ---------- gebaute Fälle ---------- */
	r = T("a.service", "[Unit]\nDescription=A\n[Service]\nUser=nobody\nGroup=nogroup\n"
			"Environment=\"FOO=a b\" BAR=1\nEnvironmentFile=-/etc/conf.d/a\nWorkingDirectory=/var/lib/a\n"
			"RuntimeDirectory=a\nStateDirectory=a\nExecStartPre=-/bin/true\nExecStartPre=+/bin/mkdir -p /x\n"
			"ExecStart=/usr/bin/a --opt=$FOO ${BAR} $BAR 'q uoted' \\\n  --weiter\n[Install]\nWantedBy=multi-user.target\n",
			RC_INIT_RUNIT, &o, &why);
	CHECK(r == 0);
	CHECK(HAS(o.run, "export FOO='a b'\n") && HAS(o.run, "export BAR=1\n"));
	CHECK(HAS(o.run, "if [ -r /etc/conf.d/a ]; then set -a; . /etc/conf.d/a; set +a; fi\n"));
	CHECK(HAS(o.run, "cd /var/lib/a || exit 1\n"));
	CHECK(HAS(o.run, "mkdir -p /run/a\nchown nobody:nogroup /run/a\n"));
	CHECK(HAS(o.run, "mkdir -p /var/lib/a\n"));
	CHECK(HAS(o.run, "chpst -u nobody:nogroup /bin/true || true\n"));
	CHECK(HAS(o.run, "\n/bin/mkdir -p /x || exit $?\n"));  /* + = als root */
	CHECK(HAS(o.run, "exec chpst -u nobody:nogroup /usr/bin/a \"--opt=${FOO}\" \"${BAR}\" $BAR 'q uoted' --weiter\n"));
	rc_svout_free(&o);

	/* forking mit PIDFile → Beobachtung + finish mit ExecStop */
	r = T("f.service", "[Service]\nType=forking\nPIDFile=/run/f.pid\nExecStart=/usr/bin/f -d\n"
			"ExecStop=/usr/bin/f -k\n[Install]\nWantedBy=multi-user.target\n", RC_INIT_S6, &o, &why);
	CHECK(r == 0 && HAS(o.run, "/usr/bin/f -d || exit $?\n") && HAS(o.run, "pf=/run/f.pid\n"));
	CHECK(HAS(o.run, "while kill -0 \"$pid\""));
	CHECK(HAS(o.finish, "/usr/bin/f -k") && HAS(o.finish, "kill \"$(cat \"$pf\")\""));
	rc_svout_free(&o);

	/* nicht übersetzbar */
	r = T("g.service", "[Service]\nType=forking\nExecStart=/usr/bin/g\n[Install]\nWantedBy=x\n", RC_INIT_RUNIT, &o, &why);
	CHECK(r == -1 && HAS(why, "without PIDFile") && HAS(why, "overlay/dienste/g/"));
	free(why);
	r = T("h.service", "[Service]\nExecStart=@/usr/bin/h hh\n[Install]\nWantedBy=x\n", RC_INIT_RUNIT, &o, &why);
	CHECK(r == -1 && HAS(why, "'@'"));
	free(why);
	r = T("i.service", "[Service]\nExecStart=/usr/bin/i %U\n[Install]\nWantedBy=x\n", RC_INIT_RUNIT, &o, &why);
	CHECK(r == -1 && HAS(why, "specifier %U"));
	free(why);
	r = T("j.service", "[Service]\nExecStart=/a\nExecStart=/b\n[Install]\nWantedBy=x\n", RC_INIT_RUNIT, &o, &why);
	CHECK(r == -1 && HAS(why, "several ExecStart"));
	free(why);
	r = T("k.service", "[Service]\nType=socket-ish\nExecStart=/a\n[Install]\nWantedBy=x\n", RC_INIT_RUNIT, &o, &why);
	CHECK(r == -1 && HAS(why, "Type=socket-ish"));
	free(why);

	/* Spezifizierer, leere Zuweisung setzt Liste zurück, $$ */
	r = T("sp.service", "[Service]\nExecStartPre=/x\nExecStartPre=\nExecStart=/usr/bin/sp %n %N %t/%p 50%% $$HOME\n"
			"[Install]\nWantedBy=x\n", RC_INIT_BUSYBOX, &o, &why);
	CHECK(r == 0 && HAS(o.run, "exec /usr/bin/sp sp.service sp /run/sp 50% \"\\$HOME\"\n"));
	CHECK(!HAS(o.run, "\n/x"));
	rc_svout_free(&o);

	/* Bedingung bei Langläufer: still statt Neustart-Schleife */
	r = T("c.service", "[Unit]\nConditionPathExists=/etc/c.conf\n[Service]\nExecStart=/c\n[Install]\nWantedBy=x\n",
			RC_INIT_RUNIT, &o, &why);
	CHECK(r == 0 && HAS(o.run, "if ! { [ -e /etc/c.conf ]; }; then") && HAS(o.run, "exec sleep 2147483647"));
	rc_svout_free(&o);

	/* Parserfehler */
	{
		char *err = NULL;
		CHECK(rc_unit_parse("x.service", "Key=1\n", &err) == NULL && HAS(err, "outside a section"));
		free(err);
		CHECK(rc_unit_parse("x.service", "[Service\n", &err) == NULL);
		free(err);
		CHECK(rc_unit_parse("x.socket", "[Socket]\n", &err) == NULL && HAS(err, "not a .service"));
		free(err);
	}
	return CHECK_DONE();
}
