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
		printf(" Keine unnötigen Abhängigkeiten gefunden.\n");
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
	printf("%s==> rcyay v%s – Paketstatistik%s\n", config->colstr.title, RCYAY_VERSION,
			config->colstr.nocolor);
	printf(" Installierte Pakete         : %zu\n", total);
	printf(" ausdrücklich installiert    : %zu\n", expl);
	printf(" als Abhängigkeit            : %zu\n", deps);
	printf(" fremd (AUR/lokal gebaut)    : %zu\n", nforeign);
	hs = humanize_size(size, '\0', 2, &unit);
	printf(" Gesamtgröße                 : %.2f %s\n", hs, unit);
	hs = humanize_size(fsize, '\0', 2, &unit);
	printf(" davon fremde Pakete         : %.2f %s\n", hs, unit);
	printf(" Repos                       :");
	for(alpm_list_t *i = alpm_get_syncdbs(R.handle); i; i = i->next) {
		printf(" %s", alpm_db_get_name(i->data));
	}
	printf("\n%s==> Zehn größte Pakete%s\n", config->colstr.title, config->colstr.nocolor);
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

int rc_op_interactive(void)
{
	alpm_list_t *repo = NULL, *aur = NULL, *choices = NULL, *sel_targets = NULL;
	const colstr_t *c = &config->colstr;
	unsigned short cols = getcols();
	alpm_db_t *local = alpm_get_localdb(R.handle);
	int count, n, ret = 0;
	char line[1024], *sel;

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
		rc_err("Keine Pakete gefunden.\n");
		return 1;
	}
	/* Liste von unten nach oben nummerieren: 1 steht direkt über der Frage */
	n = count;
	{
		alpm_list_t *raur = alpm_list_reverse(aur);
		rc_print_aur_search(raur, 0, 1, n);
		for(alpm_list_t *i = raur; i; i = i->next) {
			char *t = NULL;
			if(asprintf(&t, "aur/%s", ((aur_pkg_t *)i->data)->name) >= 0) {
				choices = alpm_list_add(choices, t);
			}
		}
		n -= (int)alpm_list_count(raur);
		alpm_list_free(raur);
	}
	{
		alpm_list_t *rrepo = alpm_list_reverse(repo);
		for(alpm_list_t *i = rrepo; i; i = i->next, n--) {
			alpm_pkg_t *p = i->data;
			alpm_pkg_t *lp = alpm_db_get_pkg(local, alpm_pkg_get_name(p));
			char *t = NULL;
			printf("%s%d%s %s%s/%s%s %s%s%s", c->meta, n, c->nocolor, c->repo,
					alpm_db_get_name(alpm_pkg_get_db(p)), c->title, alpm_pkg_get_name(p),
					c->version, alpm_pkg_get_version(p), c->nocolor);
			if(lp) {
				printf(" %s[installiert]%s", c->meta, c->nocolor);
			}
			fputs("\n    ", stdout);
			indentprint(alpm_pkg_get_desc(p) ? alpm_pkg_get_desc(p) : "", 4, cols);
			fputc('\n', stdout);
			if(asprintf(&t, "%s/%s", alpm_db_get_name(alpm_pkg_get_db(p)), alpm_pkg_get_name(p)) >= 0) {
				choices = alpm_list_add(choices, t);
			}
		}
		alpm_list_free(rrepo);
	}
	/* choices[0] gehört zur höchsten Nummer */
	printf("%s==>%s Pakete zum Installieren (z. B. 1 2 3, 1-3, ^4):\n%s==>%s ", c->title, c->nocolor,
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
		if(ret < 0) rc_err("Ungültige Auswahl\n");
		free(sel);
		ret = ret < 0 ? 1 : 0;
		goto out;
	}
	n = count;
	for(alpm_list_t *i = choices; i; i = i->next, n--) {
		if(sel[n]) {
			sel_targets = alpm_list_add(sel_targets, strdup(i->data));
		}
	}
	free(sel);
	FREELIST(R.targets);
	R.targets = sel_targets;
	R.op = 'S';
	R.tokens = alpm_list_add(R.tokens, strdup("-S"));
	ret = rc_op_sync();
out:
	alpm_list_free(repo);
	aur_pkglist_free(aur);
	FREELIST(choices);
	return ret;
}

void rc_usage(void)
{
	printf("rcyay v%s – pacman mit AUR (UsrsideOS)\n\n", RCYAY_VERSION);
	printf("Zusätzlich zu allen pacman-Operationen:\n");
	printf("    rcyay                      = rcyay -Syu (Repos und AUR aktualisieren)\n");
	printf("    rcyay WORT …               Repos+AUR durchsuchen und Auswahl installieren\n");
	printf("    rcyay -S PAKET …           Repo-Paket oder AUR-Paket (mit Abhängigkeiten bauen)\n");
	printf("    rcyay -Ss/-Si WORT         auch im AUR suchen bzw. Infos (aur/…)\n");
	printf("    rcyay -Syu | -Sua          Repos+AUR aktualisieren | nur AUR\n");
	printf("    rcyay -G PAKET | -Gp PAKET Bauordner holen | PKGBUILD anzeigen\n");
	printf("    rcyay -Yc                  unnötige Abhängigkeiten entfernen\n");
	printf("    rcyay -Ps                  Paketstatistik\n\n");
	printf("rcyay-Optionen:\n");
	printf("    -a, --aur      nur AUR          --repo          nur Repos\n");
	printf("    --devel        VCS-Pakete (-git …) neu bauen, wenn pkgver() neuer ist\n");
	printf("    --removemake / --noremovemake  Bauabhängigkeiten danach entfernen (Standard: ja)\n");
	printf("    --nodiff       PKGBUILD/Diff nicht anzeigen   --nocheck  check() überspringen\n");
	printf("    --aururl URL   --builddir DIR   --rcyayconf DATEI (Standard %s)\n\n", RCYAY_CONF);
}
