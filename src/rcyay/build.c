/*
 *  build.c - AUR-Pakete auflösen, prüfen, bauen und installieren (-S, -Su)
 *
 *  Copyright (c) 2026 rcyay-Entwickler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <ftw.h>
#include <fnmatch.h>
#include <sys/stat.h>

#include "config.h"
#include "conf.h"
#include "util.h"
#include "rcyay.h"
#include "aurutil.h"

typedef struct {
	char *pkgbase;
	aur_base_t *b;
	char *neu;       /* BuildDir/.neu/BASE  (frischer Snapshot) */
	char *tarball;   /* BuildDir/.neu/BASE.tar.gz */
	char *snap;      /* BuildDir/.snap/BASE (zuletzt gebauter Snapshot) */
	char *dir;       /* BuildDir/BASE (Bauordner) */
	int devel;       /* nur bauen, wenn pkgver() neuer ist */
} job_t;

static alpm_list_t *jobs;    /* job_t* */
static uid_t build_uid = (uid_t)-1;
static gid_t build_gid = (gid_t)-1;
static char *build_home;

static char *path2(const char *a, const char *b, const char *c)
{
	char *r = NULL;
	if(asprintf(&r, "%s/%s%s", a, b, c ? c : "") < 0) {
		return NULL;
	}
	return r;
}

static job_t *job_for(const char *pkgbase)
{
	for(alpm_list_t *i = jobs; i; i = i->next) {
		job_t *j = i->data;
		if(strcmp(j->pkgbase, pkgbase) == 0) {
			return j;
		}
	}
	return NULL;
}

static void jobs_free(void)
{
	for(alpm_list_t *i = jobs; i; i = i->next) {
		job_t *j = i->data;
		free(j->pkgbase);
		free(j->neu);
		free(j->tarball);
		free(j->snap);
		free(j->dir);
		free(j);
	}
	alpm_list_free(jobs);
	jobs = NULL;
}

/* ---------- Resolver-Rückrufe ---------- */

static int cb_installed(void *ud, const char *dep)
{
	(void)ud;
	return alpm_find_satisfier(alpm_db_get_pkgcache(alpm_get_localdb(R.handle)), dep) != NULL ||
		rc_vprov_satisfies(dep);
}

static char *cb_repo(void *ud, const char *dep)
{
	alpm_pkg_t *p;
	(void)ud;
	p = alpm_find_dbs_satisfier(R.handle, alpm_get_syncdbs(R.handle), dep);
	return p ? strdup(alpm_pkg_get_name(p)) : NULL;
}

static aur_pkg_t *cb_info(void *ud, const char *name)
{
	(void)ud;
	return rc_aur_info(name);
}

static alpm_list_t *cb_providers(void *ud, const char *name)
{
	(void)ud;
	return rc_aur_providers(name);
}

static aur_pkg_t *cb_choose(void *ud, const char *dep, alpm_list_t *cands)
{
	int n = 1, count = (int)alpm_list_count(cands), sel;
	char *depname = aur_dep_name(dep);
	aur_pkg_t *def = aur_default_choice(depname, cands);
	(void)ud;
	free(depname);
	if(R.noconfirm || !isatty(fileno(stdin))) {
		return def;
	}
	rc_msg(_("There are %d AUR providers available for %s:\n"), count, dep);
	for(alpm_list_t *i = cands; i; i = i->next, n++) {
		aur_pkg_t *p = i->data;
		printf("   %d) %s %s (+%ld)%s\n", n, p->name, p->version, p->votes, p == def ? _(" [default]") : "");
	}
	for(;;) {
		char buf[64];
		fputs(_("Enter a number (Enter = default): "), stdout);
		fflush(stdout);
		if(!safe_fgets_stdin(buf, sizeof(buf))) {
			return def;
		}
		if(buf[0] == '\n' || buf[0] == '\0') {
			return def;
		}
		sel = atoi(buf);
		if(sel >= 1 && sel <= count) {
			return alpm_list_nth(cands, (size_t)(sel - 1))->data;
		}
	}
}

/* musl: Paket geht nur mit glibc? (nur-glibc.txt, -bin/-appimage, fertige Binärpakete als Quelle) */
static int ends_with(const char *s, const char *suf)
{
	size_t n = strlen(s), m = strlen(suf);
	return n >= m && strcasecmp(s + n - m, suf) == 0;
}

static char *cb_glibc_only(void *ud, aur_base_t *b, const char *pkgname)
{
	char *path = NULL, *line = NULL, *why = NULL;
	size_t cap = 0;
	FILE *f;
	alpm_list_t *src;
	(void)ud;

	if(asprintf(&path, "%s/nur-glibc.txt", R.overlaydir) >= 0 && (f = fopen(path, "r"))) {
		while(!why && getline(&line, &cap, f) > 0) {
			char *s = line, *e, *grund;
			while(*s == ' ' || *s == '\t') s++;
			if(*s == '#' || *s == '\n' || !*s) continue;
			e = s + strcspn(s, " \t\n");
			grund = e;
			if(*e) {
				*e = '\0';
				grund = e + 1;
				while(*grund == ' ' || *grund == '\t') grund++;
				grund[strcspn(grund, "\n")] = '\0';
			}
			if(fnmatch(s, pkgname, 0) == 0 || fnmatch(s, b->pkgbase, 0) == 0) {
				if(asprintf(&why, *grund ? _("listed in nur-glibc.txt (%s)") : _("listed in nur-glibc.txt"),
							grund) < 0) why = NULL;
			}
		}
		free(line);
		fclose(f);
	}
	free(path);
	if(why || R.force_musl) {
		return why;
	}
	if(ends_with(pkgname, "-bin") || ends_with(pkgname, "-appimage")) {
		return strdup(_("prebuilt binary package (use --force-musl to try anyway)"));
	}
	src = aur_srcinfo_values(b->srcinfo, NULL, AUR_FIELD_SOURCE, b->arch);
	for(alpm_list_t *i = src; i && !why; i = i->next) {
		const char *s = i->data;
		if(ends_with(s, ".deb") || ends_with(s, ".rpm") || ends_with(s, ".appimage") ||
				strstr(s, ".pkg.tar.")) {
			const char *base = strrchr(s, '/');
			if(asprintf(&why, _("source %s is a prebuilt binary (use --force-musl to try anyway)"),
						base ? base + 1 : s) < 0) why = NULL;
		}
	}
	alpm_list_free(src);
	return why;
}

static aur_srcinfo_t *cb_srcinfo(void *ud, aur_pkg_t *pkg, char **err)
{
	job_t *j;
	char *url, e[512], *si;
	long status;
	aur_srcinfo_t *res;
	(void)ud;

	if(!pkg->pkgbase[0] || pkg->pkgbase[0] == '.' || strchr(pkg->pkgbase, '/')) {
		if(asprintf(err, _("invalid pkgbase '%s'"), pkg->pkgbase) < 0) *err = NULL;
		return NULL;
	}
	j = calloc(1, sizeof(*j));
	if(!j) {
		return NULL;
	}
	j->pkgbase = strdup(pkg->pkgbase);
	j->neu = path2(R.builddir, ".neu/", pkg->pkgbase);
	j->tarball = path2(R.builddir, ".neu/", pkg->pkgbase);
	if(j->tarball) {
		char *t = NULL;
		if(asprintf(&t, "%s.tar.gz", j->tarball) < 0) t = NULL;
		free(j->tarball);
		j->tarball = t;
	}
	j->snap = path2(R.builddir, ".snap/", pkg->pkgbase);
	j->dir = path2(R.builddir, pkg->pkgbase, NULL);
	url = aur_snapshot_url(R.ctx, pkg);
	if(!j->neu || !j->tarball || !j->snap || !j->dir || !url) {
		free(url);
		*err = strdup(_("out of memory"));
		goto fail;
	}
	printf(_(" Downloading %s ...\n"), pkg->pkgbase);
	fflush(stdout);
	if(aur_http_download(R.ctx, url, j->tarball, &status) != 0) {
		*err = strdup(R.ctx->error);
		free(url);
		goto fail;
	}
	free(url);
	rc_rm_rf(j->neu);
	if(aur_extract(j->tarball, j->neu, 1, e, sizeof(e)) != 0) {
		*err = strdup(e);
		goto fail;
	}
	si = path2(j->neu, ".SRCINFO", NULL);
	res = si ? aur_srcinfo_parse_file(si, err) : NULL;
	free(si);
	if(!res) {
		goto fail;
	}
	if(strcmp(res->pkgbase, pkg->pkgbase) != 0) {
		if(asprintf(err, _("pkgbase in .SRCINFO (%s) does not match %s"), res->pkgbase, pkg->pkgbase) < 0) {
			*err = NULL;
		}
		aur_srcinfo_free(res);
		goto fail;
	}
	jobs = alpm_list_add(jobs, j);
	return res;
fail:
	free(j->pkgbase);
	free(j->neu);
	free(j->tarball);
	free(j->snap);
	free(j->dir);
	free(j);
	return NULL;
}

/* ---------- Prüfung (Diff/PKGBUILD) ---------- */

static int show_file(const char *path, const char *title)
{
	char *text = aur_read_file(path, NULL);
	if(!text) {
		return -1;
	}
	printf("%s==> %s%s\n", config->colstr.title, title, config->colstr.nocolor);
	fputs(text, stdout);
	if(*text && text[strlen(text) - 1] != '\n') {
		fputc('\n', stdout);
	}
	free(text);
	return 0;
}

static void review(job_t *j)
{
	struct stat st;
	if(stat(j->snap, &st) == 0) {
		const char *argv_git[] = { "git", "--no-pager", "diff", "--no-index", "--stat", "-p",
			config->color == PM_COLOR_ON ? "--color=always" : "--color=never",
			j->snap, j->neu, NULL };
		const char *argv_diff[] = { "diff", "-ruN", j->snap, j->neu, NULL };
		int r;
		fputs(config->colstr.title, stdout);
		printf(_("==> Changes in %s since the last build%s\n"), j->b->pkgbase, config->colstr.nocolor);
		if(rc_have_program("git")) {
			r = rc_run(argv_git, NULL, (uid_t)-1, (gid_t)-1, NULL, NULL);
		} else {
			r = rc_run(argv_diff, NULL, (uid_t)-1, (gid_t)-1, NULL, NULL);
		}
		if(r == 0) {
			fputs(_("   (no changes)\n"), stdout);
		}
	} else {
		DIR *d;
		char *p = path2(j->neu, "PKGBUILD", NULL), *t = NULL;
		if(asprintf(&t, _("PKGBUILD of %s (new)"), j->b->pkgbase) < 0) t = NULL;
		if(show_file(p, t ? t : "PKGBUILD") != 0) {
			rc_warn(_("%s: PKGBUILD is missing\n"), j->b->pkgbase);
		}
		free(p);
		free(t);
		/* .install-Dateien laufen als root – mit anzeigen */
		if((d = opendir(j->neu))) {
			struct dirent *e;
			while((e = readdir(d))) {
				size_t n = strlen(e->d_name);
				if(n > 8 && strcmp(e->d_name + n - 8, ".install") == 0) {
					char *f = path2(j->neu, e->d_name, NULL);
					show_file(f, e->d_name);
					free(f);
				}
			}
			closedir(d);
		}
	}
	fflush(stdout);
}

/* ---------- Bau ---------- */

/* Dateien des alten Snapshots, die im neuen fehlen, aus dem Bauordner löschen */
static const char *stale_snap, *stale_neu, *stale_dir;

static int stale_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	const char *rel = path + strlen(stale_snap);
	char *inneu, *indir;
	struct stat s2;
	(void)st; (void)ftw;
	if(*rel != '/' || flag != FTW_F) {
		return 0;
	}
	inneu = path2(stale_neu, rel + 1, NULL);
	indir = path2(stale_dir, rel + 1, NULL);
	if(inneu && indir && lstat(inneu, &s2) != 0) {
		unlink(indir);
	}
	free(inneu);
	free(indir);
	return 0;
}

static int prepare_dir(job_t *j)
{
	char e[512];
	struct stat st;
	if(rc_mkdir_p(j->dir, 0755) != 0) {
		rc_err("%s: %s\n", j->dir, strerror(errno));
		return -1;
	}
	if(stat(j->snap, &st) == 0) {
		stale_snap = j->snap;
		stale_neu = j->neu;
		stale_dir = j->dir;
		nftw(j->snap, stale_cb, 16, FTW_PHYS);
	}
	if(aur_extract(j->tarball, j->dir, 1, e, sizeof(e)) != 0) {
		rc_err("%s\n", e);
		return -1;
	}
	if(geteuid() == 0 && build_uid != (uid_t)-1) {
		rc_chown_r(j->dir, build_uid, build_gid);
	}
	return 0;
}

static int makepkg(job_t *j, const char *a1, const char *a2, char **out)
{
	const char *argv[10];
	int k = 0;
	argv[k++] = "makepkg";
	if(j->b && j->b->foreign_arch) argv[k++] = "--ignorearch";
	if(a1) argv[k++] = a1;
	if(a2) argv[k++] = a2;
	if(R.nocheck && a1 && strcmp(a1, "--packagelist") != 0 && strcmp(a1, "--printsrcinfo") != 0) {
		argv[k++] = "--nocheck";
	}
	argv[k] = NULL;
	return rc_run_nostdin(argv, j->dir, build_uid, build_gid, build_home, out);
}

/* Paketname aus Dateiname NAME-VER-REL-ARCH.pkg.tar.* */
static char *pkgname_from_file(const char *path)
{
	const char *b = strrchr(path, '/');
	char *s, *p;
	b = b ? b + 1 : path;
	s = strdup(b);
	if(!s) return NULL;
	p = strstr(s, ".pkg.tar");
	if(p) *p = '\0';
	for(int k = 0; k < 3; k++) {
		p = strrchr(s, '-');
		if(!p) {
			free(s);
			return NULL;
		}
		*p = '\0';
	}
	return s;
}

static int installed_version_cmp(job_t *j, const char *newver)
{
	alpm_db_t *local = alpm_get_localdb(R.handle);
	for(alpm_list_t *i = j->b->pkgnames; i; i = i->next) {
		alpm_pkg_t *lp = alpm_db_get_pkg(local, i->data);
		if(lp) {
			return alpm_pkg_vercmp(newver, alpm_pkg_get_version(lp));
		}
	}
	return 1;
}

/* baut einen pkgbase; *files = zu installierende Pakete. 1 = übersprungen */
static int build_job(job_t *j, alpm_list_t **files)
{
	char *out = NULL;
	int r;
	*files = NULL;

	rc_msg(_("Building %s %s ...\n"), j->b->pkgbase, j->b->srcinfo->version);
	if(prepare_dir(j) != 0) {
		return -1;
	}
	if(j->devel) {
		aur_srcinfo_t *si;
		char *err = NULL;
		r = makepkg(j, "-od", NULL, NULL);
		if(r != 0) {
			rc_err(_("makepkg -od failed for %s (%d)\n"), j->b->pkgbase, r);
			return -1;
		}
		r = makepkg(j, "--printsrcinfo", NULL, &out);
		si = r == 0 ? aur_srcinfo_parse(out, &err) : NULL;
		free(out);
		out = NULL;
		if(!si) {
			rc_err(_("%s: cannot determine the new version: %s\n"), j->b->pkgbase, err ? err : "makepkg");
			free(err);
			return -1;
		}
		if(installed_version_cmp(j, si->version) <= 0) {
			printf(_(" %s is up to date (%s) -- skipping\n"), j->b->pkgbase, si->version);
			aur_srcinfo_free(si);
			return 1;
		}
		printf(_(" %s: new version %s\n"), j->b->pkgbase, si->version);
		aur_srcinfo_free(si);
		r = makepkg(j, "-fe", NULL, NULL);
	} else {
		r = makepkg(j, "-f", NULL, NULL);
	}
	if(r != 0) {
		rc_err(_("failed to build %s (makepkg: %d)\n"), j->b->pkgbase, r);
		return -1;
	}
	r = makepkg(j, "--packagelist", NULL, &out);
	if(r != 0 || !out) {
		rc_err(_("makepkg --packagelist failed for %s\n"), j->b->pkgbase);
		free(out);
		return -1;
	}
	for(char *line = strtok(out, "\n"); line; line = strtok(NULL, "\n")) {
		char *name = pkgname_from_file(line);
		if(name && alpm_list_find_str(j->b->pkgnames, name)) {
			if(access(line, R_OK) != 0) {
				rc_err(_("package file is missing: %s\n"), line);
				free(name);
				free(out);
				FREELIST(*files);
				return -1;
			}
			*files = alpm_list_add(*files, strdup(line));
		}
		free(name);
	}
	free(out);
	if(alpm_list_count(*files) != alpm_list_count(j->b->pkgnames)) {
		rc_err(_("%s: not all packages were built\n"), j->b->pkgbase);
		FREELIST(*files);
		return -1;
	}
	return 0;
}

/* ---------- Ablauf ---------- */

static alpm_list_t *args_with(alpm_list_t *pre, alpm_list_t *more)
{
	alpm_list_t *a = NULL;
	for(alpm_list_t *i = R.global; i; i = i->next) a = alpm_list_add(a, strdup(i->data));
	for(alpm_list_t *i = pre; i; i = i->next) a = alpm_list_add(a, strdup(i->data));
	for(alpm_list_t *i = more; i; i = i->next) a = alpm_list_add(a, strdup(i->data));
	return a;
}

static int pacman_do(alpm_list_t *pre, int with_trans, alpm_list_t *names)
{
	alpm_list_t *a = args_with(pre, NULL), *t;
	int r;
	if(with_trans) {
		for(t = R.trans; t; t = t->next) a = alpm_list_add(a, strdup(t->data));
	}
	for(t = names; t; t = t->next) a = alpm_list_add(a, strdup(t->data));
	r = rc_pacman(a, 1);
	FREELIST(a);
	return r;
}

static void show_plan(aur_plan_t *plan)
{
	const colstr_t *c = &config->colstr;
	if(plan->repo) {
		printf(_("\n%sRepo dependencies (%zu):%s"), c->title, alpm_list_count(plan->repo), c->nocolor);
		for(alpm_list_t *i = plan->repo; i; i = i->next) {
			printf(" %s%s", (char *)i->data, alpm_list_find_str(plan->repo_make, i->data) ? _("[make]") : "");
		}
		printf("\n");
	}
	printf(_("\n%sAUR packages (%zu):%s\n"), c->title, alpm_list_count(plan->bases), c->nocolor);
	for(alpm_list_t *i = plan->bases; i; i = i->next) {
		aur_base_t *b = i->data;
		printf("   %s%s%s %s%s%s", c->title, b->pkgbase, c->nocolor, c->version,
				b->srcinfo->version, c->nocolor);
		if(alpm_list_count(b->pkgnames) > 1 || strcmp(b->pkgnames->data, b->pkgbase) != 0) {
			printf(" (");
			for(alpm_list_t *k = b->pkgnames; k; k = k->next) {
				printf("%s%s", (char *)k->data, k->next ? " " : "");
			}
			printf(")");
		}
		if(b->explicit == NULL) {
			int allmake = 1;
			for(alpm_list_t *k = b->pkgnames; k; k = k->next) {
				if(!aur_plan_is_makeonly(plan, b, k->data)) allmake = 0;
			}
			printf(" %s[%s]%s", c->meta, allmake ? _("make, removed afterwards") : _("dependency"), c->nocolor);
		}
		printf("\n");
	}
	printf("\n");
}

/* ---------- PGP-Schlüssel (validpgpkeys) ---------- */

/* validpgpkeys aus der .SRCINFO des frischen Snapshots (gehört zu pkgbase) */
static alpm_list_t *srcinfo_pgpkeys(job_t *j, alpm_list_t *keys)
{
	char *p = path2(j->neu, ".SRCINFO", NULL), line[1024];
	FILE *f = p ? fopen(p, "r") : NULL;
	free(p);
	if(!f) {
		return keys;
	}
	while(fgets(line, sizeof(line), f)) {
		char *k = line, *v, *e;
		while(*k == ' ' || *k == '\t') k++;
		if(strncmp(k, "validpgpkeys", 12) != 0) continue;
		v = k + 12;
		while(*v == ' ' || *v == '\t') v++;
		if(*v != '=') continue;
		v++;
		while(*v == ' ' || *v == '\t') v++;
		for(e = v + strlen(v); e > v && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'); e--);
		*e = '\0';
		/* nur Hex-Zeichen erlauben (Fingerabdruck), Großschreibung wie gpg */
		if(!*v || strspn(v, "0123456789abcdefABCDEF") != strlen(v)) {
			if(*v) rc_warn(_("%s: ignoring invalid validpgpkeys entry '%s'\n"), j->pkgbase, v);
			continue;
		}
		for(char *c = v; *c; c++) {
			if(*c >= 'a' && *c <= 'f') *c -= 'a' - 'A';
		}
		/* Eintrag: "FPR PKGBASE" – der Schlüssel zuerst, für die Anzeige */
		{
			int dup = 0;
			for(alpm_list_t *i = keys; i; i = i->next) {
				const char *s = i->data;
				size_t n = strlen(v);
				if(strncmp(s, v, n) == 0 && s[n] == ' ') {
					dup = 1;
					break;
				}
			}
			if(!dup) {
				char *ent = NULL;
				if(asprintf(&ent, "%s %s", v, j->pkgbase) >= 0) {
					keys = alpm_list_add(keys, ent);
				}
			}
		}
	}
	fclose(f);
	return keys;
}

/* 1, wenn der Schlüssel (Fingerabdruck oder langes Kürzel) im Schlüsselbund des
 * Bau-Nutzers steht; list = Ausgabe von gpg --with-colons --list-keys */
static int key_known(const char *list, const char *key, size_t klen)
{
	const char *l = list;
	while(l && *l) {
		const char *nl = strchr(l, '\n');
		size_t n = nl ? (size_t)(nl - l) : strlen(l);
		if(n > 4 && (strncmp(l, "fpr:", 4) == 0 || strncmp(l, "pub:", 4) == 0 || strncmp(l, "sub:", 4) == 0)) {
			/* fpr:::::::::FPR:  bzw. pub:u:255:22:KEYID:… */
			int field = strncmp(l, "fpr:", 4) == 0 ? 9 : 4;
			const char *c = l;
			for(int k = 0; k < field && c < l + n; k++) {
				c = memchr(c, ':', (size_t)(l + n - c));
				if(!c) break;
				c++;
			}
			if(c && c < l + n) {
				const char *ce = memchr(c, ':', (size_t)(l + n - c));
				size_t fl = ce ? (size_t)(ce - c) : (size_t)(l + n - c);
				if(fl >= klen && strncasecmp(c + fl - klen, key, klen) == 0) {
					return 1;
				}
			}
		}
		l = nl ? nl + 1 : NULL;
	}
	return 0;
}

/* Fehlende validpgpkeys anzeigen und (nach Rückfrage) importieren.
 * 0 = weiter bauen, -1 = abbrechen */
static int check_pgp_keys(aur_plan_t *plan)
{
	alpm_list_t *keys = NULL, *missing = NULL;
	char *list = NULL;
	int ret = 0;

	for(alpm_list_t *i = plan->bases; i; i = i->next) {
		job_t *j = job_for(((aur_base_t *)i->data)->pkgbase);
		if(j) {
			keys = srcinfo_pgpkeys(j, keys);
		}
	}
	if(!keys) {
		return 0;
	}
	if(!rc_have_program("gpg")) {
		rc_warn(_("gpg not found - cannot check validpgpkeys\n"));
		FREELIST(keys);
		return 0;
	}
	{
		const char *argv[] = { "gpg", "--batch", "--with-colons", "--list-keys", NULL };
		rc_run(argv, NULL, build_uid, build_gid, build_home, &list);
	}
	for(alpm_list_t *i = keys; i; i = i->next) {
		const char *ent = i->data, *sp = strchr(ent, ' ');
		if(!key_known(list, ent, (size_t)(sp - ent))) {
			missing = alpm_list_add(missing, strdup(ent));
		}
	}
	free(list);
	FREELIST(keys);
	if(!missing) {
		return 0;
	}

	rc_msg(_("PGP keys need importing:\n"));
	for(alpm_list_t *i = missing; i; i = i->next) {
		const char *ent = i->data, *sp = strchr(ent, ' ');
		printf(_(" -> %.*s, required by: %s\n"), (int)(sp - ent), ent, sp + 1);
	}
	if(!R.pgpfetch) {
		rc_warn(_("PGPFetch is off - makepkg will fail if the signatures cannot be verified\n"));
		goto done;
	}
	if(!rc_confirm(1, _("Import?"))) {
		rc_warn(_("keys not imported - makepkg will fail if the signatures cannot be verified\n"));
		goto done;
	}
	{
		size_t n = alpm_list_count(missing), k = 0;
		const char **argv = calloc(n + 6, sizeof(char *));
		alpm_list_t *fprs = NULL;
		if(!argv) {
			ret = -1;
			goto done;
		}
		argv[k++] = "gpg";
		argv[k++] = "--batch";
		if(R.keyserver && *R.keyserver) {
			argv[k++] = "--keyserver";
			argv[k++] = R.keyserver;
		}
		argv[k++] = "--recv-keys";
		for(alpm_list_t *i = missing; i; i = i->next) {
			const char *ent = i->data, *sp = strchr(ent, ' ');
			char *f = strndup(ent, (size_t)(sp - ent));
			fprs = alpm_list_add(fprs, f);
			argv[k++] = f;
		}
		argv[k] = NULL;
		rc_msg(_("Importing keys with gpg...\n"));
		if(rc_run(argv, NULL, build_uid, build_gid, build_home, NULL) != 0) {
			rc_err(_("problem importing keys\n"));
			ret = -1;
		}
		free(argv);
		FREELIST(fprs);
	}
done:
	FREELIST(missing);
	return ret;
}

/* ---------- Installation ---------- */

int rc_aur_install(alpm_list_t *targets, alpm_list_t *upgrades, alpm_list_t *devel)
{
	aur_resolver_t res = { 0 };
	aur_plan_t *plan = NULL;
	alpm_list_t *all = NULL, *before = NULL, *installed_by_us = NULL, *explicit = NULL;
	alpm_list_t *deps_by_us = NULL;  /* Laufzeit-Abh., die dieser Lauf installiert hat */
	char *d1 = NULL, *d2 = NULL;
	int ret = 1;

	/* Ziele prüfen */
	if(rc_aur_prefetch(targets) != 0) {
		return 1;
	}
	for(alpm_list_t *i = targets; i; i = i->next) {
		aur_pkg_t *p = rc_aur_info(i->data);
		alpm_pkg_t *lp = alpm_db_get_pkg(alpm_get_localdb(R.handle), i->data);
		if(R.needed && p && lp && alpm_pkg_vercmp(p->version, alpm_pkg_get_version(lp)) <= 0 &&
				!(R.devel && rc_is_vcs(p->name))) {
			rc_warn(_("%s-%s is up to date -- skipping\n"), p->name, alpm_pkg_get_version(lp));
			continue;
		}
		all = aur_list_add_unique(all, i->data);
		explicit = aur_list_add_unique(explicit, p ? p->name : i->data);
	}
	for(alpm_list_t *i = upgrades; i; i = i->next) all = aur_list_add_unique(all, i->data);
	for(alpm_list_t *i = devel; i; i = i->next) all = aur_list_add_unique(all, i->data);
	if(!all) {
		fputs(_(" there is nothing to do\n"), stdout);
		return 0;
	}

	d1 = path2(R.builddir, ".neu", NULL);
	d2 = path2(R.builddir, ".snap", NULL);
	if(!d1 || !d2 || rc_mkdir_p(R.builddir, 0755) != 0 || rc_mkdir_p(d1, 0755) != 0 ||
			rc_mkdir_p(d2, 0755) != 0) {
		rc_err(_("cannot create build directory %s: %s\n"), R.builddir, strerror(errno));
		goto done;
	}
	if(rc_build_user(&build_uid, &build_gid, &build_home) != 0) {
		goto done;
	}
	rc_build_path(build_uid, build_gid, build_home);
	if(geteuid() == 0) {
		/* Bau-Nutzer muss im Bauordner schreiben, Snapshots bleiben root */
		if(chown(R.builddir, 0, 0) != 0) {
			rc_warn("%s: %s\n", R.builddir, strerror(errno));
		}
		chmod(R.builddir, 0755);
	}

	rc_msg(_("Resolving dependencies...\n"));
	res.arch = R.arch;
	res.nocheck = R.nocheck;
	res.installed = cb_installed;
	res.repo = cb_repo;
	res.info = cb_info;
	res.providers = cb_providers;
	res.srcinfo = cb_srcinfo;
	res.choose = cb_choose;
	res.glibc_only = cb_glibc_only;
	plan = aur_resolve(&res, all);
	if(!plan) {
		goto done;
	}
	for(alpm_list_t *i = plan->bases; i; i = i->next) {
		aur_base_t *b = i->data;
		if(b->foreign_arch) {
			rc_warn(_("%s: arch=() does not list %s - building from source anyway (untested on musl)\n"),
					b->pkgbase, R.arch);
		}
	}
	for(alpm_list_t *i = plan->bases; i; i = i->next) {
		aur_base_t *b = i->data;
		job_t *j = job_for(b->pkgbase);
		if(j) {
			int only_devel = 1;
			j->b = b;
			for(alpm_list_t *k = b->pkgnames; k; k = k->next) {
				if(!alpm_list_find_str(devel, k->data) || alpm_list_find_str(targets, k->data) ||
						alpm_list_find_str(upgrades, k->data)) {
					only_devel = 0;
				}
			}
			j->devel = only_devel;
		}
	}
	if(plan->errors) {
		for(alpm_list_t *i = plan->errors; i; i = i->next) {
			rc_err("%s\n", (char *)i->data);
		}
		goto done;
	}

	show_plan(plan);
	if(!rc_confirm(1, _("Proceed with building?"))) {
		ret = 1;
		goto done;
	}

	if(!R.noconfirm && !R.nodiff) {
		for(alpm_list_t *i = plan->bases; i; i = i->next) {
			review(job_for(((aur_base_t *)i->data)->pkgbase));
		}
		if(!rc_confirm(1, _("PKGBUILDs reviewed - build and install?"))) {
			ret = 1;
			goto done;
		}
	}

	if(check_pgp_keys(plan) != 0) {
		ret = 1;
		goto done;
	}

	/* vorher installierte Pakete merken (für Installationsgrund/Aufräumen) */
	for(alpm_list_t *i = plan->bases; i; i = i->next) {
		aur_base_t *b = i->data;
		for(alpm_list_t *k = b->pkgnames; k; k = k->next) {
			if(alpm_db_get_pkg(alpm_get_localdb(R.handle), k->data)) {
				before = alpm_list_add(before, strdup(k->data));
			}
		}
	}

	if(plan->repo) {
		alpm_list_t *pre = rc_strlist("-S", "--asdeps", "--needed", NULL);
		rc_msg(_("Installing repo dependencies...\n"));
		ret = pacman_do(pre, 1, plan->repo);
		FREELIST(pre);
		if(ret != 0) {
			rc_err(_("failed to install repo dependencies\n"));
			goto done;
		}
		for(alpm_list_t *i = plan->repo_make; i; i = i->next) {
			installed_by_us = alpm_list_add(installed_by_us, strdup(i->data));
		}
		for(alpm_list_t *i = plan->repo; i; i = i->next) {
			if(!alpm_list_find_str(plan->repo_make, i->data)) {
				deps_by_us = alpm_list_add(deps_by_us, strdup(i->data));
			}
		}
	}

	ret = 0;
	for(alpm_list_t *i = plan->bases; i; i = i->next) {
		aur_base_t *b = i->data;
		job_t *j = job_for(b->pkgbase);
		alpm_list_t *files = NULL, *pre, *asdeps = NULL;
		int r = build_job(j, &files);
		if(r == 1) {
			continue;
		}
		if(r != 0) {
			ret = 1;
			break;
		}
		pre = rc_strlist("-U", NULL);
		if(R.asdeps) pre = alpm_list_add(pre, strdup("--asdeps"));
		if(R.asexplicit) pre = alpm_list_add(pre, strdup("--asexplicit"));
		rc_msg(_("Installing %s...\n"), b->pkgbase);
		r = pacman_do(pre, 1, files);
		FREELIST(pre);
		FREELIST(files);
		if(r != 0) {
			rc_err(_("failed to install %s\n"), b->pkgbase);
			ret = 1;
			break;
		}
		for(alpm_list_t *k = b->pkgnames; k; k = k->next) {
			if(alpm_list_find_str(before, k->data)) {
				continue;
			}
			if(!alpm_list_find_str(explicit, k->data) && !R.asdeps && !R.asexplicit) {
				asdeps = alpm_list_add(asdeps, strdup(k->data));
				if(!aur_plan_is_makeonly(plan, b, k->data)) {
					deps_by_us = alpm_list_add(deps_by_us, strdup(k->data));
				}
			}
			if(aur_plan_is_makeonly(plan, b, k->data)) {
				installed_by_us = alpm_list_add(installed_by_us, strdup(k->data));
			}
		}
		if(asdeps) {
			alpm_list_t *dpre = rc_strlist("-D", "--asdeps", NULL);
			if(pacman_do(dpre, 0, asdeps) != 0) {
				rc_warn(_("could not set install reason for %s\n"), b->pkgbase);
			}
			FREELIST(dpre);
			FREELIST(asdeps);
		}
		/* Snapshot gilt jetzt als geprüft und gebaut */
		rc_rm_rf(j->snap);
		if(rename(j->neu, j->snap) != 0) {
			rc_warn("%s: %s\n", j->snap, strerror(errno));
		}
	}

	/* Bau gescheitert: Abhängigkeiten, die nur für die nicht installierten
	 * Ziele geholt wurden, wären jetzt Waisen. -Rsu entfernt nur, was nichts
	 * anderes mehr braucht. */
	if(ret != 0 && deps_by_us) {
		if(R.removemake) {
			for(alpm_list_t *i = deps_by_us; i; i = i->next) {
				installed_by_us = alpm_list_add(installed_by_us, strdup(i->data));
			}
		} else {
			rc_warn(_("build failed - dependencies installed for it remain (remove with rcyay -Yc):\n"));
			for(alpm_list_t *i = deps_by_us; i; i = i->next) {
				printf("    %s\n", (char *)i->data);
			}
		}
	}

	if(installed_by_us && R.removemake) {
		alpm_list_t *pre = rc_strlist("-R", "-s", "-u", "--noconfirm", NULL);
		rc_msg(_("Removing make dependencies...\n"));
		if(pacman_do(pre, 0, installed_by_us) != 0) {
			rc_warn(_("could not remove (all) make dependencies\n"));
		}
		FREELIST(pre);
	}

done:
	aur_plan_free(plan);
	jobs_free();
	FREELIST(all);
	FREELIST(before);
	FREELIST(installed_by_us);
	FREELIST(deps_by_us);
	FREELIST(explicit);
	free(d1);
	free(d2);
	free(build_home);
	build_home = NULL;
	return ret;
}

/* ---------- -S ---------- */

int rc_op_sync(void)
{
	alpm_list_t *repo = NULL, *aur = NULL, *ups = NULL, *devel = NULL;
	int ret = 0;
	unsigned int y = R.cnt['y'], u = R.cnt['u'];

	if(R.cnt['s']) {
		return rc_op_search();
	}
	if(R.cnt['i']) {
		return rc_op_info();
	}
	if(R.cnt['c'] || R.cnt['g'] || R.cnt['l'] || R.cnt['p'] || R.cnt['w'] || R.repo_only) {
		return rc_pacman_inproc(R.tokens, R.targets);
	}
	if(!R.targets && !u) {
		if(!y || R.aur_only) {
			return y ? 0 : rc_pacman_inproc(R.tokens, NULL);
		}
		return rc_pacman_inproc(R.tokens, NULL);
	}

	/* VirtualProvides: Metapaket vor der ersten Transaktion anpassen */
	if(rc_vprov_sync() != 0) {
		return 1;
	}

	if(y && !R.aur_only) {
		alpm_list_t *a = NULL;
		for(alpm_list_t *i = R.global; i; i = i->next) a = alpm_list_add(a, strdup(i->data));
		a = alpm_list_add(a, strdup("-S"));
		for(unsigned int k = 0; k < y; k++) a = alpm_list_add(a, strdup("-y"));
		ret = rc_pacman(a, 1);
		FREELIST(a);
		if(ret != 0 || rc_alpm_reload() != 0) {
			return ret ? ret : 1;
		}
	}

	for(alpm_list_t *i = R.targets; i; i = i->next) {
		const char *t = i->data;
		if(!R.aur_only && rc_is_repo_target(t)) {
			repo = alpm_list_add(repo, (char *)t);
		} else {
			aur = alpm_list_add(aur, strdup(strncmp(t, "aur/", 4) == 0 ? t + 4 : t));
		}
	}

	if(!R.aur_only && (repo || u)) {
		alpm_list_t *tok = rc_tokens_without(R.tokens, "-y", "--refresh"), *a = NULL;
		for(alpm_list_t *i = tok; i; i = i->next) a = alpm_list_add(a, i->data);
		for(alpm_list_t *i = repo; i; i = i->next) a = alpm_list_add(a, i->data);
		ret = rc_pacman(a, 1);
		alpm_list_free(a);
		alpm_list_free(tok);
		if(ret != 0) {
			goto out;
		}
		if(rc_alpm_reload() != 0) {
			ret = 1;
			goto out;
		}
	}

	if(u) {
		ups = rc_aur_upgrades(&devel);
	}
	if(aur || ups || devel) {
		ret = rc_aur_install(aur, ups, devel);
	}
out:
	alpm_list_free(repo);
	FREELIST(aur);
	FREELIST(ups);
	FREELIST(devel);
	return ret;
}
