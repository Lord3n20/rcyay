/*
 *  dienste.c - rcyay-dienste: systemd-Units der Pakete für das aktive Init
 *              (BusyBox init, runit, s6) bereitstellen (PLAN.md 1.3)
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Aufbau (alle Pfade unter --root):
 *    /etc/rcyay/dienste/NAME/{run,finish,start}  erzeugt (Übersetzung oder Overlay)
 *    /etc/sv/NAME/run                            von einem Paket geliefert → wird genutzt
 *    /etc/rcyay/dienste.an                       eingeschaltete Dienste (unabhängig vom Init)
 *  Einschalten je Init:
 *    runit   /etc/runit/runsvdir/default/NAME → Dienstordner
 *    s6      Kopie in /etc/s6-linux-init/current/run-image/service/NAME (+ live /run/service)
 *    busybox Zeile ::respawn:ORDNER/run im Block "rcyay-dienste" von /etc/inittab
 *    Einmal-Dienste (Type=oneshot) bei allen drei: /etc/usrside/start.d/rcyay-NAME.sh
 *  Overlays: /etc/rcyay/overlay/dienste/NAME/ vor /usr/share/rcyay/overlay/dienste/NAME/
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <glob.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <alpm.h>
#include "config.h"
#include "util.h"
#include "rcyay.h"
#include "unit.h"

#define GEN_DIR     "/etc/rcyay/dienste"
#define AN_FILE     "/etc/rcyay/dienste.an"
#define PKG_SV      "/etc/sv"
#define OVL_ETC     "/etc/rcyay/overlay/dienste"
#define OVL_USR     "/usr/share/rcyay/overlay/dienste"
#define RUNIT_DIR   "/etc/runit/runsvdir/default"
#define S6_IMAGE    "/etc/s6-linux-init/current/run-image/service"
#define S6_LIVE     "/run/service"
#define START_D     "/etc/usrside/start.d"
#define INITTAB     "/etc/inittab"
#define TAB_BEGIN   "# rcyay-dienste: begin (generated, do not edit inside)"
#define TAB_END     "# rcyay-dienste: end"

static const char *root = "";
static int live;        /* root ist "/" → laufendes Init benachrichtigen */
static rc_init_t init;

static char *P(const char *fmt_, ...) __attribute__((format(printf, 1, 2)));
static char *P(const char *fmt_, ...)
{
	char *a = NULL, *r = NULL;
	va_list ap;
	va_start(ap, fmt_);
	if(vasprintf(&a, fmt_, ap) < 0) a = NULL;
	va_end(ap);
	if(!a) return NULL;
	if(asprintf(&r, "%s%s", root, a) < 0) r = NULL;
	free(a);
	return r;
}

static int exists(const char *p)
{
	struct stat st;
	return p && lstat(p, &st) == 0;
}

static char *read_file(const char *path)
{
	FILE *f = fopen(path, "r");
	char *buf = NULL;
	size_t len = 0;
	if(!f) return NULL;
	if(getdelim(&buf, &len, '\0', f) < 0) {
		free(buf);
		buf = strdup("");
	}
	fclose(f);
	return buf;
}

/* atomar schreiben (Datei.neu → rename); unverändert = nichts tun */
static int write_file(const char *path, const char *text, mode_t mode)
{
	char *tmp = NULL, *old = read_file(path);
	FILE *f;
	int same = old && strcmp(old, text) == 0;
	free(old);
	if(same) {
		chmod(path, mode);
		return 0;
	}
	if(asprintf(&tmp, "%s.rcyay-neu", path) < 0) return -1;
	f = fopen(tmp, "w");
	if(!f || fputs(text, f) == EOF || fclose(f) != 0) {
		rc_err("%s: %s\n", tmp, strerror(errno));
		free(tmp);
		return -1;
	}
	chmod(tmp, mode);
	if(rename(tmp, path) != 0) {
		rc_err("%s: %s\n", path, strerror(errno));
		unlink(tmp);
		free(tmp);
		return -1;
	}
	free(tmp);
	return 0;
}

static int copy_file(const char *from, const char *to)
{
	struct stat st;
	char *t = read_file(from);
	int r;
	if(!t || stat(from, &st) != 0) {
		free(t);
		return -1;
	}
	r = write_file(to, t, st.st_mode & 07777);
	free(t);
	return r;
}

static int valid_name(const char *n)
{
	return n && *n && *n != '.' && strspn(n, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.:@") == strlen(n);
}

/* "usr/lib/systemd/system/sshd.service", "sshd.service" oder "sshd" → "sshd" */
static char *unit_name(const char *arg)
{
	const char *b = strrchr(arg, '/');
	char *n;
	size_t l;
	b = b ? b + 1 : arg;
	n = strdup(b);
	if(!n) return NULL;
	l = strlen(n);
	if(l > 8 && !strcmp(n + l - 8, ".service")) n[l - 8] = '\0';
	if(!valid_name(n)) {
		free(n);
		return NULL;
	}
	return n;
}

/* ---------- Liste der eingeschalteten Dienste ---------- */

static alpm_list_t *an_read(void)
{
	char *p = P(AN_FILE), *t = p ? read_file(p) : NULL, *save = NULL;
	alpm_list_t *l = NULL;
	free(p);
	if(!t) return NULL;
	for(char *s = strtok_r(t, "\n", &save); s; s = strtok_r(NULL, "\n", &save)) {
		while(*s == ' ' || *s == '\t') s++;
		if(*s && *s != '#' && valid_name(s) && !alpm_list_find_str(l, s)) {
			l = alpm_list_add(l, strdup(s));
		}
	}
	free(t);
	return l;
}

static int an_write(alpm_list_t *l)
{
	char *p = P(AN_FILE), *d = P("/etc/rcyay");
	size_t n = 64;
	char *buf;
	int r;
	for(alpm_list_t *i = l; i; i = i->next) n += strlen(i->data) + 1;
	buf = malloc(n);
	if(!p || !d || !buf) {
		free(p);
		free(d);
		free(buf);
		return -1;
	}
	strcpy(buf, "# rcyay-dienste: enabled services (rcyay --enable/--disable)\n");
	for(alpm_list_t *i = l; i; i = i->next) {
		strcat(buf, i->data);
		strcat(buf, "\n");
	}
	rc_mkdir_p(d, 0755);
	r = write_file(p, buf, 0644);
	free(buf);
	free(p);
	free(d);
	return r;
}

/* ---------- Dienstordner ---------- */

typedef enum { SRC_NONE, SRC_PKG, SRC_GEN } src_t;

/* Ordner mit run bzw. start für NAME; *src = Herkunft */
static char *service_dir(const char *name, src_t *src)
{
	char *pkg = P(PKG_SV "/%s", name), *pr = P(PKG_SV "/%s/run", name);
	char *gen = P(GEN_DIR "/%s", name), *gr = P(GEN_DIR "/%s/run", name), *gs = P(GEN_DIR "/%s/start", name);
	char *res = NULL;
	*src = SRC_NONE;
	if(exists(pr)) {
		res = pkg;
		pkg = NULL;
		*src = SRC_PKG;
	} else if(exists(gr) || exists(gs)) {
		res = gen;
		gen = NULL;
		*src = SRC_GEN;
	}
	free(pkg);
	free(pr);
	free(gen);
	free(gr);
	free(gs);
	return res;
}

/* Pfad ohne root-Präfix (für Links, inittab, start.d – gelten im Zielsystem) */
static const char *in_target(const char *p)
{
	size_t n = strlen(root);
	return strncmp(p, root, n) == 0 ? p + n : p;
}

static int is_oneshot(const char *dir)
{
	char *r = NULL, *s = NULL;
	int one;
	if(asprintf(&r, "%s/run", dir) < 0 || asprintf(&s, "%s/start", dir) < 0) {
		free(r);
		return 0;
	}
	one = !exists(r) && exists(s);
	free(r);
	free(s);
	return one;
}

/* ---------- inittab-Block (BusyBox init) ---------- */

static int inittab_edit(const char *name, const char *dir, int add)
{
	char *p = P(INITTAB), *t, *line = NULL, *out;
	size_t cap;
	alpm_list_t *lines = NULL, *entries = NULL;
	int inblock = 0, changed = 0, referenced = 0;
	char *want = NULL;

	if(!p) return -1;
	t = read_file(p);
	if(!t) {
		if(!add) {
			free(p);
			return 0;
		}
		t = strdup("");
	}
	if(dir && asprintf(&want, "::respawn:%s/run", in_target(dir)) < 0) want = NULL;
	/* Zeilen außerhalb des Blocks bleiben, Block wird neu geschrieben */
	{
		char *save = NULL, *copy = strdup(t);
		for(char *s = copy ? strtok_r(copy, "\n", &save) : NULL; s; s = strtok_r(NULL, "\n", &save)) {
			if(!strcmp(s, TAB_BEGIN)) { inblock = 1; continue; }
			if(!strcmp(s, TAB_END)) { inblock = 0; continue; }
			if(inblock) {
				/* Eintrag "::respawn:/etc/…/NAME/run # NAME" */
				const char *hash = strstr(s, " # ");
				if(hash && !strcmp(hash + 3, name)) {
					continue;
				}
				entries = alpm_list_add(entries, strdup(s));
			} else {
				if(want && strstr(s, in_target(dir)) && strstr(s, "respawn")) referenced = 1;
				lines = alpm_list_add(lines, strdup(s));
			}
		}
		free(copy);
	}
	if(add && !referenced) {
		char *e = NULL;
		if(asprintf(&e, "%s # %s", want, name) >= 0) entries = alpm_list_add(entries, e);
	}
	cap = strlen(t) + 512;
	for(alpm_list_t *i = entries; i; i = i->next) cap += strlen(i->data) + 1;
	out = malloc(cap);
	if(out) {
		out[0] = '\0';
		for(alpm_list_t *i = lines; i; i = i->next) {
			strcat(out, i->data);
			strcat(out, "\n");
		}
		if(entries) {
			strcat(out, TAB_BEGIN "\n");
			for(alpm_list_t *i = entries; i; i = i->next) {
				strcat(out, i->data);
				strcat(out, "\n");
			}
			strcat(out, TAB_END "\n");
		}
		changed = strcmp(out, t) != 0;
		if(changed && write_file(p, out, 0644) != 0) changed = -1;
	}
	free(out);
	free(line);
	free(want);
	free(t);
	free(p);
	FREELIST(lines);
	FREELIST(entries);
	return changed;
}

static void busybox_reload(void)
{
	char comm[64] = "";
	FILE *f;
	if(!live) return;
	f = fopen("/proc/1/comm", "r");
	if(f) {
		if(!fgets(comm, sizeof(comm), f)) comm[0] = '\0';
		fclose(f);
	}
	if(!strncmp(comm, "init", 4) || !strncmp(comm, "busybox", 7)) {
		kill(1, SIGHUP);  /* BusyBox init liest /etc/inittab neu */
	}
}

/* ---------- Einschalten/Ausschalten im aktiven Init ---------- */

static void run_quiet(const char *a0, const char *a1, const char *a2, const char *a3)
{
	const char *argv[] = { a0, a1, a2, a3, NULL };
	if(rc_have_program(a0)) {
		rc_run_quiet(argv, "/", (uid_t)-1, (gid_t)-1, NULL, NULL);
	}
}

static int s6_copy(const char *dir, const char *dest, int down)
{
	const char *files[] = { "run", "finish", NULL };
	if(rc_mkdir_p(dest, 0755) != 0) {
		rc_err("%s: %s\n", dest, strerror(errno));
		return -1;
	}
	for(int k = 0; files[k]; k++) {
		char *f = NULL, *t = NULL;
		if(asprintf(&f, "%s/%s", dir, files[k]) >= 0 && asprintf(&t, "%s/%s", dest, files[k]) >= 0) {
			if(exists(f)) copy_file(f, t);
			else unlink(t);
		}
		free(f);
		free(t);
	}
	if(down) {
		char *d = NULL;
		if(asprintf(&d, "%s/down", dest) >= 0) {
			int fd = open(d, O_WRONLY | O_CREAT, 0644);
			if(fd >= 0) close(fd);
		}
		free(d);
	}
	return 0;
}

static int activate(const char *name, int quiet)
{
	src_t src;
	char *dir = service_dir(name, &src);
	int ret = 0;

	if(init == RC_INIT_NONE || init == RC_INIT_SYSTEMD) {
		free(dir);
		return 0;
	}
	if(!dir) {
		if(!quiet) rc_warn(_("%s: no service for %s - not enabled in the init\n"), name, rc_init_name(init));
		return 1;
	}
	if(is_oneshot(dir)) {
		char *d = P(START_D), *f = P(START_D "/rcyay-%s.sh", name), *txt = NULL;
		if(!exists(d)) {
			rc_warn(_("%s: %s is missing - oneshot service %s is not run at boot\n"), name, START_D, name);
		} else if(asprintf(&txt, "# rcyay-dienste: oneshot %s (sourced by rc.start)\n%s/start\n",
					name, in_target(dir)) >= 0) {
			ret = write_file(f, txt, 0644);
		}
		free(txt);
		free(d);
		free(f);
		free(dir);
		return ret;
	}
	switch(init) {
		case RC_INIT_RUNIT: {
			char *d = P(RUNIT_DIR), *l = P(RUNIT_DIR "/%s", name);
			char cur[4096];
			ssize_t n = l ? readlink(l, cur, sizeof(cur) - 1) : -1;
			if(n >= 0) cur[n] = '\0';
			if(n >= 0 && !strcmp(cur, in_target(dir))) {
				break;
			}
			rc_mkdir_p(d, 0755);
			if(n >= 0) unlink(l);
			if(exists(l)) {
				rc_warn(_("%s: %s exists and is not a link - left alone\n"), name, l);
			} else if(symlink(in_target(dir), l) != 0) {
				rc_err("%s: %s\n", l, strerror(errno));
				ret = -1;
			}
			free(d);
			free(l);
			break;
		}
		case RC_INIT_S6: {
			char *img = P(S6_IMAGE "/%s", name);
			char *base = P(S6_IMAGE);
			if(exists(base) || exists(img)) {
				ret = s6_copy(dir, img, 1);
			} else {
				rc_warn(_("%s: %s is missing (s6-linux-init not set up) - only enabled for this boot\n"),
						name, S6_IMAGE);
			}
			if(live && exists(S6_LIVE "/.s6-svscan")) {
				char *ld = NULL;
				if(asprintf(&ld, S6_LIVE "/%s", name) >= 0) {
					int neu = !exists(ld);
					s6_copy(dir, ld, 0);
					if(neu) run_quiet("s6-svscanctl", "-a", S6_LIVE, NULL);
					else run_quiet("s6-svc", "-u", ld, NULL);
				}
				free(ld);
			}
			free(img);
			free(base);
			break;
		}
		case RC_INIT_BUSYBOX: {
			int c = inittab_edit(name, dir, 1);
			if(c < 0) ret = -1;
			else if(c > 0) busybox_reload();
			break;
		}
		default:
			break;
	}
	free(dir);
	return ret;
}

static int deactivate(const char *name, rc_init_t which)
{
	char *f = P(START_D "/rcyay-%s.sh", name);
	if(f) unlink(f);
	free(f);
	switch(which) {
		case RC_INIT_RUNIT: {
			char *l = P(RUNIT_DIR "/%s", name);
			char cur[4096];
			ssize_t n = l ? readlink(l, cur, sizeof(cur) - 1) : -1;
			if(n >= 0) unlink(l);  /* nur Links (die wir oder ein Paket setzten) */
			free(l);
			break;
		}
		case RC_INIT_S6: {
			char *img = P(S6_IMAGE "/%s", name);
			if(img && exists(img)) rc_rm_rf(img);
			free(img);
			if(live) {
				char *ld = NULL;
				if(asprintf(&ld, S6_LIVE "/%s", name) >= 0 && exists(ld)) {
					run_quiet("s6-svc", "-wD", "-d", ld);
					rc_rm_rf(ld);
					run_quiet("s6-svscanctl", "-an", S6_LIVE, NULL);
				}
				free(ld);
			}
			break;
		}
		case RC_INIT_BUSYBOX:
			if(inittab_edit(name, NULL, 0) > 0) busybox_reload();
			break;
		default:
			break;
	}
	return 0;
}

/* ---------- Übersetzen ---------- */

static char *find_unit(const char *name)
{
	const char *dirs[] = { "/etc/systemd/system", "/usr/lib/systemd/system", NULL };
	for(int k = 0; dirs[k]; k++) {
		char *p = P("%s/%s.service", dirs[k], name);
		struct stat st;
		if(p && stat(p, &st) == 0 && S_ISREG(st.st_mode)) return p;
		free(p);
	}
	return NULL;
}

static char *find_overlay(const char *name)
{
	const char *dirs[] = { OVL_ETC, OVL_USR, NULL };
	for(int k = 0; dirs[k]; k++) {
		char *d = P("%s/%s", dirs[k], name), *r = NULL, *s = NULL;
		int ok = d && asprintf(&r, "%s/run", d) >= 0 && asprintf(&s, "%s/start", d) >= 0 &&
			(exists(r) || exists(s));
		free(r);
		free(s);
		if(ok) return d;
		free(d);
	}
	return NULL;
}

/* systemd-Einschaltzustand übernehmen (/etc/systemd/system/X.wants/NAME.service) */
static int systemd_enabled(const char *name)
{
	char *pat = P("/etc/systemd/system/*.wants/%s.service", name);
	glob_t g;
	int r = 0;
	if(pat && glob(pat, GLOB_NOSORT, NULL, &g) == 0) {
		r = g.gl_pathc > 0;
		globfree(&g);
	}
	free(pat);
	return r;
}

static void gen_clear(const char *name)
{
	char *d = P(GEN_DIR "/%s", name);
	if(d && exists(d)) rc_rm_rf(d);
	free(d);
}

/* 0 = bereit, 1 = übersprungen, -1 = Fehler/nicht übersetzbar */
static int provide(const char *name, int verbose)
{
	char *ovl, *unit, *text, *gdir, *err = NULL, *why = NULL;
	char *pr = P(PKG_SV "/%s/run", name);
	rc_unit_t *u;
	rc_svout_t out;
	alpm_list_t *notes = NULL;
	int r;

	if(exists(pr)) {
		free(pr);
		gen_clear(name);
		if(verbose) rc_msg(_("%s: provided by a package in %s/%s\n"), name, PKG_SV, name);
		return 0;
	}
	free(pr);
	gdir = P(GEN_DIR "/%s", name);
	if(!gdir) return -1;
	if((ovl = find_overlay(name))) {
		const char *files[] = { "run", "finish", "start", NULL };
		gen_clear(name);
		rc_mkdir_p(gdir, 0755);
		for(int k = 0; files[k]; k++) {
			char *f = NULL, *t = NULL;
			if(asprintf(&f, "%s/%s", ovl, files[k]) >= 0 && asprintf(&t, "%s/%s", gdir, files[k]) >= 0 &&
					exists(f)) {
				copy_file(f, t);
			}
			free(f);
			free(t);
		}
		if(verbose) rc_msg(_("%s: own service from %s\n"), name, in_target(ovl));
		free(ovl);
		free(gdir);
		return 0;
	}
	unit = find_unit(name);
	if(!unit) {
		gen_clear(name);
		free(gdir);
		return 1;
	}
	text = read_file(unit);
	u = text ? rc_unit_parse(in_target(unit), text, &err) : NULL;
	free(text);
	if(!u) {
		rc_warn(_("%s: %s\n"), name, err ? err : strerror(errno));
		free(err);
		free(unit);
		free(gdir);
		return -1;
	}
	r = rc_unit_translate(u, init, &out, &why, &notes);
	if(r == 0) {
		char *f = NULL;
		gen_clear(name);
		rc_mkdir_p(gdir, 0755);
		if(out.run && asprintf(&f, "%s/run", gdir) >= 0) write_file(f, out.run, 0755);
		free(f);
		f = NULL;
		if(out.finish && asprintf(&f, "%s/finish", gdir) >= 0) write_file(f, out.finish, 0755);
		free(f);
		f = NULL;
		if(out.start && asprintf(&f, "%s/start", gdir) >= 0) write_file(f, out.start, 0755);
		free(f);
		if(verbose) {
			rc_msg(_("%s: translated for %s (%s)\n"), name, rc_init_name(init),
					out.kind == RC_SV_ONESHOT ? _("oneshot") : _("supervised"));
		}
		for(alpm_list_t *i = notes; i; i = i->next) {
			printf(" -> %s\n", (char *)i->data);
		}
		rc_svout_free(&out);
	} else if(r == 1) {
		gen_clear(name);
		if(verbose) printf(" -> %s\n", why);
	} else {
		gen_clear(name);
		rc_warn("%s\n", why);
	}
	FREELIST(notes);
	free(why);
	rc_unit_free(u);
	free(unit);
	free(gdir);
	return r;
}

/* alle Dienstnamen: Units + Overlays + bisher Erzeugtes */
static alpm_list_t *all_names(void)
{
	const char *dirs[] = { "/usr/lib/systemd/system", "/etc/systemd/system", GEN_DIR, OVL_ETC, OVL_USR, NULL };
	alpm_list_t *l = NULL;
	for(int k = 0; dirs[k]; k++) {
		char *d = P("%s", dirs[k]);
		DIR *dp = d ? opendir(d) : NULL;
		struct dirent *e;
		while(dp && (e = readdir(dp))) {
			char *n;
			size_t len = strlen(e->d_name);
			if(e->d_name[0] == '.') continue;
			if(k < 2 && (len <= 8 || strcmp(e->d_name + len - 8, ".service") != 0)) continue;
			n = unit_name(e->d_name);
			if(n && !alpm_list_find_str(l, n)) l = alpm_list_add(l, n);
			else free(n);
		}
		if(dp) closedir(dp);
		free(d);
	}
	return alpm_list_msort(l, alpm_list_count(l), (alpm_list_fn_cmp)strcmp);
}

/* Ziele aus Befehlszeile oder (Hook, NeedsTargets) von stdin */
static alpm_list_t *targets_or_stdin(alpm_list_t *args, int all_if_empty)
{
	alpm_list_t *l = NULL;
	for(alpm_list_t *i = args; i; i = i->next) {
		char *n = unit_name(i->data);
		if(!n) {
			rc_warn(_("invalid service name '%s'\n"), (char *)i->data);
			continue;
		}
		if(!alpm_list_find_str(l, n)) l = alpm_list_add(l, n);
		else free(n);
	}
	if(!args && !isatty(STDIN_FILENO)) {
		char *line = NULL;
		size_t cap = 0;
		ssize_t n;
		while((n = getline(&line, &cap, stdin)) > 0) {
			char *nm;
			while(n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
			if(!*line) continue;
			nm = unit_name(line);
			if(nm && strstr(line, "systemd/user/")) {
				/* Nutzer-Units (pipewire …) gehören in die Sitzung, nicht ins Init */
				printf(_(" -> %s: user unit - start it from your session (e.g. niri spawn-at-startup), not translated\n"), nm);
				free(nm);
				continue;
			}
			if(nm && !alpm_list_find_str(l, nm)) l = alpm_list_add(l, nm);
			else free(nm);
		}
		free(line);
	} else if(!args && all_if_empty) {
		l = all_names();
	}
	return l;
}

static int need_root(void)
{
	if(geteuid() != 0 && !*root) {
		rc_err(_("this operation needs root privileges\n"));
		return -1;
	}
	return 0;
}

static int cmd_update(alpm_list_t *names, int verbose)
{
	alpm_list_t *an = an_read();
	int changed = 0, ret = 0;
	if(init == RC_INIT_NONE || init == RC_INIT_SYSTEMD) {
		if(verbose) rc_msg(_("Init is %s - systemd units are used directly\n"), rc_init_name(init));
		FREELIST(an);
		return 0;
	}
	for(alpm_list_t *i = names; i; i = i->next) {
		const char *n = i->data;
		int r = provide(n, verbose);
		if(r < 0) ret = 1;
		if(r == 0 && !alpm_list_find_str(an, n) && systemd_enabled(n)) {
			an = alpm_list_add(an, strdup(n));
			changed = 1;
		}
		if(alpm_list_find_str(an, n)) {
			if(r == 0) activate(n, 0);
			else deactivate(n, init);
		}
	}
	if(changed) an_write(an);
	FREELIST(an);
	return ret;
}

static int cmd_remove(alpm_list_t *names)
{
	alpm_list_t *an = an_read(), *keep = NULL;
	for(alpm_list_t *i = names; i; i = i->next) {
		const char *n = i->data;
		deactivate(n, init);
		gen_clear(n);
	}
	for(alpm_list_t *i = an; i; i = i->next) {
		if(!alpm_list_find_str(names, i->data)) keep = alpm_list_add(keep, strdup(i->data));
	}
	if(alpm_list_count(keep) != alpm_list_count(an)) an_write(keep);
	FREELIST(an);
	FREELIST(keep);
	return 0;
}

static int cmd_enable(alpm_list_t *names, int on)
{
	alpm_list_t *an = an_read();
	int ret = 0;
	for(alpm_list_t *i = names; i; i = i->next) {
		const char *n = i->data;
		src_t src;
		char *dir;
		if(on) {
			if(init != RC_INIT_NONE && init != RC_INIT_SYSTEMD) {
				dir = service_dir(n, &src);
				if(!dir && provide(n, 1) != 0) {
					rc_err(_("%s: no service to enable\n"), n);
					ret = 1;
					continue;
				}
				free(dir);
				if(activate(n, 0) != 0) ret = 1;
			}
			if(!alpm_list_find_str(an, n)) an = alpm_list_add(an, strdup(n));
		} else {
			alpm_list_t *keep = NULL;
			deactivate(n, init);
			for(alpm_list_t *k = an; k; k = k->next) {
				if(strcmp(k->data, n) != 0) keep = alpm_list_add(keep, strdup(k->data));
			}
			FREELIST(an);
			an = keep;
		}
	}
	if(init == RC_INIT_NONE || init == RC_INIT_SYSTEMD) {
		rc_warn(_("Init is %s - use systemctl; recorded for other inits only\n"), rc_init_name(init));
	}
	if(an_write(an) != 0) ret = 1;
	FREELIST(an);
	return ret;
}

static int cmd_apply(void)
{
	alpm_list_t *an = an_read();
	int ret = 0;
	for(alpm_list_t *i = an; i; i = i->next) {
		if(activate(i->data, 0) < 0) ret = 1;
	}
	FREELIST(an);
	return ret;
}

static int cmd_list(void)
{
	alpm_list_t *names = all_names(), *an = an_read();
	printf("%-28s %-4s %s\n", _("SERVICE"), _("ON"), _("SOURCE"));
	for(alpm_list_t *i = names; i; i = i->next) {
		const char *n = i->data;
		src_t src;
		char *dir = service_dir(n, &src), *ovl = find_overlay(n);
		const char *what;
		if(src == SRC_PKG) what = _("package (/etc/sv)");
		else if(src == SRC_GEN && ovl) what = _("overlay");
		else if(src == SRC_GEN) what = is_oneshot(dir) ? _("translated, oneshot") : _("translated");
		else what = _("not translated");
		printf("%-28s %-4s %s\n", n, alpm_list_find_str(an, n) ? _("yes") : "-", what);
		free(dir);
		free(ovl);
	}
	FREELIST(names);
	FREELIST(an);
	return 0;
}

/* Init = X in rcyay.conf setzen */
static int conf_set_init(const char *value)
{
	const char *path = R.rcyayconf ? R.rcyayconf : getenv("RCYAY_CONF");
	char *t, *out, *save = NULL;
	int done = 0, inopt = 0, r;
	size_t cap;
	if(!path) path = RCYAY_CONF;
	t = read_file(path);
	if(!t) t = strdup("[options]\n");
	if(!t) return -1;
	cap = strlen(t) + 64;
	out = malloc(cap);
	if(!out) {
		free(t);
		return -1;
	}
	out[0] = '\0';
	for(char *s = strtok_r(t, "\n", &save); s; s = strtok_r(NULL, "\n", &save)) {
		char *k = s;
		while(*k == ' ' || *k == '\t' || (*k == '#' && !done)) k++;
		if(!done && !strncmp(k, "Init", 4) && (k[4] == ' ' || k[4] == '=' || k[4] == '\t')) {
			char *eq = strchr(k, '=');
			if(eq) {
				sprintf(out + strlen(out), "Init = %s\n", value);
				done = 1;
				continue;
			}
		}
		if(s[0] == '[') {
			if(inopt && !done) {
				sprintf(out + strlen(out), "Init = %s\n", value);
				done = 1;
			}
			inopt = !strncmp(s, "[options]", 9) || !strncmp(s, "[rcyay]", 7);
		}
		strcat(out, s);
		strcat(out, "\n");
	}
	if(!done) {
		if(!inopt) strcat(out, "[options]\n");
		sprintf(out + strlen(out), "Init = %s\n", value);
	}
	r = write_file(path, out, 0644);
	free(out);
	free(t);
	return r;
}

static int cmd_init(const char *value)
{
	rc_init_t neu = rc_init_parse(value), alt = init;
	alpm_list_t *names, *an;
	int ret;
	if((int)neu < 0 || neu == RC_INIT_NONE) {
		rc_err(_("invalid init '%s' (systemd, busybox, runit, s6)\n"), value);
		return 1;
	}
	an = an_read();
	if(alt != RC_INIT_NONE && alt != RC_INIT_SYSTEMD) {
		for(alpm_list_t *i = an; i; i = i->next) deactivate(i->data, alt);
	}
	/* systemd-Zustand einmal übernehmen, wenn noch nichts eingeschaltet ist */
	if(conf_set_init(value) != 0) {
		FREELIST(an);
		return 1;
	}
	init = neu;
	rc_msg(_("Init is now %s\n"), rc_init_name(neu));
	FREELIST(an);
	if(neu == RC_INIT_SYSTEMD) {
		return 0;
	}
	names = all_names();
	ret = cmd_update(names, 1);
	FREELIST(names);
	if(cmd_apply() != 0) ret = 1;
	return ret;
}

int rc_dienste_main(void)
{
	alpm_list_t *names;
	int ret;
	const char *sub = R.svcmd;

	if(R.rootdir && strcmp(R.rootdir, "/") != 0) {
		static char rbuf[4096];
		snprintf(rbuf, sizeof(rbuf), "%s", R.rootdir);
		while(strlen(rbuf) > 1 && rbuf[strlen(rbuf) - 1] == '/') rbuf[strlen(rbuf) - 1] = '\0';
		root = rbuf;
	}
	live = !*root;
	init = rc_init_parse(R.init);
	if((int)init < 0) {
		rc_err(_("rcyay.conf: invalid Init = %s (systemd, busybox, runit, s6)\n"), R.init);
		return 1;
	}

	if(!strcmp(sub, "list")) {
		return cmd_list();
	}
	if(need_root() != 0) return 1;
	if(!strcmp(sub, "init")) {
		if(!R.targets || R.targets->next) {
			rc_err(_("--init needs exactly one of: systemd, busybox, runit, s6\n"));
			return 1;
		}
		return cmd_init(R.targets->data);
	}
	if(!strcmp(sub, "apply")) {
		return cmd_apply();
	}
	if(!strcmp(sub, "update")) {
		names = targets_or_stdin(R.targets, 1);
		ret = cmd_update(names, 1);
		FREELIST(names);
		return ret;
	}
	if(!strcmp(sub, "remove")) {
		names = targets_or_stdin(R.targets, 0);
		ret = cmd_remove(names);
		FREELIST(names);
		return ret;
	}
	if(!strcmp(sub, "enable") || !strcmp(sub, "disable")) {
		if(!R.targets) {
			rc_err(_("no service given\n"));
			return 1;
		}
		names = targets_or_stdin(R.targets, 0);
		ret = cmd_enable(names, sub[0] == 'e');
		FREELIST(names);
		return ret;
	}
	rc_err(_("unknown --services command '%s' (update, remove, list, apply)\n"), sub);
	return 1;
}
