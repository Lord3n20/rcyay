/*
 *  aur.c - AUR-Abfragen: Cache, Suche (-Ss), Info (-Si), Bauordner (-G)
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
#include <fnmatch.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config.h"
#include "conf.h"
#include "util.h"
#include "rcyay.h"
#include "aurutil.h"

/* ---------- RPC-Cache ---------- */

static int cached(const char *name, aur_pkg_t **out)
{
	aur_pkg_t *p = aur_pkglist_find(R.aurcache, name);
	if(p) {
		*out = p;
		return 1;
	}
	if(alpm_list_find_str(R.aurmiss, name)) {
		*out = NULL;
		return 1;
	}
	return 0;
}

int rc_aur_prefetch(alpm_list_t *names)
{
	alpm_list_t *want = NULL, *res = NULL;
	aur_pkg_t *dummy;
	for(alpm_list_t *i = names; i; i = i->next) {
		if(!cached(i->data, &dummy) && !alpm_list_find_str(want, i->data)) {
			want = alpm_list_add(want, i->data);
		}
	}
	if(!want) {
		return 0;
	}
	if(aur_info(R.ctx, want, &res) != 0) {
		rc_err(_("AUR query failed: %s\n"), R.ctx->error);
		alpm_list_free(want);
		return -1;
	}
	for(alpm_list_t *i = want; i; i = i->next) {
		aur_pkg_t *p = aur_pkglist_find(res, i->data);
		if(!p) {
			R.aurmiss = alpm_list_add(R.aurmiss, strdup(i->data));
		}
	}
	for(alpm_list_t *i = res; i; i = i->next) {
		aur_pkg_t *p = i->data;
		if(aur_pkglist_find(R.aurcache, p->name)) {
			aur_pkg_free(p);
		} else {
			R.aurcache = alpm_list_add(R.aurcache, p);
		}
	}
	alpm_list_free(res);
	alpm_list_free(want);
	return 0;
}

aur_pkg_t *rc_aur_info(const char *name)
{
	aur_pkg_t *p = NULL;
	alpm_list_t one = { (void *)name, NULL, NULL };
	one.prev = &one;
	if(cached(name, &p)) {
		return p;
	}
	if(rc_aur_prefetch(&one) != 0) {
		return NULL;
	}
	cached(name, &p);
	return p;
}

/* AUR-Pakete, die name bereitstellen (Einträge im Cache) */
alpm_list_t *rc_aur_providers(const char *name)
{
	alpm_list_t *found = NULL, *names = NULL, *r = NULL;
	if(!alpm_list_find_str(R.provcache, name)) {
		if(aur_search(R.ctx, name, "provides", &found) != 0) {
			rc_warn(_("AUR search for providers of %s failed: %s\n"), name, R.ctx->error);
		}
		for(alpm_list_t *i = found; i; i = i->next) {
			names = alpm_list_add(names, ((aur_pkg_t *)i->data)->name);
		}
		/* search liefert keine provides-Listen – info nachladen */
		rc_aur_prefetch(names);
		alpm_list_free(names);
		aur_pkglist_free(found);
		R.provcache = alpm_list_add(R.provcache, strdup(name));
	}
	for(alpm_list_t *i = R.aurcache; i; i = i->next) {
		aur_pkg_t *p = i->data;
		for(alpm_list_t *j = p->provides; j; j = j->next) {
			char *n = aur_dep_name(j->data);
			if(n && strcmp(n, name) == 0 && !alpm_list_find_ptr(r, p)) {
				r = alpm_list_add(r, p);
			}
			free(n);
		}
	}
	return r;
}

/* ---------- Repo ---------- */

static alpm_db_t *find_syncdb(const char *name)
{
	for(alpm_list_t *i = alpm_get_syncdbs(R.handle); i; i = i->next) {
		if(strcmp(alpm_db_get_name(i->data), name) == 0) {
			return i->data;
		}
	}
	return NULL;
}

/* Repo-Paket zu einem Ziel ("name", "repo/name"), nur nach Namen */
alpm_pkg_t *rc_repo_find(const char *target)
{
	const char *slash = strchr(target, '/');
	if(slash) {
		char *db = strndup(target, (size_t)(slash - target));
		alpm_db_t *d = find_syncdb(db);
		free(db);
		return d ? alpm_db_get_pkg(d, slash + 1) : NULL;
	}
	for(alpm_list_t *i = alpm_get_syncdbs(R.handle); i; i = i->next) {
		alpm_pkg_t *p = alpm_db_get_pkg(i->data, target);
		if(p) {
			return p;
		}
	}
	return NULL;
}

/* Ziel für -S: gehört es in die Repos (Paket, provides, Gruppe, repo/…)? */
int rc_is_repo_target(const char *target)
{
	const char *slash = strchr(target, '/');
	if(slash) {
		if(strncmp(target, "aur/", 4) == 0) {
			return 0;
		}
		return 1; /* repo/name – pacman meldet Fehler selbst */
	}
	if(rc_repo_find(target)) {
		return 1;
	}
	for(alpm_list_t *i = alpm_get_syncdbs(R.handle); i; i = i->next) {
		if(alpm_db_get_group(i->data, target)) {
			return 1;
		}
	}
	/* nur über provides im Repo (z. B. "sh") – wie pacman zuerst Repo */
	for(alpm_list_t *i = alpm_get_syncdbs(R.handle); i; i = i->next) {
		if(alpm_find_satisfier(alpm_db_get_pkgcache(i->data), target)) {
			return 1;
		}
	}
	return 0;
}

/* ---------- -Ss ---------- */

/* Sortierung wie yay: votes (Standard), popularity, name, modified;
 * "beste" zuerst – die Anzeige dreht bei BottomUp um */
static int pkg_cmp(const void *a, const void *b)
{
	const aur_pkg_t *x = a, *y = b;
	const char *by = R.sortby ? R.sortby : "votes";
	if(strcmp(by, "name") == 0) {
		return strcmp(x->name, y->name);
	} else if(strcmp(by, "popularity") == 0) {
		if(x->popularity > y->popularity) return -1;
		if(x->popularity < y->popularity) return 1;
	} else if(strcmp(by, "modified") == 0) {
		if(x->lastmodified != y->lastmodified) return x->lastmodified > y->lastmodified ? -1 : 1;
	} else {
		if(x->votes != y->votes) return x->votes > y->votes ? -1 : 1;
		if(x->popularity > y->popularity) return -1;
		if(x->popularity < y->popularity) return 1;
	}
	return strcmp(x->name, y->name);
}

static int matches_all(aur_pkg_t *p, alpm_list_t *terms)
{
	for(alpm_list_t *i = terms; i; i = i->next) {
		const char *t = i->data;
		if(!strcasestr(p->name, t) && !(p->desc && strcasestr(p->desc, t))) {
			return 0;
		}
	}
	return 1;
}

void rc_print_aur_search(alpm_list_t *pkgs, int quiet, int numbered, int start)
{
	const colstr_t *c = &config->colstr;
	alpm_db_t *local = alpm_get_localdb(R.handle);
	unsigned short cols = getcols();
	int n = start;
	for(alpm_list_t *i = pkgs; i; i = i->next, n--) {
		aur_pkg_t *p = i->data;
		alpm_pkg_t *lp = alpm_db_get_pkg(local, p->name);
		if(quiet) {
			printf("%s\n", p->name);
			continue;
		}
		if(numbered) {
			printf("%s%d%s ", c->meta, n, c->nocolor);
		}
		printf("%saur/%s%s %s%s%s %s(+%ld %.2f)%s", c->repo, c->title, p->name,
				c->version, p->version, c->nocolor, c->meta, p->votes, p->popularity, c->nocolor);
		if(p->outofdate) {
			char buf[32];
			time_t t = (time_t)p->outofdate;
			strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&t));
			printf(" %s(%s: %s)%s", c->err, _("Out-of-date"), buf, c->nocolor);
		}
		if(!p->maintainer) {
			printf(" %s%s%s", c->warn, _("(orphaned)"), c->nocolor);
		}
		if(lp) {
			const char *lv = alpm_pkg_get_version(lp);
			if(strcmp(lv, p->version) == 0) {
				printf(" %s[%s]%s", c->meta, P_("installed"), c->nocolor);
			} else {
				printf(" %s[%s: %s]%s", c->meta, P_("installed"), lv, c->nocolor);
			}
		}
		fputs("\n    ", stdout);
		indentprint(p->desc ? p->desc : "", 4, cols);
		fputc('\n', stdout);
	}
}

/* AUR-Suche mit allen Begriffen (sortiert nach Namen); NULL bei Fehler/leer */
static int aur_search_terms(alpm_list_t *terms, alpm_list_t **out)
{
	const char *best = NULL;
	alpm_list_t *res = NULL, *keep = NULL;
	*out = NULL;
	for(alpm_list_t *i = terms; i; i = i->next) {
		if(!best || strlen(i->data) > strlen(best)) {
			best = i->data;
		}
	}
	if(!best) {
		return 0;
	}
	if(aur_search(R.ctx, best, "name-desc", &res) != 0) {
		rc_err(_("AUR search failed: %s\n"), R.ctx->error);
		return -1;
	}
	for(alpm_list_t *i = res; i; i = i->next) {
		if(matches_all(i->data, terms)) {
			keep = alpm_list_add(keep, i->data);
		} else {
			aur_pkg_free(i->data);
		}
	}
	alpm_list_free(res);
	*out = alpm_list_msort(keep, alpm_list_count(keep), pkg_cmp);
	return 0;
}

int rc_aur_search_list(alpm_list_t *terms, alpm_list_t **out)
{
	return aur_search_terms(terms, out);
}

static void print_aur_block(alpm_list_t *res)
{
	alpm_list_t *show = R.bottomup ? alpm_list_reverse(res) : alpm_list_copy(res);
	rc_print_aur_search(show, R.cnt['q'] > 0, 0, 0);
	alpm_list_free(show);
}

int rc_op_search(void)
{
	int found = 0, ret = 0;
	alpm_list_t *res = NULL;

	/* wie yay: bei BottomUp stehen die besten Treffer (Repos) unten */
	if(!R.repo_only && R.targets) {
		if(aur_search_terms(R.targets, &res) != 0) {
			return 1;
		}
		if(res) {
			found = 1;
		}
	}
	if(res && R.bottomup) {
		print_aur_block(res);
	}
	if(!R.aur_only) {
		alpm_list_t *args = NULL;
		int r;
		for(alpm_list_t *i = R.tokens; i; i = i->next) args = alpm_list_add(args, i->data);
		for(alpm_list_t *i = R.targets; i; i = i->next) args = alpm_list_add(args, i->data);
		r = rc_pacman(args, 0);
		alpm_list_free(args);
		if(r == 0) {
			found = 1;
		} else if(r != 1) {
			ret = r;
		}
	}
	if(res && !R.bottomup) {
		print_aur_block(res);
	}
	aur_pkglist_free(res);
	if(ret) {
		return ret;
	}
	return found ? 0 : 1;
}

/* Feldnamen wie pacman -Si: übersetzt, auf gleiche Breite aufgefüllt */
enum {
	F_REPO, F_NAME, F_BASE, F_VERSION, F_DESC, F_URL, F_AURURL, F_GROUPS, F_LICENSES,
	F_PROVIDES, F_DEPENDS, F_MAKEDEPS, F_CHECKDEPS, F_OPTDEPS, F_CONFLICTS, F_REPLACES,
	F_KEYWORDS, F_MAINT, F_COMAINT, F_VOTES, F_POP, F_FIRST, F_LAST, F_OOD, F_COUNT
};

static size_t u8len(const char *s)
{
	size_t n = 0;
	for(; *s; s++) {
		if(((unsigned char)*s & 0xC0) != 0x80) n++;
	}
	return n;
}

static char *labels[F_COUNT];

static void make_labels(void)
{
	const char *raw[F_COUNT] = {
		_("Repository"), _("Name"), _("Package Base"), _("Version"), _("Description"),
		_("URL"), _("AUR URL"), _("Groups"), _("Licenses"), _("Provides"), _("Depends On"),
		_("Make Deps"), _("Check Deps"), _("Optional Deps"), _("Conflicts With"),
		_("Replaces"), _("Keywords"), _("Maintainer"), _("Co-Maintainers"), _("Votes"),
		_("Popularity"), _("First Submitted"), _("Last Modified"), _("Out-of-date")
	};
	size_t max = 0;
	if(labels[0]) {
		return;
	}
	for(int i = 0; i < F_COUNT; i++) {
		if(u8len(raw[i]) > max) max = u8len(raw[i]);
	}
	for(int i = 0; i < F_COUNT; i++) {
		size_t pad = max - u8len(raw[i]);
		if(asprintf(&labels[i], "%s%*s :", raw[i], (int)pad, "") < 0) {
			labels[i] = NULL;
		}
	}
}

static void show_time(const char *title, long t, unsigned short cols)
{
	char buf[64];
	time_t tt = (time_t)t;
	if(t <= 0) {
		string_display(title, NULL, cols);
		return;
	}
	strftime(buf, sizeof(buf), "%c", localtime(&tt));
	string_display(title, buf, cols);
}

static void aur_show_info(aur_pkg_t *p)
{
	unsigned short cols = getcols();
	char buf[128], *aurl = NULL;
	char **L = labels;
	make_labels();
	string_display(L[F_REPO], "aur", cols);
	string_display(L[F_NAME], p->name, cols);
	string_display(L[F_BASE], p->pkgbase, cols);
	string_display(L[F_VERSION], p->version, cols);
	string_display(L[F_DESC], p->desc, cols);
	string_display(L[F_URL], p->url, cols);
	if(asprintf(&aurl, "%s/packages/%s", R.ctx->baseurl, p->name) >= 0) {
		string_display(L[F_AURURL], aurl, cols);
		free(aurl);
	}
	list_display(L[F_GROUPS], p->groups, cols);
	list_display(L[F_LICENSES], p->licenses, cols);
	list_display(L[F_PROVIDES], p->provides, cols);
	list_display(L[F_DEPENDS], p->depends, cols);
	list_display(L[F_MAKEDEPS], p->makedepends, cols);
	list_display(L[F_CHECKDEPS], p->checkdepends, cols);
	list_display_linebreak(L[F_OPTDEPS], p->optdepends, cols);
	list_display(L[F_CONFLICTS], p->conflicts, cols);
	list_display(L[F_REPLACES], p->replaces, cols);
	list_display(L[F_KEYWORDS], p->keywords, cols);
	string_display(L[F_MAINT], p->maintainer ? p->maintainer : _("(orphaned)"), cols);
	list_display(L[F_COMAINT], p->comaintainers, cols);
	snprintf(buf, sizeof(buf), "%ld", p->votes);
	string_display(L[F_VOTES], buf, cols);
	snprintf(buf, sizeof(buf), "%.2f", p->popularity);
	string_display(L[F_POP], buf, cols);
	show_time(L[F_FIRST], p->firstsubmitted, cols);
	show_time(L[F_LAST], p->lastmodified, cols);
	if(p->outofdate) {
		show_time(L[F_OOD], p->outofdate, cols);
	} else {
		string_display(L[F_OOD], P_("No"), cols);
	}
	printf("\n");
}

int rc_op_info(void)
{
	alpm_list_t *repo = NULL, *aur = NULL, *names = NULL;
	int ret = 0;

	if(!R.targets) {
		return rc_pacman_inproc(R.tokens, NULL);
	}
	for(alpm_list_t *i = R.targets; i; i = i->next) {
		const char *t = i->data;
		if(!R.aur_only && strncmp(t, "aur/", 4) != 0 && rc_repo_find(t)) {
			repo = alpm_list_add(repo, i->data);
		} else if(!R.repo_only) {
			aur = alpm_list_add(aur, (char *)(strncmp(t, "aur/", 4) == 0 ? t + 4 : t));
		} else {
			repo = alpm_list_add(repo, i->data);
		}
	}
	if(repo) {
		alpm_list_t *args = NULL;
		for(alpm_list_t *i = R.tokens; i; i = i->next) args = alpm_list_add(args, i->data);
		for(alpm_list_t *i = repo; i; i = i->next) args = alpm_list_add(args, i->data);
		ret = rc_pacman(args, 0);
		alpm_list_free(args);
	}
	if(aur) {
		if(rc_aur_prefetch(aur) != 0) {
			ret = 1;
		} else {
			for(alpm_list_t *i = aur; i; i = i->next) {
				aur_pkg_t *p = rc_aur_info(i->data);
				if(p) {
					aur_show_info(p);
				} else {
					rc_err(_("package '%s' was not found\n"), (char *)i->data);
					ret = 1;
				}
			}
		}
	}
	alpm_list_free(repo);
	alpm_list_free(aur);
	alpm_list_free(names);
	return ret;
}

/* ---------- Upgrades ---------- */

int rc_is_vcs(const char *name)
{
	static const char *suf[] = { "-git", "-svn", "-hg", "-bzr", "-darcs", "-cvs", "-fossil", NULL };
	size_t n = strlen(name);
	for(const char **s = suf; *s; s++) {
		size_t k = strlen(*s);
		if(n > k && strcmp(name + n - k, *s) == 0) {
			return 1;
		}
	}
	return 0;
}

static int ignored(const char *name)
{
	for(alpm_list_t *i = config->ignorepkg; i; i = i->next) {
		if(fnmatch(i->data, name, 0) == 0) {
			return 1;
		}
	}
	return 0;
}

/* Fremdpakete (nicht in Repos), die im AUR neuer sind. *devel bekommt
 * VCS-Pakete (Version erst nach pkgver() bekannt). */
alpm_list_t *rc_aur_upgrades(alpm_list_t **devel)
{
	alpm_list_t *foreign = NULL, *ups = NULL, *missing = NULL;
	alpm_db_t *local = alpm_get_localdb(R.handle);

	*devel = NULL;
	for(alpm_list_t *i = alpm_db_get_pkgcache(local); i; i = i->next) {
		alpm_pkg_t *lp = i->data;
		if(!rc_repo_find(alpm_pkg_get_name(lp))) {
			foreign = alpm_list_add(foreign, (char *)alpm_pkg_get_name(lp));
		}
	}
	if(!foreign) {
		return NULL;
	}
	rc_msg(_("Searching AUR for updates...\n"));
	if(rc_aur_prefetch(foreign) != 0) {
		alpm_list_free(foreign);
		return NULL;
	}
	for(alpm_list_t *i = foreign; i; i = i->next) {
		const char *name = i->data;
		alpm_pkg_t *lp = alpm_db_get_pkg(local, name);
		aur_pkg_t *p = rc_aur_info(name);
		const char *lv = alpm_pkg_get_version(lp);
		if(!p) {
			missing = alpm_list_add(missing, (char *)name);
			continue;
		}
		if(alpm_pkg_vercmp(p->version, lv) > 0) {
			if(ignored(name)) {
				rc_warn(_("%s: ignoring package upgrade (%s => %s)\n"), name, lv, p->version);
				continue;
			}
			printf(" %s %s%s%s -> %s%s%s\n", name, config->colstr.err, lv, config->colstr.nocolor,
					config->colstr.version, p->version, config->colstr.nocolor);
			ups = alpm_list_add(ups, strdup(name));
		} else if(R.devel && rc_is_vcs(name) && !ignored(name)) {
			*devel = alpm_list_add(*devel, strdup(name));
		}
	}
	if(missing && config->verbose == 0) {
		char *s = NULL, *n;
		for(alpm_list_t *i = missing; i; i = i->next) {
			if(asprintf(&n, "%s%s%s", s ? s : "", s ? " " : "", (char *)i->data) >= 0) {
				free(s);
				s = n;
			}
		}
		rc_warn(_("not in the AUR (nor in any repository): %s\n"), s ? s : "");
		free(s);
	}
	if(!ups && !*devel) {
		fputs(_(" there is nothing to do in the AUR\n"), stdout);
	}
	alpm_list_free(missing);
	alpm_list_free(foreign);
	return ups;
}

/* ---------- -G / -Gp ---------- */

/* gitlab-Projektname wie bei Arch (archlinux/packaging) */
static char *gitlab_project(const char *pkgbase)
{
	size_t n = strlen(pkgbase);
	char *r = malloc(n * 4 + 1), *o = r;
	if(!r) return NULL;
	for(const char *s = pkgbase; *s; s++) {
		if(*s == '+') {
			if(s[1] == '+') {
				memcpy(o, "plusplus", 8);
				o += 8;
				s++;
			} else {
				memcpy(o, "plus", 4);
				o += 4;
			}
		} else if(*s == '@') {
			*o++ = '-';
		} else {
			*o++ = *s;
		}
	}
	*o = '\0';
	return r;
}

/* -G nimmt auch pkgbase-Namen (z. B. geteilte Pakete) */
static aur_pkg_t *find_pkgbase(const char *base)
{
	alpm_list_t *res = NULL;
	aur_pkg_t *hit = NULL;
	if(strlen(base) < 2 || aur_search(R.ctx, base, "name", &res) != 0) {
		return NULL;
	}
	for(alpm_list_t *i = res; i; i = i->next) {
		aur_pkg_t *p = i->data;
		if(!hit && strcmp(p->pkgbase, base) == 0) {
			hit = p;
			R.aurcache = alpm_list_add(R.aurcache, p);
		} else {
			aur_pkg_free(p);
		}
	}
	alpm_list_free(res);
	return hit;
}

static int get_one(const char *target, int print)
{
	const char *name = target;
	alpm_pkg_t *rp = NULL;
	char *url = NULL, *dest = NULL, *tarball = NULL, err[512];
	const char *base;
	int repo = 0, ret = 1;
	long status;

	if(strncmp(name, "aur/", 4) == 0) {
		name += 4;
	} else if(!R.aur_only) {
		rp = rc_repo_find(name);
		if(rp) {
			repo = 1;
		}
	}
	if(repo) {
		char *proj;
		base = alpm_pkg_get_base(rp) ? alpm_pkg_get_base(rp) : alpm_pkg_get_name(rp);
		proj = gitlab_project(base);
		if(print) {
			if(asprintf(&url, "%s/%s/-/raw/main/PKGBUILD", R.repogiturl, proj) < 0) url = NULL;
		} else if(asprintf(&url, "%s/%s/-/archive/main/%s-main.tar.gz", R.repogiturl, proj, proj) < 0) {
			url = NULL;
		}
		free(proj);
	} else {
		aur_pkg_t *p;
		if(R.repo_only) {
			rc_err(_("package '%s' was not found in any repository\n"), name);
			return 1;
		}
		p = rc_aur_info(name);
		if(!p) {
			p = find_pkgbase(name);
		}
		if(!p) {
			rc_err(_("package '%s' was found neither in the repositories nor in the AUR\n"), name);
			return 1;
		}
		base = p->pkgbase;
		url = print ? aur_plain_url(R.ctx, base, "PKGBUILD") : aur_snapshot_url(R.ctx, p);
	}
	if(!url) {
		return 1;
	}
	if(print) {
		char *body = NULL;
		size_t len = 0;
		if(aur_http_get(R.ctx, url, &body, &len, &status) != 0) {
			rc_err(_("cannot fetch PKGBUILD of %s: %s\n"), base, R.ctx->error);
		} else {
			fwrite(body, 1, len, stdout);
			ret = 0;
		}
		free(body);
		free(url);
		return ret;
	}
	dest = strdup(base);
	if(asprintf(&tarball, ".%s.rcyay.tar.gz", base) < 0) {
		tarball = NULL;
		goto out;
	}
	if(aur_http_download(R.ctx, url, tarball, &status) != 0) {
		rc_err(_("download of %s failed: %s\n"), base, R.ctx->error);
		goto out;
	}
	{
		struct stat st;
		int existed = stat(dest, &st) == 0;
		if(aur_extract(tarball, dest, 1, err, sizeof(err)) != 0) {
			rc_err("%s\n", err);
		} else {
			printf(":: %s %s (%s)\n", existed ? _("Updated:") : _("Downloaded:"), dest,
					repo ? alpm_db_get_name(alpm_pkg_get_db(rp)) : "aur");
			ret = 0;
		}
	}
	unlink(tarball);
out:
	free(url);
	free(dest);
	free(tarball);
	return ret;
}

int rc_op_getpkgbuild(void)
{
	int ret = 0;
	alpm_list_t *aurnames = NULL;
	if(!R.targets) {
		rc_err(_("no targets specified (rcyay -G PACKAGE ...)\n"));
		return 1;
	}
	for(alpm_list_t *i = R.targets; i; i = i->next) {
		const char *t = i->data;
		if(strncmp(t, "aur/", 4) == 0) {
			aurnames = alpm_list_add(aurnames, (char *)t + 4);
		} else if(R.aur_only || !rc_repo_find(t)) {
			aurnames = alpm_list_add(aurnames, (char *)t);
		}
	}
	if(aurnames) {
		rc_aur_prefetch(aurnames);
		alpm_list_free(aurnames);
	}
	for(alpm_list_t *i = R.targets; i; i = i->next) {
		if(get_one(i->data, R.cnt['p'] > 0) != 0) {
			ret = 1;
		}
	}
	return ret;
}
