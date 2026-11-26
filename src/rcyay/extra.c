/*
 *  extra.c - -Yc (Aufräumen), -Ps (Statistik), interaktive Suche, Hilfe
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
#include <ctype.h>

#include "config.h"
#include "conf.h"
#include "util.h"
#include "rcyay.h"
#include "aurutil.h"

/* ---------- -Yc ---------- */

int rc_op_clean(void)
{
	alpm_list_t *names = NULL, *a = NULL;
	int ret;
	for(alpm_list_t *i = alpm_db_get_pkgcache(alpm_get_localdb(R.handle)); i; i = i->next) {
		alpm_pkg_t *p = i->data;
		alpm_list_t *req, *opt;
		if(alpm_pkg_get_reason(p) != ALPM_PKG_REASON_DEPEND) {
			continue;
		}
		req = alpm_pkg_compute_requiredby(p);
		opt = alpm_pkg_compute_optionalfor(p);
		if(!req && !opt) {
			names = alpm_list_add(names, strdup(alpm_pkg_get_name(p)));
		}
		FREELIST(req);
		FREELIST(opt);
	}
	if(!names) {
		fputs(_(" no unneeded dependencies found\n"), stdout);
		return 0;
	}
	for(alpm_list_t *i = R.global; i; i = i->next) a = alpm_list_add(a, strdup(i->data));
	a = alpm_list_add(a, strdup("-Rns"));
	for(alpm_list_t *i = R.trans; i; i = i->next) a = alpm_list_add(a, strdup(i->data));
	for(alpm_list_t *i = names; i; i = i->next) a = alpm_list_add(a, strdup(i->data));
	ret = rc_pacman(a, 1);
	FREELIST(a);
	FREELIST(names);
	return ret;
}

/* ---------- -Ps ---------- */

static size_t u8len(const char *s)
{
	size_t n = 0;
	for(; *s; s++) {
		if(((unsigned char)*s & 0xC0) != 0x80) n++;
	}
	return n;
}

int rc_op_stats(void)
{
	alpm_list_t *pkgs = alpm_db_get_pkgcache(alpm_get_localdb(R.handle)), *foreign = NULL;
	size_t total = 0, expl = 0, deps = 0, nforeign = 0;
	off_t size = 0, fsize = 0;
	const char *unit;
	double hs;
	alpm_pkg_t *big[10] = { 0 };

	for(alpm_list_t *i = pkgs; i; i = i->next) {
		alpm_pkg_t *p = i->data;
		off_t s = alpm_pkg_get_isize(p);
		total++;
		size += s;
		if(alpm_pkg_get_reason(p) == ALPM_PKG_REASON_EXPLICIT) expl++; else deps++;
		if(!rc_repo_find(alpm_pkg_get_name(p))) {
			nforeign++;
			fsize += s;
			foreign = alpm_list_add(foreign, p);
		}
		for(int k = 0; k < 10; k++) {
			if(!big[k] || s > alpm_pkg_get_isize(big[k])) {
				memmove(&big[k + 1], &big[k], sizeof(big[0]) * (size_t)(9 - k));
				big[k] = p;
				break;
			}
		}
	}
	{
		const char *lab[] = { _("Installed packages"), _("Explicitly installed"),
			_("Installed as dependency"), _("Foreign (AUR/locally built)"), _("Total size"),
			_("Size of foreign packages"), _("Repositories") };
		size_t w = 0;
		for(size_t k = 0; k < sizeof(lab) / sizeof(lab[0]); k++) {
			if(u8len(lab[k]) > w) w = u8len(lab[k]);
		}
#define LAB(k) printf(" %s%*s : ", lab[k], (int)(w - u8len(lab[k])), "")
		printf(_("%s==> rcyay v%s - package statistics%s\n"), config->colstr.title, RCYAY_VERSION,
				config->colstr.nocolor);
		LAB(0); printf("%zu\n", total);
		LAB(1); printf("%zu\n", expl);
		LAB(2); printf("%zu\n", deps);
		LAB(3); printf("%zu\n", nforeign);
		hs = humanize_size(size, '\0', 2, &unit);
		LAB(4); printf("%.2f %s\n", hs, unit);
		hs = humanize_size(fsize, '\0', 2, &unit);
		LAB(5); printf("%.2f %s\n", hs, unit);
		LAB(6);
#undef LAB
	}
	for(alpm_list_t *i = alpm_get_syncdbs(R.handle); i; i = i->next) {
		printf("%s ", alpm_db_get_name(i->data));
	}
	printf(_("\n%s==> Ten biggest packages%s\n"), config->colstr.title, config->colstr.nocolor);
	for(int k = 0; k < 10 && big[k]; k++) {
		hs = humanize_size(alpm_pkg_get_isize(big[k]), '\0', 2, &unit);
		printf(" %-30s %8.2f %s\n", alpm_pkg_get_name(big[k]), hs, unit);
	}
	alpm_list_free(foreign);
	return 0;
}

/* ---------- interaktive Suche (rcyay WORT …) ---------- */

static int parse_selection(const char *line, int count, char *sel)
{
	char *buf = strdup(line), *tok, *save = NULL;
	int any = 0, excl_only = 1;
	char *excl = calloc((size_t)count + 1, 1);
	if(!buf || !excl) {
		free(buf);
		free(excl);
		return -1;
	}
	memset(sel, 0, (size_t)count + 1);
	for(tok = strtok_r(buf, " ,\t\n", &save); tok; tok = strtok_r(NULL, " ,\t\n", &save)) {
		int neg = 0, a, b;
		char *dash;
		if(*tok == '^') {
			neg = 1;
			tok++;
		} else {
			excl_only = 0;
		}
		dash = strchr(tok, '-');
		a = atoi(tok);
		b = dash ? atoi(dash + 1) : a;
		if(a < 1 || b < a || b > count) {
			free(buf);
			free(excl);
			return -1;
		}
		for(int k = a; k <= b; k++) {
			if(neg) excl[k] = 1; else sel[k] = 1;
		}
		any = 1;
	}
	if(any && excl_only) {
		for(int k = 1; k <= count; k++) sel[k] = 1;
	}
	for(int k = 1; k <= count; k++) {
		if(excl[k]) sel[k] = 0;
	}
	free(buf);
	free(excl);
	return any ? 0 : 1;
}

typedef struct {
	alpm_pkg_t *repo;
	aur_pkg_t *aur;
} entry_t;

static void print_entry(entry_t *e, int n, alpm_db_t *local, unsigned short cols)
{
	const colstr_t *c = &config->colstr;
	if(e->aur) {
		alpm_list_t one = { e->aur, NULL, NULL };
		one.prev = &one;
		rc_print_aur_search(&one, 0, 1, n);
		return;
	}
	printf("%s%d%s %s%s/%s%s %s%s%s", c->meta, n, c->nocolor, c->repo,
			alpm_db_get_name(alpm_pkg_get_db(e->repo)), c->title, alpm_pkg_get_name(e->repo),
			c->version, alpm_pkg_get_version(e->repo), c->nocolor);
	if(alpm_db_get_pkg(local, alpm_pkg_get_name(e->repo))) {
		printf(" %s[%s]%s", c->meta, P_("installed"), c->nocolor);
	}
	fputs("\n    ", stdout);
	indentprint(alpm_pkg_get_desc(e->repo) ? alpm_pkg_get_desc(e->repo) : "", 4, cols);
	fputc('\n', stdout);
}

int rc_op_interactive(void)
{
	alpm_list_t *repo = NULL, *aur = NULL, *sel_targets = NULL;
	const colstr_t *c = &config->colstr;
	unsigned short cols = getcols();
	alpm_db_t *local = alpm_get_localdb(R.handle);
	entry_t *E = NULL;
	int count = 0, ret = 0;
	char line[1024], *sel = NULL;

	if(!R.aur_only) {
		for(alpm_list_t *i = alpm_get_syncdbs(R.handle); i; i = i->next) {
			alpm_list_t *found = NULL;
			if(alpm_db_search(i->data, R.targets, &found) == 0) {
				for(alpm_list_t *k = found; k; k = k->next) {
					repo = alpm_list_add(repo, k->data);
				}
			}
			alpm_list_free(found);
		}
	}
	if(!R.repo_only) {
		rc_aur_search_list(R.targets, &aur);
	}
	count = (int)(alpm_list_count(repo) + alpm_list_count(aur));
	if(count == 0) {
		rc_err(_("no packages found\n"));
		ret = 1;
		goto out;
	}
	/* Nummer 1 = bester Treffer (Repos vor AUR, AUR nach --sortby);
	 * BottomUp: 1 steht unten direkt über der Frage */
	E = calloc((size_t)count, sizeof(*E));
	if(!E) {
		ret = 1;
		goto out;
	}
	{
		int k = 0;
		for(alpm_list_t *i = repo; i; i = i->next) E[k++].repo = i->data;
		for(alpm_list_t *i = aur; i; i = i->next) E[k++].aur = i->data;
	}
	if(R.bottomup) {
		for(int k = count; k >= 1; k--) print_entry(&E[k - 1], k, local, cols);
	} else {
		for(int k = 1; k <= count; k++) print_entry(&E[k - 1], k, local, cols);
	}
	printf(_("%s==>%s Packages to install (eg: 1 2 3, 1-3 or ^4):\n%s==>%s "), c->title, c->nocolor,
			c->title, c->nocolor);
	fflush(stdout);
	if(!safe_fgets_stdin(line, sizeof(line))) {
		ret = 1;
		goto out;
	}
	sel = calloc((size_t)count + 1, 1);
	if(!sel) {
		ret = 1;
		goto out;
	}
	ret = parse_selection(line, count, sel);
	if(ret != 0) {
		if(ret < 0) {
			rc_err(_("invalid selection\n"));
		}
		ret = ret < 0 ? 1 : 0;
		goto out;
	}
	for(int k = 1; k <= count; k++) {
		char *t = NULL;
		if(!sel[k]) {
			continue;
		}
		if(E[k - 1].aur) {
			if(asprintf(&t, "aur/%s", E[k - 1].aur->name) < 0) t = NULL;
		} else if(asprintf(&t, "%s/%s", alpm_db_get_name(alpm_pkg_get_db(E[k - 1].repo)),
					alpm_pkg_get_name(E[k - 1].repo)) < 0) {
			t = NULL;
		}
		if(t) {
			sel_targets = alpm_list_add(sel_targets, t);
		}
	}
	FREELIST(R.targets);
	R.targets = sel_targets;
	R.op = 'S';
	R.tokens = alpm_list_add(R.tokens, strdup("-S"));
	ret = rc_op_sync();
out:
	free(sel);
	free(E);
	alpm_list_free(repo);
	aur_pkglist_free(aur);
	return ret;
}

void rc_usage(void)
{
	printf(_("rcyay v%s - pacman with AUR support (UsrsideOS)\n\n"), RCYAY_VERSION);
	fputs(_("In addition to all pacman operations:\n"), stdout);
	fputs(_("    rcyay                      same as rcyay -Syu (upgrade repos and AUR)\n"), stdout);
	fputs(_("    rcyay WORD ...             search repos+AUR and install a selection\n"), stdout);
	fputs(_("    rcyay -S PACKAGE ...       repo package or AUR package (built with dependencies)\n"), stdout);
	fputs(_("    rcyay -Ss/-Si WORD         also search the AUR / show AUR info (aur/...)\n"), stdout);
	fputs(_("    rcyay -Syu | -Sua          upgrade repos+AUR | AUR only\n"), stdout);
	fputs(_("    rcyay -G PKG | -Gp PKG     download build files | print PKGBUILD\n"), stdout);
	fputs(_("    rcyay -Yc                  remove unneeded dependencies\n"), stdout);
	fputs(_("    rcyay -Ps                  package statistics\n\n"), stdout);
	fputs(_("rcyay options:\n"), stdout);
	fputs(_("    -a, --aur      AUR only         --repo          repositories only\n"), stdout);
	fputs(_("    --devel        rebuild VCS packages (-git ...) when pkgver() is newer\n"), stdout);
	fputs(_("    --removemake / --noremovemake  remove make dependencies afterwards (default: yes)\n"), stdout);
	fputs(_("    --nodiff       do not show PKGBUILD/diff      --nocheck  skip check()\n"), stdout);
	fputs(_("    --pgpfetch / --nopgpfetch  import missing validpgpkeys with gpg (default: yes)\n"), stdout);
	fputs("    --sortby votes|popularity|name|modified       --topdown / --bottomup\n", stdout);
	printf(_("    --aururl URL   --builddir DIR   --rcyayconf FILE (default %s)\n\n"), RCYAY_CONF);
}
