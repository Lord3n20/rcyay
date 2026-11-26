/*
 *  vprov.c - VirtualProvides: Abhängigkeiten, die das System selbst erfüllt
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  „VirtualProvides = coreutils findutils=4.10.0 …“ in /etc/rcyay.conf erklärt,
 *  dass das System (z. B. BusyBox) diese Abhängigkeiten erfüllt.
 *
 *  Umsetzung ohne Änderung an libalpm: rcyay erzeugt das Metapaket
 *  „usrside-provides“ (arch=any, keine Dateien, provides=(Liste)) und installiert
 *  es mit pacman -U, sobald die Liste nicht mehr zum installierten Paket passt
 *  (geprüft vor jeder Transaktion -S/-U). Damit sieht jede pacman-Transaktion
 *  (auch reines pacman, -Syu, pacman -r) die Liste über die lokale Datenbank,
 *  und -Qi/-Qo/-Rs bleiben ehrlich. Der AUR-Resolver fragt die Liste zusätzlich
 *  direkt (rc_vprov_satisfies), damit er auch ohne installiertes Metapaket
 *  (z. B. -Sp, Fehler beim Erzeugen) richtig plant.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>

#include "config.h"
#include "conf.h"
#include "util.h"
#include "rcyay.h"

#define VPROV_PKG "usrside-provides"

/* Eintrag prüfen: NAME oder NAME=VERSION (wie provides=() in PKGBUILDs) */
int rc_vprov_valid(const char *s)
{
	const char *eq = strchr(s, '=');
	size_t nlen = eq ? (size_t)(eq - s) : strlen(s);
	if(nlen == 0 || s[0] == '-' || s[0] == '.') {
		return 0;
	}
	for(size_t i = 0; i < nlen; i++) {
		char c = s[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				strchr("@._+-", c))) {
			return 0;
		}
	}
	if(eq) {
		const char *v = eq + 1;
		if(!*v) {
			return 0;
		}
		for(; *v; v++) {
			if(*v == '=' || *v == '<' || *v == '>' || *v == '/' || (unsigned char)*v <= ' ') {
				return 0;
			}
		}
	}
	return 1;
}

/* Eine Zeile „VirtualProvides = a b=1 …“ aus rcyay.conf aufnehmen */
void rc_vprov_add(const char *file, int line, const char *value)
{
	char *dup = strdup(value), *save = NULL;
	if(!dup) {
		return;
	}
	for(char *t = strtok_r(dup, " \t", &save); t; t = strtok_r(NULL, " \t", &save)) {
		if(!rc_vprov_valid(t)) {
			rc_warn(_("%s:%d: VirtualProvides: invalid entry '%s' (expected NAME or NAME=VERSION)\n"),
					file, line, t);
			continue;
		}
		if(!alpm_list_find_str(R.vprovides, t)) {
			R.vprovides = alpm_list_add(R.vprovides, strdup(t));
		}
	}
	free(dup);
}

/* Erfüllt die Liste dep? Gleiche Regeln wie libalpm für provides:
 * ein Eintrag ohne Version erfüllt nur Abhängigkeiten ohne Versionsangabe. */
int rc_vprov_satisfies(const char *dep)
{
	alpm_depend_t *d;
	int ok = 0;
	if(!R.vprovides) {
		return 0;
	}
	if(!(d = alpm_dep_from_string(dep))) {
		return 0;
	}
	for(alpm_list_t *i = R.vprovides; i && !ok; i = i->next) {
		const char *p = i->data, *eq = strchr(p, '=');
		size_t nlen = eq ? (size_t)(eq - p) : strlen(p);
		int cmp;
		if(strlen(d->name) != nlen || strncmp(d->name, p, nlen) != 0) {
			continue;
		}
		if(d->mod == ALPM_DEP_MOD_ANY) {
			ok = 1;
			break;
		}
		if(!eq) {
			continue;
		}
		cmp = alpm_pkg_vercmp(eq + 1, d->version);
		switch(d->mod) {
			case ALPM_DEP_MOD_EQ: ok = cmp == 0; break;
			case ALPM_DEP_MOD_GE: ok = cmp >= 0; break;
			case ALPM_DEP_MOD_LE: ok = cmp <= 0; break;
			case ALPM_DEP_MOD_GT: ok = cmp > 0; break;
			case ALPM_DEP_MOD_LT: ok = cmp < 0; break;
			default: break;
		}
	}
	alpm_dep_free(d);
	return ok;
}

static int cmpstr(const void *a, const void *b)
{
	return strcmp(a, b);
}

/* provides des installierten Metapakets als sortierte Zeichenketten */
static alpm_list_t *installed_provides(alpm_pkg_t *lp)
{
	alpm_list_t *r = NULL;
	for(alpm_list_t *i = alpm_pkg_get_provides(lp); i; i = i->next) {
		char *s = alpm_dep_compute_string(i->data);
		if(s) {
			r = alpm_list_add(r, s);
		}
	}
	return alpm_list_msort(r, alpm_list_count(r), cmpstr);
}

/* 1, wenn das installierte Metapaket nicht zur Liste passt */
static int needs_update(alpm_pkg_t *lp)
{
	alpm_list_t *have, *want;
	int diff;
	if(!lp) {
		return R.vprovides != NULL;
	}
	have = installed_provides(lp);
	want = alpm_list_msort(rc_list_dup(R.vprovides), alpm_list_count(R.vprovides), cmpstr);
	diff = alpm_list_count(have) != alpm_list_count(want);
	for(alpm_list_t *a = have, *b = want; !diff && a && b; a = a->next, b = b->next) {
		diff = strcmp(a->data, b->data) != 0;
	}
	FREELIST(have);
	FREELIST(want);
	return diff;
}

static int add_entry(struct archive *a, const char *name, const char *data, size_t len, time_t now)
{
	struct archive_entry *e = archive_entry_new();
	int r;
	if(!e) {
		return -1;
	}
	archive_entry_set_pathname(e, name);
	archive_entry_set_filetype(e, AE_IFREG);
	archive_entry_set_perm(e, 0644);
	archive_entry_set_size(e, (la_int64_t)len);
	archive_entry_set_mtime(e, now, 0);
	archive_entry_set_uname(e, "root");
	archive_entry_set_gname(e, "root");
	r = archive_write_header(a, e);
	if(r == ARCHIVE_OK && len > 0 && archive_write_data(a, data, len) != (la_ssize_t)len) {
		r = ARCHIVE_FATAL;
	}
	archive_entry_free(e);
	return r == ARCHIVE_OK ? 0 : -1;
}

/* Paketdatei schreiben (unkomprimiertes .pkg.tar, nur .PKGINFO – das Paket hat
 * keine Dateien; pacman braucht weder .MTREE noch .BUILDINFO zum Installieren).
 * Rückgabe: malloc'd Pfad oder NULL. */
static char *write_pkg(const char *dir, const char *ver)
{
	char *path = NULL, *info = NULL, *prov = NULL;
	struct archive *a = NULL;
	time_t now = time(NULL);
	size_t plen = 0;
	FILE *ps;
	int ok = 0;

	if(!(ps = open_memstream(&prov, &plen))) {
		return NULL;
	}
	for(alpm_list_t *i = R.vprovides; i; i = i->next) {
		fprintf(ps, "provides = %s\n", (char *)i->data);
	}
	fclose(ps);
	if(asprintf(&info,
			"# Generated by rcyay from VirtualProvides in %s\n"
			"pkgname = " VPROV_PKG "\n"
			"pkgbase = " VPROV_PKG "\n"
			"xdata = pkgtype=pkg\n"
			"pkgver = %s\n"
			"pkgdesc = UsrsideOS: dependencies provided by the system (VirtualProvides in rcyay.conf)\n"
			"url = https://github.com/Lord3n20/usrsideos\n"
			"builddate = %lld\n"
			"packager = rcyay\n"
			"size = 0\n"
			"arch = any\n"
			"license = GPL-2.0-or-later\n"
			"%s",
			R.rcyayconf ? R.rcyayconf : RCYAY_CONF, ver, (long long)now, prov ? prov : "") < 0) {
		info = NULL;
		goto out;
	}
	if(asprintf(&path, "%s/" VPROV_PKG "-%s-any.pkg.tar", dir, ver) < 0) {
		path = NULL;
		goto out;
	}
	if(!(a = archive_write_new())) {
		goto out;
	}
	archive_write_set_format_pax_restricted(a);
	archive_write_add_filter_none(a);
	if(archive_write_open_filename(a, path) != ARCHIVE_OK) {
		rc_err("%s: %s\n", path, archive_error_string(a));
		goto out;
	}
	if(add_entry(a, ".PKGINFO", info, strlen(info), now) != 0) {
		rc_err("%s: %s\n", path, archive_error_string(a));
		goto out;
	}
	ok = archive_write_close(a) == ARCHIVE_OK;
out:
	if(a) {
		archive_write_free(a);
	}
	free(info);
	free(prov);
	if(!ok) {
		if(path) {
			unlink(path);
		}
		free(path);
		return NULL;
	}
	chmod(path, 0644);
	return path;
}

static char *next_version(alpm_pkg_t *lp)
{
	long n = 0;
	char *r = NULL;
	if(lp) {
		char *end;
		n = strtol(alpm_pkg_get_version(lp), &end, 10);
		if(n < 0 || *end != '-') {
			n = 0;
		}
	}
	if(asprintf(&r, "%ld-1", n + 1) < 0) {
		r = NULL;
	}
	return r;
}

static int pacman_run(const char *op, const char *arg, int noconfirm)
{
	alpm_list_t *a = NULL;
	int r;
	for(alpm_list_t *i = R.global; i; i = i->next) a = alpm_list_add(a, strdup(i->data));
	a = alpm_list_add(a, strdup(op));
	if(noconfirm) a = alpm_list_add(a, strdup("--noconfirm"));
	a = alpm_list_add(a, strdup(arg));
	r = rc_pacman(a, 1);
	FREELIST(a);
	return r;
}

/* Metapaket an die Liste anpassen (vor Transaktionen). 0 = ok/nichts zu tun. */
int rc_vprov_sync(void)
{
	alpm_pkg_t *lp = alpm_db_get_pkg(alpm_get_localdb(R.handle), VPROV_PKG);
	char *dir = NULL, *ver = NULL, *file = NULL;
	int ret = 1;

	if(!needs_update(lp)) {
		return 0;
	}
	if(!R.vprovides) {
		/* Liste leer: Metapaket entfernen – pacman verweigert das, solange
		 * installierte Pakete die Einträge brauchen */
		rc_msg(_("VirtualProvides is empty - removing %s...\n"), VPROV_PKG);
		if(pacman_run("-R", VPROV_PKG, 1) != 0) {
			rc_warn(_("%s stays installed: installed packages still need what it provides\n"), VPROV_PKG);
			return 0;
		}
		return rc_alpm_reload();
	}
	if(asprintf(&dir, "%s/.provides", R.builddir) < 0) {
		return 1;
	}
	if(rc_mkdir_p(dir, 0755) != 0) {
		rc_err(_("cannot create %s: %s\n"), dir, strerror(errno));
		goto out;
	}
	if(!(ver = next_version(lp)) || !(file = write_pkg(dir, ver))) {
		rc_err(_("cannot create the package %s\n"), VPROV_PKG);
		goto out;
	}
	rc_msg(_("VirtualProvides changed - installing %s %s...\n"), VPROV_PKG, ver);
	if(pacman_run("-U", file, 1) != 0) {
		rc_err(_("failed to install %s (VirtualProvides)\n"), VPROV_PKG);
		goto out;
	}
	ret = rc_alpm_reload();
out:
	if(file) {
		unlink(file);
	}
	free(file);
	free(ver);
	free(dir);
	return ret;
}

/* rcyay --provides [list|apply] */
int rc_vprov_main(const char *cmd)
{
	alpm_pkg_t *lp = alpm_db_get_pkg(alpm_get_localdb(R.handle), VPROV_PKG);
	if(strcmp(cmd, "apply") == 0) {
		return rc_vprov_sync();
	}
	if(strcmp(cmd, "list") != 0) {
		rc_err(_("--provides: unknown command '%s' (list, apply)\n"), cmd);
		return 1;
	}
	for(alpm_list_t *i = R.vprovides; i; i = i->next) {
		printf("%s\n", (char *)i->data);
	}
	if(needs_update(lp)) {
		rc_warn(_("%s is not up to date - run rcyay --provides apply (as root)\n"), VPROV_PKG);
	}
	return 0;
}

/* usrside-busybox: Links /usr/bin/APPLET -> busybox gehören keinem Paket. Damit ein Paket
 * (coreutils, which …) den Pfad übernehmen kann, bekommt pacman bei -S/-U genau diese
 * Pfade als --overwrite (Liste des Hooks in ROOT/var/lib/usrside-busybox/links). */
void rc_busybox_overwrite(void)
{
	const char *root = alpm_option_get_root(R.handle);
	char *path = NULL, *line = NULL, *arg = NULL;
	size_t cap = 0, alen = 0;
	FILE *f, *out;
	ssize_t n;

	if(!root || asprintf(&path, "%s%svar/lib/usrside-busybox/links", root,
				root[strlen(root) - 1] == '/' ? "" : "/") < 0) {
		return;
	}
	f = fopen(path, "r");
	free(path);
	if(!f) {
		return;
	}
	if(!(out = open_memstream(&arg, &alen))) {
		fclose(f);
		return;
	}
	fputs("--overwrite=", out);
	n = 0;
	for(int first = 1; (n = getline(&line, &cap, f)) > 0; ) {
		line[strcspn(line, "\n")] = '\0';
		/* nur einfache Pfade usr/bin/NAME (keine Muster) */
		if(strncmp(line, "usr/bin/", 8) != 0 || !line[8] || strpbrk(line + 8, "/*?[,\\")) {
			continue;
		}
		fprintf(out, "%s%s", first ? "" : ",", line);
		first = 0;
	}
	fclose(out);
	fclose(f);
	free(line);
	if(arg && strcmp(arg, "--overwrite=") != 0) {
		R.tokens = alpm_list_add(R.tokens, strdup(arg));
		R.trans = alpm_list_add(R.trans, strdup(arg));
	}
	free(arg);
}
